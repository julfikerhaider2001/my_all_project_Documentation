// ================================================================
//  Southern Water Monitor — Firmware v4.5
//  Board    : ESP32-S3 SuperMini
//  Partition: RainMaker   ← MUST set in Tools > Partition Scheme
//  Core     : ESP32 Arduino 3.3.x
// ================================================================
//  CHANGES FROM v4.4  (three bug fixes — see notes inline):
//
//  [FIX 1] RELAY WAS INVERTED  →  pump never ran in AUTO mode.
//    setPump() wrote HIGH to start the pump, but HIGH = relay OFF
//    on this opto-isolated board. All relay writes now go through
//    relayWrite(), driven by the RELAY_ACTIVE_HIGH #define so the
//    polarity is in exactly one place. Default = active-LOW (the
//    correct setting for the "HIGH = off" wiring this build uses).
//
//  [FIX 2] "Out of MQTT Budget. Dropping publish message."
//    v4.4 republished ALL 8 RainMaker params every 10 s — a burst
//    of 8 messages = 0.8 msg/s sustained. RainMaker only refills
//    publish budget at ~1 msg / 5 s (0.2 msg/s), so the budget
//    drained and updates were dropped. Reporting is now
//    change-detection based: a param is published ONLY when its
//    value actually changes, plus a slow level heartbeat. Steady-
//    state traffic is now far below the budget.
//
//  [FIX 3] State/data showed up slowly in the app.
//    Caused by (a) the 10 s polling-only reporting and (b) dropped
//    publishes once the budget was gone. Now important changes
//    (pump on/off, mode, sensor OK/fault, force) are reported the
//    instant they happen (event-driven), so the app reflects them
//    immediately instead of waiting for the next poll.
// ================================================================
//
//  PIN MAP
//  ─────────────────────────────────────────────────────────────
//  GPIO10  LED_AUTO    (LED1) — solid = AUTO mode
//  GPIO11  LED_MANUAL  (LED2) — solid = MANUAL mode
//  GPIO8   LED_WIFI    (LED3) — was RGB_GREEN. Blink=connecting, solid=connected
//  GPIO7   LED_SENSOR  (LED4) — was RGB_RED.   Off=OK, blink=fault/critical
//  GPIO9   (unused)           — was RGB_BLUE, now free
//  GPIO38  BUZZER_PIN
//  GPIO5   TRIG_PIN  (HC-SR04)
//  GPIO6   ECHO_PIN  (HC-SR04) ← voltage divider required! 5V→3.3V
//  GPIO4   RELAY_PIN (via PC817C opto, HIGH=off)
//  GPIO1   DPDT_PIN  INPUT_PULLDOWN, LOW=AUTO, HIGH=MANUAL
//
//  LED3 / LED4 STATUS BEHAVIOR
//  ─────────────────────────────────────────────────────────────
//  LED3 WiFi:    connecting → slow blink 500ms/500ms (1 Hz)
//                connected  → solid ON
//  LED4 Sensor:  sensor OK        → OFF
//                sensor fault     → fast blink 100ms/100ms (5 Hz)
//                critical/overtime→ very fast blink 60ms/65ms (~8 Hz)
// ================================================================

#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include <Preferences.h>
#include <math.h>

// ─── BLE PROVISIONING ────────────────────────────────────────────────
const char *SERVICE_NAME = "Alif's Water Monitor";
const char *POP          = "southern123";

// ─── PIN MAP ─────────────────────────────────────────────────────────
#define LED_AUTO     10
#define LED_MANUAL   11
#define LED_WIFI      8   // was RGB_GREEN
#define LED_SENSOR    7   // was RGB_RED
// GPIO9 (was RGB_BLUE) intentionally left unconfigured — free for future use

#define BUZZER_PIN   38
#define TRIG_PIN      5
#define ECHO_PIN      6
#define RELAY_PIN     4
#define DPDT_PIN      1

