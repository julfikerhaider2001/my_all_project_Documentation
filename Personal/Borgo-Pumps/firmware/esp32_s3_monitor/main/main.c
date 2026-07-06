/* ================================================================
 *  Southern Water Monitor — ESP-IDF RTOS Firmware
 *  Board:    ESP32 DevKit V1
 *  Target:   ESP-IDF v5.x with FreeRTOS
 * ================================================================ */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "protocomm.h"

#include "config_store.h"
#include "sensor.h"
#include "led_blinker.h"
#include "pump.h"
#include "rmaker_wrapper.h"

static const char *TAG = "main";

/* ─── Pin map (ESP32 DevKit V1) ─────────────────────────────────────── */
#define LED_AUTO     16
#define LED_MANUAL   17
#define LED_WIFI     18
#define LED_SENSOR   19
#define DPDT_PIN     13

/* ─── Enums ────────────────────────────────────────────────────────── */
typedef enum {
    SYS_WIFI_CONNECTING,
    SYS_NORMAL,
    SYS_PUMP_RUNNING,
    SYS_SENSOR_FAULT,
    SYS_CRITICAL_ERROR,
} sys_state_t;

typedef enum {
    AUTO_MODE,
    MANUAL_MODE,
} mode_t;

/* ─── Safety ───────────────────────────────────────────────────────── */
#define PUMP_OVERTIME_MS  1800000UL   /* 30 min hard cutoff */

/* ─── LED active-high wiring flag ──────────────────────────────────── */
#define NEW_LEDS_ACTIVE_HIGH  true

/* ─── WiFi provisioning ────────────────────────────────────────────── */
#define PROV_SERVICE_NAME  "Water Monitor"
#define PROV_POP           "southern123"

/* ─── Globals ──────────────────────────────────────────────────────── */
static led_blinker_t s_led_auto, s_led_manual, s_led_wifi, s_led_sensor;
static sensor_data_t s_sensor_data;

static mode_t      s_current_mode = AUTO_MODE;
static bool        s_last_dpdt    = false;
static sys_state_t s_sys_state    = SYS_WIFI_CONNECTING;
static bool        s_force_pump   = false;
static int         s_tank_height;
static int         s_start_pct;
static int         s_stop_pct;

static int64_t  s_last_rm_update_us   = 0;
static int64_t  s_last_rm_heartbeat_us = 0;
static int64_t  s_last_mode_check_us  = 0;
static int64_t  s_last_wifi_retry_us  = 0;
static bool     s_wifi_connected      = false;

/* ─── Forward declarations ─────────────────────────────────────────── */
static void apply_indicators(void);
static void auto_pump_decision(void);
static void wifi_prov_start(void);

/* ───────────────────────────────────────────────────────────────────── */
/*  LED helpers                                                         */
/* ───────────────────────────────────────────────────────────────────── */
static void led_update_all(void)
{
    led_blinker_update(&s_led_auto);
    led_blinker_update(&s_led_manual);
    led_blinker_update(&s_led_wifi);
    led_blinker_update(&s_led_sensor);
}

static void apply_indicators(void)
{
    led_blinker_solid(&s_led_auto,  s_current_mode == AUTO_MODE);
    led_blinker_solid(&s_led_manual, s_current_mode == MANUAL_MODE);

    if (s_wifi_connected) {
        led_blinker_solid(&s_led_wifi, true);
    } else {
        led_blinker_blink(&s_led_wifi, 500, 500, -1, 0);
    }

    if (s_sys_state == SYS_CRITICAL_ERROR) {
        led_blinker_blink(&s_led_sensor, 60, 65, -1, 0);
    } else if (s_sensor_data.state == SENSOR_STAT_NONE) {
        led_blinker_blink(&s_led_sensor, 120, 120, 2, 800);
    } else if (s_sensor_data.state == SENSOR_STAT_FAULT) {
        led_blinker_blink(&s_led_sensor, 100, 100, -1, 0);
    } else {
        led_blinker_solid(&s_led_sensor, false);
    }
}

