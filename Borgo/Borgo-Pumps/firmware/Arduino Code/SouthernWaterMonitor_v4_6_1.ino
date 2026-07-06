// ================================================================
//  Southern Water Monitor — Firmware v4.6.1
//  Board    : ESP32-S3 SuperMini
//  Partition: RainMaker   ← MUST set in Tools > Partition Scheme
//  Core     : ESP32 Arduino 3.3.x
// ================================================================
//  CHANGES FROM v4.5  (see the matching [v4.6 #n] tags inline):
//
//  [v4.6 #1] UART SENSOR SUPPORT (OPTIONAL — OFF by default in v4.6.1).
//    NOTE: v4.6.1 ships in HC-SR04 trig/echo mode (USE_UART_SENSOR 0),
//    identical wiring to v4.5 — no rewire needed. The UART path below
//    is kept for the future; enable it only if you switch sensors.
//    Trig/echo reading is unchanged from v4.5, but now feeds the new
//    median + smoothing filter so the level is steadier.
//
//    >>> WIRING (you must do this once, OTA cannot do it) <<<
//    We reuse the SAME two pins, so it is a small change:
//      Sensor TX  ->  GPIO6   (old ECHO pin)  ← keep the divider here
//      Sensor RX  <-  GPIO5   (old TRIG pin)  ← optional, see below
//      Sensor VCC ->  5V       Sensor GND -> GND
//    Most of these sensors auto-stream data and need only ONE signal
//    wire (their TX -> GPIO6). The GPIO5/sensor-RX wire is only needed
//    if your sensor is in "controlled/triggered" UART mode; leaving it
//    connected is harmless either way.
//    KEEP the existing 5V->3.3V voltage divider on GPIO6 — it protects
//    the ESP if the sensor's TX idles at 5V. A divider does not hurt a
//    9600-baud line.
//
//    If you ever want to go back to the old trig/echo sensor, set
//    USE_UART_SENSOR to 0 below and reflash. No other change needed.
//
//  [v4.6 #2] PUMP NO LONGER STOPS EARLY / CYCLES MID-FILL.
//    Two causes were fixed:
//      (a) Filtering: a noisy spike (foam/turbulence right under the
//          sensor while filling makes the water look momentarily high)
//          used to cross the Stop % and shut the pump off. Readings now
//          pass through a 5-sample MEDIAN filter (kills single spikes)
//          + a light smoothing filter. One bad sample can no longer
//          flip the pump.
//      (b) Confirmation: the pump only STARTS after the level is below
//          Start % for a few readings in a row, and only STOPS after it
//          is at/above Stop % for a few readings in a row. Transients
//          can't toggle it anymore.
//    NOTE: also check "Stop At %" in the app — if it is set near 40%,
//    the pump is *correctly* stopping at 40%. Set it to your real full
//    level (e.g. 80–90%). Force Pump fills up to Stop %, then releases.
//
//  [v4.6 #3] BUZZER REMOVED.
//    There was never a buzzer wired, so all buzzer code and the
//    BUZZER_PIN (GPIO38) definition were deleted. GPIO38 is now free.
//
//  [v4.6 #4] SENSOR-PRESENCE INDICATOR.
//    The firmware now distinguishes three sensor conditions and shows
//    each one clearly on LED4 *and* in the app ("Sensor Status"):
//      OK        -> LED4 OFF                       app: "OK"
//      Fault     -> LED4 fast blink (was working,  app: "Fault"
//                   now intermittent / bad data)
//      No Sensor -> LED4 double-blink + pause      app: "No Sensor"
//                   (never produced a reading -> not wired/powered)
//    In AUTO mode the pump is held OFF whenever the sensor isn't OK, so
//    a disconnected sensor can never run the pump blind.
//
//  (Carried over from v4.5: correct relay polarity via relayWrite(),
//   change-detection RainMaker reporting to stay inside the MQTT
//   budget, and event-driven pushes so the app updates instantly.)
// ================================================================
//
//  PIN MAP
//  ─────────────────────────────────────────────────────────────
//  GPIO10  LED_AUTO    (LED1) — solid = AUTO mode
//  GPIO11  LED_MANUAL  (LED2) — solid = MANUAL mode
//  GPIO8   LED_WIFI    (LED3) — blink = connecting, solid = connected
//  GPIO7   LED_SENSOR  (LED4) — sensor status (see [v4.6 #4] above)
//  GPIO9   (unused / free)
//  GPIO38  (unused / free)    — was BUZZER_PIN, now removed
//  GPIO5   SENSOR_UART_TX  -> sensor RX  (was TRIG_PIN)
//  GPIO6   SENSOR_UART_RX  <- sensor TX  (was ECHO_PIN) ← keep divider
//  GPIO4   RELAY_PIN (via PC817C opto, HIGH = off)
//  GPIO1   DPDT_PIN  INPUT_PULLDOWN, LOW = AUTO, HIGH = MANUAL
// ================================================================