// ─── LED3/LED4 WIRING POLARITY ───────────────────────────────────────
// true  = standard wiring: GPIO HIGH turns the LED ON (most common)
// false = LED wired through the old RGB module's shared-anode rail,
//         where GPIO LOW turns the LED ON.
// If LED3/LED4 behave backwards, flip this single line and reflash.
#define NEW_LEDS_ACTIVE_HIGH  true

// ─── RELAY DRIVE POLARITY  [FIX 1] ───────────────────────────────────
// Most opto-isolated (PC817) relay boards are ACTIVE-LOW: driving the
// GPIO LOW energizes the relay (pump ON), and HIGH releases it
// (pump OFF). That matches every "HIGH = relay off" note in this build,
// so the correct value here is `false`.
//
// If, after this update, the pump runs INVERTED (off when it should be
// on, or on at boot), your board is active-high — flip this one line to
// `true` and reflash. Nothing else needs to change.
#define RELAY_ACTIVE_HIGH  false

// ─── CONFIG DEFAULTS ─────────────────────────────────────────────────
#define DEFAULT_TANK_HEIGHT  80
#define DEFAULT_START_PCT    25
#define DEFAULT_STOP_PCT     70

// ─── FLASH KEYS ──────────────────────────────────────────────────────
#define PREF_NAMESPACE  "swm_cfg"
#define PREF_TANK_H     "tankH"
#define PREF_START      "startPct"
#define PREF_STOP       "stopPct"

// ─── SAFETY ──────────────────────────────────────────────────────────
#define PUMP_OVERTIME_MS  1800000UL   // 30 min hard cutoff

// ─── RAINMAKER PARAM NAMES ───────────────────────────────────────────
#define PARAM_LEVEL   "Water Level"
#define PARAM_PUMP    "Pump Running"
#define PARAM_MODE    "Mode"
#define PARAM_SENSOR  "Sensor OK"
#define PARAM_START   "Start At %"
#define PARAM_STOP    "Stop At %"
#define PARAM_TANKH   "Tank Height cm"
#define PARAM_FORCE   "Force Pump"

// ─── REPORTING CADENCE  [FIX 2 / FIX 3] ──────────────────────────────
// RM_TICK_MS:      how often we *check* for changes and push the level.
//                  5 s keeps us at/below RainMaker's ~1-msg-per-5s
//                  publish-budget refill rate even when the tank is
//                  filling continuously.
// RM_HEARTBEAT_MS: force a single level publish at least this often so
//                  the app graph stays alive even if nothing changed.
// LEVEL_REPORT_DELTA: ignore sub-1% sensor jitter so we don't waste
//                  publishes on noise.
#define RM_TICK_MS          5000UL
#define RM_HEARTBEAT_MS     60000UL
#define LEVEL_REPORT_DELTA  1.0f

// ─── ENUMS ───────────────────────────────────────────────────────────
enum SysState {
  SYS_WIFI_CONNECTING,
  SYS_NORMAL,
  SYS_PUMP_RUNNING,
  SYS_SENSOR_FAULT,
  SYS_CRITICAL_ERROR
};
enum Mode { AUTO_MODE, MANUAL_MODE };

// ─────────────────────────────────────────────────────────────────────
//  LED BLINKER — used for ALL FOUR LEDs (Auto, Manual, WiFi, Sensor)
//
//  Each LED is fully independent — its own GPIO, its own timer state.
//
//  `activeHigh` selects the wiring style without duplicating logic:
//    activeHigh = true   → digitalWrite(pin, HIGH) = LED on  (default)
//    activeHigh = false  → digitalWrite(pin, LOW)  = LED on
// ─────────────────────────────────────────────────────────────────────
struct LedBlinker {
  uint8_t  pin;
  bool     activeHigh;
  uint32_t onMs, offMs, pauseMs;
  int      repeats, count;
  bool     state;
  uint32_t lastChange;
  bool     inPause;

  void _write(bool on) {
    bool physicalHigh = activeHigh ? on : !on;
    digitalWrite(pin, physicalHigh ? HIGH : LOW);
  }