/* ───────────────────────────────────────────────────────────────────── */
/*  Pump decision logic                                                 */
/* ───────────────────────────────────────────────────────────────────── */
static void update_pump_state(bool on)
{
    if (on == pump_is_on()) return;

    if (on) {
        s_sys_state = SYS_PUMP_RUNNING;
        sensor_reset_above_stop();
    } else {
        if (s_sys_state != SYS_CRITICAL_ERROR) s_sys_state = SYS_NORMAL;
    }

    pump_set(on);
    apply_indicators();
}

static void auto_pump_decision(void)
{
    if (!(s_current_mode == AUTO_MODE && s_sensor_data.sensor_ok)) return;

    bool want_on = pump_is_on();

    if (s_force_pump) {
        want_on = true;
        if (sensor_get_above_stop_count() >= STOP_CONFIRM) {
            ESP_LOGI(TAG, "[Force] Auto-stop at %.1f%%", s_sensor_data.water_pct);
            s_force_pump = false;
            want_on = false;
        }
    } else {
        if (!pump_is_on() && sensor_get_below_start_count() >= START_CONFIRM) {
            ESP_LOGI(TAG, "[AUTO] ON - %.1f%% < %d%% (confirmed)",
                     s_sensor_data.water_pct, s_start_pct);
            want_on = true;
        } else if (pump_is_on() && sensor_get_above_stop_count() >= STOP_CONFIRM) {
            ESP_LOGI(TAG, "[AUTO] OFF - %.1f%% >= %d%% (confirmed)",
                     s_sensor_data.water_pct, s_stop_pct);
            want_on = false;
        }
    }

    if (want_on != pump_is_on()) update_pump_state(want_on);
}

/* ───────────────────────────────────────────────────────────────────── */
/*  Sensor task                                                         */
/* ───────────────────────────────────────────────────────────────────── */
static void sensor_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));

        sensor_read_and_update(&s_sensor_data);

        if (s_sensor_data.state == SENSOR_STAT_FAULT &&
            s_current_mode == AUTO_MODE && pump_is_on()) {
            ESP_LOGW(TAG, "Sensor fault - killing pump");
            update_pump_state(false);
        }

        if (s_sensor_data.sensor_ok) {
            if (s_sys_state == SYS_SENSOR_FAULT) {
                s_sys_state = pump_is_on() ? SYS_PUMP_RUNNING : SYS_NORMAL;
            }
        } else {
            if (s_sys_state != SYS_CRITICAL_ERROR) {
                s_sys_state = SYS_SENSOR_FAULT;
            }
        }

        auto_pump_decision();
    }
}

/* ───────────────────────────────────────────────────────────────────── */
/*  LED task                                                            */
/* ───────────────────────────────────────────────────────────────────── */
static void led_task(void *arg)
{
    ESP_LOGI(TAG, "LED task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        led_update_all();
    }
}

/* ───────────────────────────────────────────────────────────────────── */
/*  WiFi event handler                                                  */
/* ───────────────────────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                s_wifi_connected = false;
                ESP_LOGW(TAG, "[WiFi] Disconnected");
                apply_indicators();
                break;
            default:
                break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "[WiFi] Connected - IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        s_sys_state = SYS_NORMAL;
        apply_indicators();
    }
}

/* ───────────────────────────────────────────────────────────────────── */
/*  WiFi Provisioning event handler                                     */
/* ───────────────────────────────────────────────────────────────────── */
static void prov_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    switch (id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "[Prov] BLE started - open RainMaker app");
            break;
        case WIFI_PROV_CRED_RECV: {
            ESP_LOGI(TAG, "[Prov] Credentials received");
            wifi_config_t *wifi_cfg = (wifi_config_t *)data;
            esp_wifi_set_config(WIFI_IF_STA, wifi_cfg);
            break;
        }
        case WIFI_PROV_CRED_FAIL:
            ESP_LOGE(TAG, "[Prov] FAILED - wrong password or 5GHz?");
            break;
        case WIFI_PROV_END:
            ESP_LOGI(TAG, "[Prov] Provisioning complete");
            wifi_prov_mgr_deinit();
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_start();
            break;
        default:
            break;
    }
}