#include "RMaker.h"
#include "WiFi.h"
#include "WiFiProv.h"
#include <Preferences.h>
#include <math.h>

// ─── BLE PROVISIONING ────────────────────────────────────────────────
const char *SERVICE_NAME = "Alif's Water Monitor";
const char *POP          = "southern123";

// ─── SENSOR COMMUNICATION MODE  [v4.6 #1] ────────────────────────────
// 0 = HC-SR04 trig/echo  (ACTIVE — same wiring as v4.5, no rewire)
// 1 = UART               (only if you physically rewire to UART)
#define USE_UART_SENSOR  0

// ─── PIN MAP ─────────────────────────────────────────────────────────
#define LED_AUTO     10
#define LED_MANUAL   11
#define LED_WIFI      8
#define LED_SENSOR    7

// Same physical pins, dual-purpose names so UART/legacy both work.
#define TRIG_PIN         5
#define ECHO_PIN         6
#define SENSOR_UART_TX   TRIG_PIN   // ESP TX  -> sensor RX (controlled mode only)
#define SENSOR_UART_RX   ECHO_PIN   // ESP RX  <- sensor TX (keep divider here)

#define RELAY_PIN     4
#define DPDT_PIN      1

// ─── LED3/LED4 WIRING POLARITY ───────────────────────────────────────
// true  = GPIO HIGH turns the LED ON (most common)
// false = GPIO LOW turns the LED ON (old shared-anode RGB rail)
#define NEW_LEDS_ACTIVE_HIGH  true

// ─── RELAY DRIVE POLARITY ────────────────────────────────────────────
// Opto-isolated (PC817) boards are usually ACTIVE-LOW: GPIO LOW = pump
// ON, HIGH = pump OFF. That matches this build, so keep `false`.
// If the pump runs inverted after flashing, set this to `true`.
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

// ─── SENSOR FILTER / TIMING  [v4.6 #1 / #2] ──────────────────────────
#define MEDIAN_WINDOW     5      // samples in the spike-rejecting median
#define EMA_ALPHA         0.40f  // smoothing strength (0..1, higher = snappier)
#define START_CONFIRM     2      // readings below Start% before pump ON
#define STOP_CONFIRM      4      // readings at/above Stop% before pump OFF
#define SENSOR_MIN_CM     2.0f   // valid distance window
#define SENSOR_MAX_CM     450.0f

#if USE_UART_SENSOR
  #define SENSOR_INTERVAL_MS    250UL   // how often we evaluate a reading
  #define UART_FRAME_TIMEOUT_MS 1000UL  // no fresh frame this long = a "miss"
  #define SENSOR_FAIL_LIMIT     8       // consecutive misses -> Fault
  #define NO_SENSOR_BOOT_MS     4000UL  // no reading ever by this -> No Sensor
  #define UART_BAUD             9600
#else
  #define SENSOR_INTERVAL_MS    500UL
  #define SENSOR_FAIL_LIMIT     4
  #define NO_SENSOR_BOOT_MS     5000UL