  void begin(uint8_t p, bool activeHighWiring = true) {
    pin        = p;
    activeHigh = activeHighWiring;
    pinMode(p, OUTPUT);
    _write(false);     // start OFF, respecting polarity
    repeats = 0;
  }

  void solid(bool on) {
    repeats = 0;
    inPause = false;
    _write(on);
  }

  void blink(uint32_t on, uint32_t off, int rep = -1, uint32_t pause = 0) {
    // Already running this exact blink? Don't restart it — that keeps the
    // phase smooth if applyIndicators() gets called repeatedly while the
    // same condition (e.g. "WiFi connecting") persists.
    if (repeats == rep && onMs == on && offMs == off && pauseMs == pause)
      return;
    onMs = on; offMs = off; repeats = rep; pauseMs = pause;
    count = 0; inPause = false;
    state = true; lastChange = millis();
    _write(true);
  }

  void update() {
    if (repeats == 0) return;
    uint32_t now = millis();
    if (inPause) {
      if (now - lastChange >= pauseMs) {
        inPause = false; count = 0;
        state = true; lastChange = now;
        _write(true);
      }
      return;
    }
    if (now - lastChange < (state ? onMs : offMs)) return;
    state = !state;
    _write(state);
    lastChange = now;
    if (!state) {
      count++;
      if (repeats > 0 && count >= repeats) {
        if (pauseMs > 0) { inPause = true; lastChange = now; count = 0; }
        else             { repeats = 0; _write(false); }
      }
    }
  }
};

// ─────────────────────────────────────────────────────────────────────
//  NON-BLOCKING BUZZER (unchanged)
// ─────────────────────────────────────────────────────────────────────
struct BuzzerCtrl {
  uint8_t  pin;
  bool     active;
  uint32_t onMs, offMs;
  int      repeats, count;
  bool     state;
  uint32_t lastChange;

  void begin(uint8_t p) {
    pin = p; pinMode(p, OUTPUT); digitalWrite(p, LOW); active = false;
  }

  // Soft beep — ignored if buzzer already active
  void beep(uint32_t on, uint32_t off, int rep) {
    if (active) return;
    _start(on, off, rep);
  }

  // Force beep — interrupts any ongoing pattern
  void beepForce(uint32_t on, uint32_t off, int rep) {
    _start(on, off, rep);
  }

  void _start(uint32_t on, uint32_t off, int rep) {
    onMs = on; offMs = off; repeats = rep; count = 0;
    state = true; lastChange = millis(); active = true;
    digitalWrite(pin, HIGH);
  }

  void update() {
    if (!active) return;
    uint32_t now = millis();
    if (now - lastChange < (state ? onMs : offMs)) return;
    state = !state;
    digitalWrite(pin, state ? HIGH : LOW);
    lastChange = now;
    if (!state) {
      count++;
      if (count >= repeats) { active = false; digitalWrite(pin, LOW); }
    }
  }
};

// ─── GLOBALS ─────────────────────────────────────────────────────────
LedBlinker  ledAuto, ledManual, ledWifi, ledSensor;
BuzzerCtrl  buzzer;
Preferences prefs;

float    waterDistCm   = 0;
float    waterDepthCm  = 0;
float    waterPct      = 0;
bool     sensorOK      = false;
uint8_t  sensorFails   = 0;
uint32_t lastSensorMs  = 0;

bool     pumpOn        = false;
uint32_t pumpStartMs   = 0;
uint32_t totalRuntimeS = 0;

Mode     currentMode   = AUTO_MODE;
bool     lastDPDT      = false;
SysState sysState      = SYS_WIFI_CONNECTING;

int  g_tankHeight = DEFAULT_TANK_HEIGHT;
int  g_startPct   = DEFAULT_START_PCT;
int  g_stopPct    = DEFAULT_STOP_PCT;
bool g_forcePump  = false;

static Device *waterMonitor = NULL;
uint32_t lastRMUpdate    = 0;
uint32_t lastRMHeartbeat = 0;