/* ───────────────────────────────────────────────────────────────────── */
/*  WiFi provisioning setup                                             */
/* ───────────────────────────────────────────────────────────────────── */
static void wifi_prov_start(void)
{
    ESP_LOGI(TAG, "Starting WiFi provisioning");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_prov_mgr_config_t prov_cfg = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_cfg));

    esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL);

    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);

    if (!provisioned) {
        ESP_LOGI(TAG, "[Prov] Not provisioned - starting BLE provisioning");
        wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, PROV_POP, PROV_SERVICE_NAME, NULL);
    } else {
        ESP_LOGI(TAG, "[Prov] Already provisioned - connecting to WiFi");
        wifi_prov_mgr_deinit();
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    }
}

/* ───────────────────────────────────────────────────────────────────── */
/*  RainMaker write callback                                            */
/* ───────────────────────────────────────────────────────────────────── */
static void rmaker_write_cb(const char *name, esp_rmaker_param_val_t val)
{
    if (strcmp(name, PARAM_START) == 0) {
        int v = val.val.i;
        if (v < 5) v = 5;
        if (v > 95) v = 95;
        s_start_pct = v;
        config_store_set_start_pct(v);
        ESP_LOGI(TAG, "[Config] Start=%d%%", v);
    }
    else if (strcmp(name, PARAM_STOP) == 0) {
        int v = val.val.i;
        if (v < s_start_pct + 5) v = s_start_pct + 5;
        if (v > 100) v = 100;
        s_stop_pct = v;
        config_store_set_stop_pct(v);
        ESP_LOGI(TAG, "[Config] Stop=%d%%", v);
    }
    else if (strcmp(name, PARAM_TANKH) == 0) {
        int v = val.val.i;
        if (v < 20) v = 20;
        if (v > 500) v = 500;
        s_tank_height = v;
        s_sensor_data.tank_height = v;
        config_store_set_tank_height(v);
        ESP_LOGI(TAG, "[Config] TankH=%dcm", v);
    }
    else if (strcmp(name, PARAM_FORCE) == 0) {
        if (s_current_mode == MANUAL_MODE) {
            ESP_LOGW(TAG, "[Override] Ignored - MANUAL mode");
            return;
        }
        if (!s_sensor_data.sensor_ok) {
            ESP_LOGW(TAG, "[Override] Ignored - sensor not OK");
            return;
        }
        s_force_pump = val.val.b;
        if (s_force_pump) {
            ESP_LOGI(TAG, "[Override] Pump FORCED ON");
            update_pump_state(true);
        } else {
            ESP_LOGI(TAG, "[Override] Pump FORCED OFF");
            update_pump_state(false);
        }
    }
}