#endif

// ─── RAINMAKER PARAM NAMES ───────────────────────────────────────────
#define PARAM_LEVEL    "Water Level"
#define PARAM_PUMP     "Pump Running"
#define PARAM_MODE     "Mode"
#define PARAM_SENSOR   "Sensor OK"          // kept for backward-compat automations
#define PARAM_SSTATUS  "Sensor Status"      // [v4.6 #4] human-readable text
#define PARAM_START    "Start At %"
#define PARAM_STOP     "Stop At %"
#define PARAM_TANKH    "Tank Height cm"
#define PARAM_FORCE    "Force Pump"

// ─── REPORTING CADENCE ───────────────────────────────────────────────
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

// [v4.6 #4] Distinct sensor conditions
enum SensorState {
  SENSOR_STAT_INIT,    // booting, no reading yet (grace period)
  SENSOR_STAT_OK,      // healthy
  SENSOR_STAT_FAULT,   // was working, now intermittent / out of range
  SENSOR_STAT_NONE     // never produced a reading -> not wired/powered
};

// ─────────────────────────────────────────────────────────────────────
//  LED BLINKER — used for ALL FOUR LEDs
//  Supports a "burst + pause" pattern (repeats + pauseMs), used for the
//  distinctive "No Sensor" double-blink.
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
    _write(false);
    repeats = 0;
  }

  void solid(bool on) {
    repeats = 0;
    inPause = false;
    _write(on);
  }

  void blink(uint32_t on, uint32_t off, int rep = -1, uint32_t pause = 0) {
    if (repeats == rep && onMs == on && offMs == off && pauseMs == pause)
      return;                       // already running this exact pattern
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

// ─── GLOBALS ─────────────────────────────────────────────────────────
LedBlinker  ledAuto, ledManual, ledWifi, ledSensor;
Preferences prefs;

float    waterDistCm   = 0;
float    waterDepthCm  = 0;
float    waterPct      = 0;
bool     sensorOK      = false;
uint8_t  sensorFails   = 0;
bool     sensorEverSeen = false;
SensorState sensorState = SENSOR_STAT_INIT;
uint32_t lastSensorMs  = 0;
uint32_t g_bootMs      = 0;

bool     pumpOn        = false;
uint32_t pumpStartMs   = 0;
uint32_t totalRuntimeS = 0;

// [v4.6 #2] threshold-confirmation counters
uint8_t  belowStartCount = 0;
uint8_t  aboveStopCount  = 0;

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

// ─── RAINMAKER REPORT CACHE ──────────────────────────────────────────
static float rep_level   = -1000.0f;
static int   rep_pump    = -1;
static int   rep_mode    = -1;
static int   rep_sensor  = -1;
static int   rep_sstate  = -1;   // [v4.6 #4]
static int   rep_start   = -1;
static int   rep_stop    = -1;
static int   rep_tankH   = -1;
static int   rep_force   = -1;

void resetReportCache() {
  rep_level   = -1000.0f;
  rep_pump    = -1;
  rep_mode    = -1;
  rep_sensor  = -1;
  rep_sstate  = -1;
  rep_start   = -1;
  rep_stop    = -1;
  rep_tankH   = -1;
  rep_force   = -1;
}

// ─── RELAY HELPER ────────────────────────────────────────────────────
static int relayPhysical = -1;

static inline void relayWrite(bool pumpRunning) {
  int level = (RELAY_ACTIVE_HIGH ? pumpRunning : !pumpRunning) ? HIGH : LOW;
  if (level != relayPhysical) {
    digitalWrite(RELAY_PIN, level);
    relayPhysical = level;
  }
}

// ─── MEDIAN FILTER  [v4.6 #2] ────────────────────────────────────────
float   medBuf[MEDIAN_WINDOW];
uint8_t medCount = 0, medHead = 0;

void medianReset() { medCount = 0; medHead = 0; }

void medianPush(float v) {
  medBuf[medHead] = v;
  medHead = (medHead + 1) % MEDIAN_WINDOW;
  if (medCount < MEDIAN_WINDOW) medCount++;
}

float medianGet() {
  float tmp[MEDIAN_WINDOW];
  for (uint8_t i = 0; i < medCount; i++) tmp[i] = medBuf[i];
  // insertion sort (tiny array)
  for (uint8_t i = 1; i < medCount; i++) {
    float key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > key) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = key;
  }
  return tmp[medCount / 2];
}