// ─── RAINMAKER REPORT CACHE  [FIX 2] ─────────────────────────────────
// Last value we successfully published for each param. A param is only
// re-published when its live value differs from the cache, which is what
// keeps us inside the MQTT publish budget. Sentinels guarantee the first
// report (and every reconnect) pushes a full snapshot.
static float rep_level  = -1000.0f;
static int   rep_pump   = -1;   // 0 / 1
static int   rep_mode   = -1;   // 0 = Auto, 1 = Manual
static int   rep_sensor = -1;   // 0 / 1
static int   rep_start  = -1;
static int   rep_stop   = -1;
static int   rep_tankH  = -1;
static int   rep_force  = -1;   // 0 / 1

void resetReportCache() {
  rep_level  = -1000.0f;
  rep_pump   = -1;
  rep_mode   = -1;
  rep_sensor = -1;
  rep_start  = -1;
  rep_stop   = -1;
  rep_tankH  = -1;
  rep_force  = -1;
}

// ─── RELAY HELPER  [FIX 1] ───────────────────────────────────────────
// Single source of truth for relay polarity. Pass the desired PUMP state
// (true = pump running). The cache var below mirrors the physical pin so
// we never issue a redundant write.
static int relayPhysical = -1;   // -1 unknown, else last HIGH/LOW written

static inline void relayWrite(bool pumpRunning) {
  // active-low board: pump ON → LOW ; active-high board: pump ON → HIGH
  int level = (RELAY_ACTIVE_HIGH ? pumpRunning : !pumpRunning) ? HIGH : LOW;
  if (level != relayPhysical) {
    digitalWrite(RELAY_PIN, level);
    relayPhysical = level;
  }
}

// ─────────────────────────────────────────────────────────────────────
//  FLASH CONFIG
// ─────────────────────────────────────────────────────────────────────
void loadConfig() {
  prefs.begin(PREF_NAMESPACE, true);
  g_tankHeight = prefs.getInt(PREF_TANK_H, DEFAULT_TANK_HEIGHT);
  g_startPct   = prefs.getInt(PREF_START,  DEFAULT_START_PCT);
  g_stopPct    = prefs.getInt(PREF_STOP,   DEFAULT_STOP_PCT);
  prefs.end();
  Serial.printf("[Config] Tank=%dcm  Start=%d%%  Stop=%d%%\n",
                g_tankHeight, g_startPct, g_stopPct);
}

void saveConfigValue(const char *key, int value) {
  prefs.begin(PREF_NAMESPACE, false);
  prefs.putInt(key, value);
  prefs.end();
}

// ─────────────────────────────────────────────────────────────────────
//  RAINMAKER REPORTING  [FIX 2 / FIX 3]
//
//  Publishes ONLY the params whose values changed since the last
//  successful publish. Because updateAndReportParam() returns an
//  esp_err_t, we advance a param's cache only when the publish was
//  accepted — so a message dropped while MQTT is still connecting (or
//  for any other reason) is automatically retried on the next call
//  instead of being silently lost.
//
//  `forceLevel` re-publishes the water level even if it hasn't moved
//  (used by the periodic heartbeat).
// ─────────────────────────────────────────────────────────────────────
void rmReport(bool forceLevel) {
  if (!waterMonitor || WiFi.status() != WL_CONNECTED) return;

  // Water level — skip sub-delta jitter unless forced
  if (forceLevel || fabsf(waterPct - rep_level) >= LEVEL_REPORT_DELTA) {
    if (waterMonitor->updateAndReportParam(PARAM_LEVEL, waterPct) == ESP_OK)
      rep_level = waterPct;
  }

  int v;

  v = pumpOn ? 1 : 0;
  if (v != rep_pump &&
      waterMonitor->updateAndReportParam(PARAM_PUMP, pumpOn) == ESP_OK)
    rep_pump = v;

  v = (currentMode == AUTO_MODE) ? 0 : 1;
  if (v != rep_mode &&
      waterMonitor->updateAndReportParam(
        PARAM_MODE, currentMode == AUTO_MODE ? "Auto" : "Manual") == ESP_OK)
    rep_mode = v;

  v = sensorOK ? 1 : 0;
  if (v != rep_sensor &&
      waterMonitor->updateAndReportParam(PARAM_SENSOR, sensorOK) == ESP_OK)
    rep_sensor = v;

  if (g_startPct != rep_start &&
      waterMonitor->updateAndReportParam(PARAM_START, g_startPct) == ESP_OK)
    rep_start = g_startPct;

  if (g_stopPct != rep_stop &&
      waterMonitor->updateAndReportParam(PARAM_STOP, g_stopPct) == ESP_OK)
    rep_stop = g_stopPct;

  if (g_tankHeight != rep_tankH &&
      waterMonitor->updateAndReportParam(PARAM_TANKH, g_tankHeight) == ESP_OK)
    rep_tankH = g_tankHeight;

  v = g_forcePump ? 1 : 0;
  if (v != rep_force &&
      waterMonitor->updateAndReportParam(PARAM_FORCE, g_forcePump) == ESP_OK)
    rep_force = v;
}