/* ───────────────────────────────────────────────────────────────────── */
/*  Main entry point                                                    */
/* ───────────────────────────────────────────────────────────────────── */
void app_main(void)
{
    /* ── NVS ─────────────────────────────────────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── Network stack ───────────────────────────────────────────── */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ── Load config ─────────────────────────────────────────────── */
    config_store_init();
    s_tank_height = config_store_get_tank_height();
    s_start_pct   = config_store_get_start_pct();
    s_stop_pct    = config_store_get_stop_pct();
    ESP_LOGI(TAG, "=== Southern Water Monitor v4.6.1 (ESP-IDF RTOS) ===");
    ESP_LOGI(TAG, "Config: Tank=%dcm  Start=%d%%  Stop=%d%%", s_tank_height, s_start_pct, s_stop_pct);

    /* ── LEDs ────────────────────────────────────────────────────── */
    led_blinker_init(&s_led_auto,   LED_AUTO,   false);
    led_blinker_init(&s_led_manual, LED_MANUAL, false);
    led_blinker_init(&s_led_wifi,   LED_WIFI,   NEW_LEDS_ACTIVE_HIGH);
    led_blinker_init(&s_led_sensor, LED_SENSOR, NEW_LEDS_ACTIVE_HIGH);

    /* ── Pump relay ──────────────────────────────────────────────── */
    pump_init();

    /* ── Mode switch (DPDT pin) ──────────────────────────────────── */
    gpio_config_t dpdt_cfg = {
        .pin_bit_mask = (1ULL << DPDT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&dpdt_cfg);
    s_last_dpdt    = gpio_get_level(DPDT_PIN);
    s_current_mode = (s_last_dpdt == 1) ? MANUAL_MODE : AUTO_MODE;
    ESP_LOGI(TAG, "Mode: %s", s_current_mode == AUTO_MODE ? "AUTO" : "MANUAL");

    /* ── Sensor init ─────────────────────────────────────────────── */
    sensor_init(&s_sensor_data);
    s_sensor_data.tank_height = s_tank_height;
    s_sensor_data.start_pct   = s_start_pct;
    s_sensor_data.stop_pct    = s_stop_pct;

    /* ── RainMaker init ──────────────────────────────────────────── */
    rmaker_init(s_tank_height, s_start_pct, s_stop_pct);
    rmaker_write_callback_set(rmaker_write_cb);

    s_sys_state = SYS_WIFI_CONNECTING;
    apply_indicators();

    /* ── WiFi + RainMaker start ──────────────────────────────────── */
    rmaker_start();
    wifi_prov_start();

    /* ── Create tasks ────────────────────────────────────────────── */
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(led_task,     "led_task",    2048, NULL, 3, NULL, 0);

    s_last_rm_update_us   = esp_timer_get_time() - (RM_TICK_MS * 1000 - 1500000);
    s_last_rm_heartbeat_us = esp_timer_get_time();

    /* ── Main loop ───────────────────────────────────────────────── */
    ESP_LOGI(TAG, "Entering main loop");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));

        int64_t now_us  = esp_timer_get_time();
        uint32_t now_ms = (uint32_t)(now_us / 1000);

        /* Mode switch polling (200ms) */
        if ((now_us - s_last_mode_check_us) > 200000) {
            s_last_mode_check_us = now_us;
            bool dpdt = gpio_get_level(DPDT_PIN);
            if (dpdt != s_last_dpdt) {
                s_last_dpdt    = dpdt;
                s_current_mode = (dpdt == 1) ? MANUAL_MODE : AUTO_MODE;
                ESP_LOGI(TAG, "[Mode] -> %s", s_current_mode == AUTO_MODE ? "AUTO" : "MANUAL");

                if (s_current_mode == MANUAL_MODE) {
                    if (pump_is_on()) {
                        pump_set(false);
                        if (s_sys_state != SYS_CRITICAL_ERROR) s_sys_state = SYS_NORMAL;
                    }
                    if (s_force_pump) {
                        s_force_pump = false;
                        ESP_LOGI(TAG, "[Override] Cleared - MANUAL");
                    }
                }
                apply_indicators();
            }
        }

        /* Overtime safety cutoff (30 min) */
        if (s_current_mode == AUTO_MODE && pump_is_on()) {
            uint32_t pump_ms = now_ms - pump_get_start_tick();
            if (pump_ms > PUMP_OVERTIME_MS) {
                ESP_LOGE(TAG, "[Safety] OVERTIME - pump killed");
                s_force_pump = false;
                update_pump_state(false);
                s_sys_state = SYS_CRITICAL_ERROR;
                apply_indicators();
            }
        }

        /* WiFi reconnect watchdog (30s) */
        if (!s_wifi_connected && (now_us - s_last_wifi_retry_us) > 30000000) {
            s_last_wifi_retry_us = now_us;
            ESP_LOGI(TAG, "[WiFi] Retrying...");
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_wifi_connect();
        }

        /* RainMaker periodic report */
        if (s_wifi_connected &&
            (now_us - s_last_rm_update_us) > (RM_TICK_MS * 1000)) {
            s_last_rm_update_us = now_us;
            bool heartbeat = ((now_us - s_last_rm_heartbeat_us) > (RM_HEARTBEAT_MS * 1000));
            if (heartbeat) s_last_rm_heartbeat_us = now_us;

            s_sensor_data.tank_height = s_tank_height;
            s_sensor_data.start_pct   = s_start_pct;
            s_sensor_data.stop_pct    = s_stop_pct;

            rmaker_report(heartbeat, s_sensor_data.water_pct, pump_is_on(),
                          s_current_mode == AUTO_MODE, s_sensor_data.state,
                          s_start_pct, s_stop_pct, s_tank_height, s_force_pump);
        }
    }
}