float emaCm = -1.0f;   // smoothed distance

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

// ─── SENSOR STATUS HELPERS  [v4.6 #4] ────────────────────────────────
const char *sensorStatusText() {
  switch (sensorState) {
    case SENSOR_STAT_OK:    return "OK";
    case SENSOR_STAT_FAULT: return "Fault";
    case SENSOR_STAT_NONE:  return "No Sensor";
    default:                return "Starting";
  }
}

// forward decls
void applyIndicators();
void rmReport(bool forceLevel);
void setPump(bool on);

void setSensorState(SensorState s) {
  if (s == sensorState) return;
  sensorState = s;
  sensorOK    = (s == SENSOR_STAT_OK);

  // Keep sysState in sync for LED priority, but never override a latched
  // CRITICAL (overtime) error.
  if (s == SENSOR_STAT_OK) {
    if (sysState == SYS_SENSOR_FAULT)
      sysState = pumpOn ? SYS_PUMP_RUNNING : SYS_NORMAL;
  } else if (s == SENSOR_STAT_FAULT || s == SENSOR_STAT_NONE) {
    if (sysState != SYS_CRITICAL_ERROR) sysState = SYS_SENSOR_FAULT;
  }

  Serial.printf("[Sensor] state -> %s\n", sensorStatusText());
  applyIndicators();
  rmReport(false);     // event-driven push so the app updates instantly
}

// ─────────────────────────────────────────────────────────────────────
//  RAINMAKER REPORTING  (change-detection; advance cache only on ESP_OK)
// ─────────────────────────────────────────────────────────────────────
void rmReport(bool forceLevel) {
  if (!waterMonitor || WiFi.status() != WL_CONNECTED) return;

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

  v = (int)sensorState;
  if (v != rep_sstate &&
      waterMonitor->updateAndReportParam(PARAM_SSTATUS, sensorStatusText()) == ESP_OK)
    rep_sstate = v;

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
  // LED1 / LED2 — Mode
  ledAuto.solid(currentMode == AUTO_MODE);
  ledManual.solid(currentMode == MANUAL_MODE);

  // LED3 — WiFi
  if (WiFi.status() == WL_CONNECTED) ledWifi.solid(true);
  else                               ledWifi.blink(500, 500);

  // LED4 — Sensor / safety  [v4.6 #4]
  if (sysState == SYS_CRITICAL_ERROR) {
    ledSensor.blink(60, 65);             // overtime/critical: very fast
  } else if (sensorState == SENSOR_STAT_NONE) {
    ledSensor.blink(120, 120, 2, 800);   // No Sensor: blink-blink … pause
  } else if (sensorState == SENSOR_STAT_FAULT) {
    ledSensor.blink(100, 100);           // Fault: steady fast blink
  } else {
    ledSensor.solid(false);              // OK / Init: off
  }
}

// ─── PUMP CONTROL ────────────────────────────────────────────────────
void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  relayWrite(on);
  if (on) {
    pumpStartMs    = millis();
    aboveStopCount = 0;          // require fresh confirmations before stopping
    sysState       = SYS_PUMP_RUNNING;
  } else {
    totalRuntimeS += (millis() - pumpStartMs) / 1000;
    if (sysState != SYS_CRITICAL_ERROR) sysState = SYS_NORMAL;
  }
  applyIndicators();
  rmReport(false);
  Serial.println(on ? "[Pump] ON" : "[Pump] OFF");
}