// ─────────────────────────────────────────────────────────────────────
//  APPLY ALL INDICATORS
// ─────────────────────────────────────────────────────────────────────
void applyIndicators() {

  // LED1 / LED2 — Mode indicators
  ledAuto.solid(currentMode == AUTO_MODE);
  ledManual.solid(currentMode == MANUAL_MODE);

  // LED3 — WiFi status (live link, not sysState)
  if (WiFi.status() == WL_CONNECTED) {
    ledWifi.solid(true);          // solid ON = connected
  } else {
    ledWifi.blink(500, 500);      // slow blink = connecting
  }

  // LED4 — Sensor / safety fault status
  if (sysState == SYS_CRITICAL_ERROR) {
    ledSensor.blink(60, 65);      // very fast blink = overtime/critical
  } else if (sysState == SYS_SENSOR_FAULT) {
    ledSensor.blink(100, 100);    // fast blink = sensor fault
  } else {
    ledSensor.solid(false);       // OFF = sensor OK, no fault
  }
}

// ─── PUMP CONTROL  [FIX 1 + FIX 3] ───────────────────────────────────
void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  relayWrite(on);                 // [FIX 1] correct polarity, single source
  if (on) {
    pumpStartMs = millis();
    sysState    = SYS_PUMP_RUNNING;
    buzzer.beep(150, 100, 2);
  } else {
    totalRuntimeS += (millis() - pumpStartMs) / 1000;
    sysState = SYS_NORMAL;
    buzzer.beep(80, 0, 1);
  }
  applyIndicators();
  rmReport(false);                // [FIX 3] push pump state immediately
  Serial.println(on ? "[Pump] ON" : "[Pump] OFF");
}

// ─── ULTRASONIC (3-ping average) ─────────────────────────────────────
float readUltrasonic() {
  float total = 0;
  int   valid = 0;
  for (int i = 0; i < 3; i++) {
    digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);   // HC-SR04: 10us trigger
    digitalWrite(TRIG_PIN, LOW);
    long dur = pulseIn(ECHO_PIN, HIGH, 23200);
    if (dur > 0) {
      float d = dur * 0.01715f;
      if (d >= 2.0f && d <= 400.0f) { total += d; valid++; }
    }
    delay(30);
  }
  return (valid == 0) ? -1.0f : total / valid;
}