// ─── AUTO PUMP DECISION  [v4.6 #2] ───────────────────────────────────
void updateConfirmCounters() {
  if (waterPct < (float)g_startPct) { if (belowStartCount < 250) belowStartCount++; }
  else                                belowStartCount = 0;

  if (waterPct >= (float)g_stopPct) { if (aboveStopCount < 250) aboveStopCount++; }
  else                                aboveStopCount = 0;
}

void autoPumpDecision() {
  if (!(currentMode == AUTO_MODE && sensorOK)) return;

  bool wantOn = pumpOn;

  if (g_forcePump) {
    wantOn = true;
    if (aboveStopCount >= STOP_CONFIRM) {   // reached target -> release override
      Serial.printf("[Force] Auto-stop at %.1f%%\n", waterPct);
      g_forcePump = false;
      wantOn      = false;
    }
  } else {
    if (!pumpOn && belowStartCount >= START_CONFIRM) {
      Serial.printf("[AUTO] ON — %.1f%% < %d%% (confirmed)\n", waterPct, g_startPct);
      wantOn = true;
    } else if (pumpOn && aboveStopCount >= STOP_CONFIRM) {
      Serial.printf("[AUTO] OFF — %.1f%% >= %d%% (confirmed)\n", waterPct, g_stopPct);
      wantOn = false;
    }
  }

  if (wantOn != pumpOn) setPump(wantOn);
}

// ─────────────────────────────────────────────────────────────────────
//  SENSOR READING LAYER
//  sensorReadRawCm() returns a fresh, validated distance in cm, or -1.
// ─────────────────────────────────────────────────────────────────────
#if USE_UART_SENSOR

HardwareSerial SensorSerial(1);   // UART1

uint8_t  uartBuf[4];
uint8_t  uartIdx = 0;
float    uartLastCm = -1.0f;
uint32_t uartLastFrameMs = 0;

// Drain all available bytes and parse 0xFF / DataH / DataL / checksum
// frames (mm). Non-blocking — safe to call every loop iteration.
void pollSensorUart() {
  while (SensorSerial.available()) {
    uint8_t b = SensorSerial.read();
    if (uartIdx == 0) {
      if (b == 0xFF) uartBuf[uartIdx++] = b;     // wait for header
    } else {
      uartBuf[uartIdx++] = b;
      if (uartIdx >= 4) {
        uartIdx = 0;
        uint8_t sum = (uartBuf[0] + uartBuf[1] + uartBuf[2]) & 0xFF;
        if (sum == uartBuf[3]) {
          uint16_t mm = ((uint16_t)uartBuf[1] << 8) | uartBuf[2];
          float cm = mm / 10.0f;
          if (cm >= SENSOR_MIN_CM && cm <= SENSOR_MAX_CM) {
            uartLastCm      = cm;
            uartLastFrameMs = millis();
          }
        }
      }
    }
  }
}

float sensorReadRawCm() {
  pollSensorUart();
  if (uartLastCm > 0 && (millis() - uartLastFrameMs) < UART_FRAME_TIMEOUT_MS)
    return uartLastCm;
  return -1.0f;     // no fresh valid frame
}

#else   // ── legacy HC-SR04 trig/echo ──────────────────────────────────

float sensorReadRawCm() {
  float total = 0; int valid = 0;
  for (int i = 0; i < 3; i++) {
    digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long dur = pulseIn(ECHO_PIN, HIGH, 23200);
    if (dur > 0) {
      float d = dur * 0.01715f;
      if (d >= SENSOR_MIN_CM && d <= SENSOR_MAX_CM) { total += d; valid++; }
    }
    delay(30);
  }
  return (valid == 0) ? -1.0f : total / valid;
}

#endif

// ─────────────────────────────────────────────────────────────────────
//  SENSOR SERVICE — runs every SENSOR_INTERVAL_MS
// ─────────────────────────────────────────────────────────────────────
void serviceSensor() {
  float raw = sensorReadRawCm();

  if (raw < 0) {
    // No fresh/valid reading this cycle.
    if (sensorFails < 250) sensorFails++;

    if (!sensorEverSeen) {
      // Never got a reading since boot.
      if (millis() - g_bootMs > NO_SENSOR_BOOT_MS &&
          sensorState != SENSOR_STAT_NONE)
        setSensorState(SENSOR_STAT_NONE);     // -> "No Sensor"
    } else {
      if (sensorFails >= SENSOR_FAIL_LIMIT && sensorState != SENSOR_STAT_FAULT)
        setSensorState(SENSOR_STAT_FAULT);    // -> "Fault"
      // Safety: can't see the level -> don't keep pumping blind.
      if (sensorState == SENSOR_STAT_FAULT &&
          currentMode == AUTO_MODE && pumpOn)
        setPump(false);
    }
    return;
  }

  // Fresh valid reading -------------------------------------------------
  bool recovering = (sensorState != SENSOR_STAT_OK);
  sensorEverSeen  = true;
  sensorFails     = 0;

  if (recovering) {            // fresh start after fault/none/boot
    medianReset();
    emaCm           = -1.0f;
    belowStartCount = 0;
    aboveStopCount  = 0;
  }

  medianPush(raw);
  float m = medianGet();
  if (emaCm < 0) emaCm = m;
  else           emaCm += EMA_ALPHA * (m - emaCm);

  waterDistCm  = emaCm;
  waterDepthCm = max(0.0f, (float)g_tankHeight - waterDistCm);
  waterPct     = constrain(waterDepthCm / g_tankHeight * 100.0f, 0.0f, 100.0f);

  Serial.printf("[Sensor] raw=%.1f  med=%.1f  smooth=%.1f  level=%.1f%%\n",
                raw, m, emaCm, waterPct);

  setSensorState(SENSOR_STAT_OK);   // clears fault, pushes level+status if changed

  // Decisions run on each confirmed, smoothed sample.
  updateConfirmCounters();
  autoPumpDecision();
}

// ─── RAINMAKER WRITE CALLBACK ─────────────────────────────────────────
void write_callback(Device *device, Param *param,
                    const param_val_t val, void *priv_data,
                    write_ctx_t *ctx)
{
  const char *name = param->getParamName();

  if (strcmp(name, PARAM_START) == 0) {
    g_startPct = constrain(val.val.i, 5, 95);
    param->updateAndReport(value(g_startPct));
    rep_start = g_startPct;
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
    if (!sensorOK) {                 // [v4.6 #4] don't force-run blind
      param->updateAndReport(value(false));
      rep_force = 0;
      Serial.println("[Override] Ignored — sensor not OK");
      return;
    }
    g_forcePump = val.val.b;
    param->updateAndReport(val);
    rep_force = g_forcePump ? 1 : 0;
    if (g_forcePump) {
      Serial.println("[Override] Pump FORCED ON");
      aboveStopCount = 0;
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
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
      Serial.println("[Prov] Credentials received");
      break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("[Prov] FAILED — wrong password or 5GHz?");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WiFi] Connected — IP: %s\n",
                    WiFi.localIP().toString().c_str());
      if (sysState == SYS_WIFI_CONNECTING) sysState = SYS_NORMAL;
      applyIndicators();
      resetReportCache();
      lastRMUpdate    = millis() - (RM_TICK_MS - 1500);
      lastRMHeartbeat = millis();
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WiFi] Disconnected");
      applyIndicators();          // LED3 -> slow blink
      resetReportCache();
      break;

    default: break;
  }
}

// ─── SETUP ───────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Southern Water Monitor v4.6.1 ===");

  g_bootMs = millis();
  loadConfig();

  // Mode LEDs
  ledAuto.begin(LED_AUTO);
  ledManual.begin(LED_MANUAL);
  // WiFi / Sensor LEDs
  ledWifi.begin(LED_WIFI, NEW_LEDS_ACTIVE_HIGH);
  ledSensor.begin(LED_SENSOR, NEW_LEDS_ACTIVE_HIGH);

  // Sensor interface  [v4.6 #1]