// ─── RAINMAKER WRITE CALLBACK ─────────────────────────────────────────
void write_callback(Device *device, Param *param,
                    const param_val_t val, void *priv_data,
                    write_ctx_t *ctx)
{
  const char *name = param->getParamName();

  if (strcmp(name, PARAM_START) == 0) {
    g_startPct = constrain(val.val.i, 5, 95);
    param->updateAndReport(value(g_startPct));   // ack the *constrained* value
    rep_start = g_startPct;                       // keep cache in sync
    saveConfigValue(PREF_START, g_startPct);
    Serial.printf("[Config] Start=%d%%\n", g_startPct);
  }
  else if (strcmp(name, PARAM_STOP) == 0) {
    g_stopPct = constrain(val.val.i, g_startPct + 5, 100);
    param->updateAndReport(value(g_stopPct));
    rep_stop = g_stopPct;
    saveConfigValue(PREF_STOP, g_stopPct);
    Serial.printf("[Config] Stop=%d%%\n", g_stopPct);
  }
  else if (strcmp(name, PARAM_TANKH) == 0) {
    g_tankHeight = constrain(val.val.i, 20, 500);
    param->updateAndReport(value(g_tankHeight));
    rep_tankH = g_tankHeight;
    saveConfigValue(PREF_TANK_H, g_tankHeight);
    Serial.printf("[Config] TankH=%dcm\n", g_tankHeight);
  }
  else if (strcmp(name, PARAM_FORCE) == 0) {
    if (currentMode == MANUAL_MODE) {
      param->updateAndReport(value(false));
      rep_force = 0;
      Serial.println("[Override] Ignored — MANUAL mode");
      return;
    }
    g_forcePump = val.val.b;
    param->updateAndReport(val);
    rep_force = g_forcePump ? 1 : 0;
    if (g_forcePump) {
      Serial.println("[Override] Pump FORCED ON");
      setPump(true);
    } else {
      Serial.println("[Override] Pump FORCED OFF");
      setPump(false);
    }
  }
}

// ─── PROVISIONING EVENTS ─────────────────────────────────────────────
void sysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {

    case ARDUINO_EVENT_PROV_START:
      Serial.println("[Prov] BLE started — open RainMaker app");
      buzzer.beepForce(800, 0, 1);
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
      Serial.println("[Prov] Credentials received");
      buzzer.beepForce(80, 80, 3);
      break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("[Prov] FAILED — wrong password or 5GHz?");
      buzzer.beepForce(100, 100, 4);
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WiFi] Connected — IP: %s\n",
                    WiFi.localIP().toString().c_str());
      sysState = SYS_NORMAL;
      applyIndicators();          // LED3 → solid
      buzzer.beepForce(400, 0, 1);
      // Force a full re-sync shortly after connecting. We don't publish
      // here directly because the MQTT session may not be up yet; instead
      // we clear the cache and schedule the next report ~1.5 s out. Any
      // publish that is still too early simply isn't cached and is retried.
      resetReportCache();
      lastRMUpdate    = millis() - (RM_TICK_MS - 1500);
      lastRMHeartbeat = millis();
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WiFi] Disconnected");
      sysState = SYS_WIFI_CONNECTING;
      applyIndicators();          // LED3 → slow blink
      resetReportCache();         // re-publish everything on reconnect
      break;

    default: break;
  }
}

// ─── SETUP ───────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Southern Water Monitor v4.5 ===");

  loadConfig();

  // Mode LEDs — unchanged wiring/polarity
  ledAuto.begin(LED_AUTO);
  ledManual.begin(LED_MANUAL);

  // WiFi / Sensor LEDs — polarity controlled by NEW_LEDS_ACTIVE_HIGH
  ledWifi.begin(LED_WIFI, NEW_LEDS_ACTIVE_HIGH);
  ledSensor.begin(LED_SENSOR, NEW_LEDS_ACTIVE_HIGH);

  // Buzzer
  buzzer.begin(BUZZER_PIN);
  buzzer.beep(80, 80, 3);   // 3 short beeps — boot

  // Peripherals
  pinMode(TRIG_PIN,  OUTPUT);
  pinMode(ECHO_PIN,  INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  relayWrite(false);                 // [FIX 1] relay OFF at boot, via helper
  pinMode(DPDT_PIN,  INPUT_PULLDOWN);

  lastDPDT    = digitalRead(DPDT_PIN);
  currentMode = (lastDPDT == HIGH) ? MANUAL_MODE : AUTO_MODE;
  Serial.printf("[Boot] Mode: %s\n",
                currentMode == AUTO_MODE ? "AUTO" : "MANUAL");

  // Initial LED state — LED3 slow blink while connecting
  sysState = SYS_WIFI_CONNECTING;
  applyIndicators();

  // ── RainMaker setup ───────────────────────────────────────────
  Node node = RMaker.initNode("Alif's Home Water Circuit");
  waterMonitor = new Device("Water Tank", "esp.device.water-sensor", NULL);

  Param lvlParam(PARAM_LEVEL, "esp.param.percentage",
                 value(0.0f), PROP_FLAG_READ);
  lvlParam.addUIType("esp.ui.slider");
  waterMonitor->addParam(lvlParam);

  Param pumpParam(PARAM_PUMP, "esp.param.power",
                  value(false), PROP_FLAG_READ);
  waterMonitor->addParam(pumpParam);

  Param modeParam(PARAM_MODE, "esp.param.mode",
                  value(currentMode == AUTO_MODE ? "Auto" : "Manual"),
                  PROP_FLAG_READ);
  waterMonitor->addParam(modeParam);

  Param sensorParam(PARAM_SENSOR, "esp.param.power",
                    value(false), PROP_FLAG_READ);
  waterMonitor->addParam(sensorParam);

  Param startParam(PARAM_START, "esp.param.level",
                   value(g_startPct), PROP_FLAG_READ | PROP_FLAG_WRITE);
  startParam.addUIType("esp.ui.slider");
  waterMonitor->addParam(startParam);

  Param stopParam(PARAM_STOP, "esp.param.level",
                  value(g_stopPct), PROP_FLAG_READ | PROP_FLAG_WRITE);
  stopParam.addUIType("esp.ui.slider");
  waterMonitor->addParam(stopParam);

  Param tankHParam(PARAM_TANKH, "esp.param.length",
                   value(g_tankHeight), PROP_FLAG_READ | PROP_FLAG_WRITE);
  waterMonitor->addParam(tankHParam);

  Param forceParam(PARAM_FORCE, "esp.param.power",
                   value(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
  forceParam.addUIType("esp.ui.toggle");
  waterMonitor->addParam(forceParam);

  waterMonitor->addCb(write_callback);
  node.addDevice(*waterMonitor);

  RMaker.enableOTA(OTA_USING_TOPICS);
  RMaker.enableTZService();
  RMaker.enableSchedule();
  RMaker.start();

  WiFi.onEvent(sysProvEvent);

  WiFiProv.beginProvision(
    WIFI_PROV_SCHEME_BLE,
    WIFI_PROV_SCHEME_HANDLER_FREE_BTDM,
    WIFI_PROV_SECURITY_1,
    POP,
    SERVICE_NAME
  );
}

// ─── LOOP ────────────────────────────────────────────────────────────
void loop() {

  // Non-blocking drivers — must run every loop
  ledAuto.update();
  ledManual.update();
  ledWifi.update();
  ledSensor.update();
  buzzer.update();

  // ── WiFi state watchdog ──────────────────────────────────────
  static bool     lastWifiState = false;
  static uint32_t lastWifiRetry = 0;
  bool wifiNow = (WiFi.status() == WL_CONNECTED);

  if (wifiNow != lastWifiState) {
    lastWifiState = wifiNow;
    if (!wifiNow) {
      sysState = SYS_WIFI_CONNECTING;
      applyIndicators();
      resetReportCache();          // re-sync everything once we're back
    }
  }

  if (!wifiNow && millis() - lastWifiRetry > 30000) {
    lastWifiRetry = millis();
    Serial.println("[WiFi] Retrying...");
    WiFi.disconnect(true);
    delay(200);
    WiFi.begin();
  }

  // ── DPDT mode switch ─────────────────────────────────────────
  static uint32_t lastModeCheck = 0;
  if (millis() - lastModeCheck > 200) {
    lastModeCheck = millis();
    bool dpdt = digitalRead(DPDT_PIN);
    if (dpdt != lastDPDT) {
      lastDPDT    = dpdt;
      currentMode = (dpdt == HIGH) ? MANUAL_MODE : AUTO_MODE;
      Serial.printf("[Mode] → %s\n",
                    currentMode == AUTO_MODE ? "AUTO" : "MANUAL");
      buzzer.beepForce(60, 60, 2);

      if (currentMode == MANUAL_MODE) {
        if (pumpOn) {
          pumpOn = false;
          relayWrite(false);                 // [FIX 1] release relay
          totalRuntimeS += (millis() - pumpStartMs) / 1000;
          sysState = SYS_NORMAL;
        }
        if (g_forcePump) {
          g_forcePump = false;
          Serial.println("[Override] Cleared — MANUAL");
        }
      }
      applyIndicators();
      rmReport(false);             // [FIX 3] push mode/pump/force instantly
    }
  }

  // ── Ultrasonic sensor ────────────────────────────────────────
  if (millis() - lastSensorMs > 2000) {
    lastSensorMs = millis();
    float dist = readUltrasonic();

    if (dist < 0) {
      sensorFails++;
      Serial.printf("[Sensor] FAIL #%d\n", sensorFails);
      if (sensorFails >= 3) {
        bool wasOK = sensorOK;
        sensorOK = false;
        if (currentMode == AUTO_MODE && pumpOn) setPump(false);
        if (wasOK) {
          sysState = SYS_SENSOR_FAULT;
          applyIndicators();          // LED4 → fast blink
          buzzer.beepForce(80, 80, 4);
          rmReport(false);            // [FIX 3] push sensor fault now
        }
      }
    } else {
      sensorFails  = 0;
      bool wasOK   = sensorOK;
      sensorOK     = true;
      waterDistCm  = dist;
      waterDepthCm = max(0.0f, (float)g_tankHeight - waterDistCm);
      waterPct     = constrain(waterDepthCm / g_tankHeight * 100.0f,
                               0.0f, 100.0f);

      Serial.printf("[Sensor] dist=%.1f  depth=%.1f  level=%.1f%%\n",
                    waterDistCm, waterDepthCm, waterPct);

      if (!wasOK) {
        // Sensor recovered
        sysState = pumpOn ? SYS_PUMP_RUNNING : SYS_NORMAL;
        applyIndicators();           // LED4 → off
        buzzer.beep(200, 100, 2);
        rmReport(false);             // [FIX 3] push recovery + level now
      }
    }
  }

  // ── AUTO pump logic ──────────────────────────────────────────
  if (currentMode == AUTO_MODE && sensorOK) {

    if (!g_forcePump) {
      if (!pumpOn && waterPct < (float)g_startPct) {
        Serial.printf("[AUTO] ON — %.1f%% < %d%%\n", waterPct, g_startPct);
        setPump(true);
      }
      if (pumpOn && waterPct >= (float)g_stopPct) {
        Serial.printf("[AUTO] OFF — %.1f%% >= %d%%\n", waterPct, g_stopPct);
        setPump(false);
      }
    } else {
      if (pumpOn && waterPct >= (float)g_stopPct) {
        Serial.printf("[Force] Auto-stop at %.1f%%\n", waterPct);
        g_forcePump = false;         // set before setPump so report reflects it
        setPump(false);              // setPump() reports pump + force
      }
    }

    // 30-min overtime hard cutoff
    if (pumpOn && (millis() - pumpStartMs) > PUMP_OVERTIME_MS) {
      Serial.println("[Safety] OVERTIME — pump killed");
      g_forcePump = false;
      setPump(false);
      sysState = SYS_CRITICAL_ERROR;
      applyIndicators();            // LED4 → very fast blink
      buzzer.beepForce(100, 80, 6);
    }
  }

  // ── RainMaker change-detection report  [FIX 2] ────────────────
  // Publishes only what changed; forces a level refresh on heartbeat.
  if (millis() - lastRMUpdate > RM_TICK_MS) {
    lastRMUpdate = millis();
    bool heartbeat = (millis() - lastRMHeartbeat > RM_HEARTBEAT_MS);
    if (heartbeat) lastRMHeartbeat = millis();
    rmReport(heartbeat);
  }
}