#if USE_UART_SENSOR
  SensorSerial.begin(UART_BAUD, SERIAL_8N1, SENSOR_UART_RX, SENSOR_UART_TX);
  Serial.printf("[Sensor] UART @ %d  RX=GPIO%d  TX=GPIO%d\n",
                UART_BAUD, SENSOR_UART_RX, SENSOR_UART_TX);
#else
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.println("[Sensor] HC-SR04 trig/echo");
#endif

  // Relay + mode switch
  pinMode(RELAY_PIN, OUTPUT);
  relayWrite(false);                 // pump OFF at boot
  pinMode(DPDT_PIN, INPUT_PULLDOWN);

  lastDPDT    = digitalRead(DPDT_PIN);
  currentMode = (lastDPDT == HIGH) ? MANUAL_MODE : AUTO_MODE;
  Serial.printf("[Boot] Mode: %s\n",
                currentMode == AUTO_MODE ? "AUTO" : "MANUAL");

  sysState    = SYS_WIFI_CONNECTING;
  sensorState = SENSOR_STAT_INIT;
  applyIndicators();

  // ── RainMaker setup ───────────────────────────────────────────
  Node node = RMaker.initNode("Alif's Home Water Circuit");
  waterMonitor = new Device("Alif's Water Node", "esp.device.water-sensor", NULL);

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

  // [v4.6 #4] Human-readable sensor status (read-only text label).
  Param sStatusParam(PARAM_SSTATUS, "esp.param.name",
                     value("Starting"), PROP_FLAG_READ);
  waterMonitor->addParam(sStatusParam);

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

  // Non-blocking drivers
  ledAuto.update();
  ledManual.update();
  ledWifi.update();
  ledSensor.update();

#if USE_UART_SENSOR
  pollSensorUart();        // keep draining the UART every iteration
#endif

  // ── WiFi state watchdog ──────────────────────────────────────
  static bool     lastWifiState = false;
  static uint32_t lastWifiRetry = 0;
  bool wifiNow = (WiFi.status() == WL_CONNECTED);

  if (wifiNow != lastWifiState) {
    lastWifiState = wifiNow;
    applyIndicators();
    if (!wifiNow) resetReportCache();
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
      Serial.printf("[Mode] -> %s\n",
                    currentMode == AUTO_MODE ? "AUTO" : "MANUAL");

      if (currentMode == MANUAL_MODE) {
        if (pumpOn) {
          pumpOn = false;
          relayWrite(false);
          totalRuntimeS += (millis() - pumpStartMs) / 1000;
          if (sysState != SYS_CRITICAL_ERROR) sysState = SYS_NORMAL;
        }
        if (g_forcePump) {
          g_forcePump = false;
          Serial.println("[Override] Cleared — MANUAL");
        }
      }
      applyIndicators();
      rmReport(false);
    }
  }

  // ── Sensor service ───────────────────────────────────────────
  if (millis() - lastSensorMs > SENSOR_INTERVAL_MS) {
    lastSensorMs = millis();
    serviceSensor();          // read, filter, update status, run AUTO logic
  }

  // ── Overtime hard cutoff (time-based, independent of samples) ──
  if (currentMode == AUTO_MODE && pumpOn &&
      (millis() - pumpStartMs) > PUMP_OVERTIME_MS) {
    Serial.println("[Safety] OVERTIME — pump killed");
    g_forcePump = false;
    setPump(false);
    sysState = SYS_CRITICAL_ERROR;
    applyIndicators();
  }

  // ── RainMaker change-detection report ────────────────────────
  if (millis() - lastRMUpdate > RM_TICK_MS) {
    lastRMUpdate = millis();
    bool heartbeat = (millis() - lastRMHeartbeat > RM_HEARTBEAT_MS);
    if (heartbeat) lastRMHeartbeat = millis();
    rmReport(heartbeat);
  }
}
