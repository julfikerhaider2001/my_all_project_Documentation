// config/swm_config.h
// Shared configuration and constants for Southern Water Monitor

#ifndef SWM_CONFIG_H
#define SWM_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ──────────────────────────────────────────────────────────────
// SENSOR CONFIGURATION
// ──────────────────────────────────────────────────────────────
#define SENSOR_ENABLED          false   // Set true when HC-SR04 installed with voltage divider

// ──────────────────────────────────────────────────────────────
// PIN MAPPING
// ──────────────────────────────────────────────────────────────
#define PIN_LED_AUTO            10
#define PIN_LED_MANUAL          11
#define PIN_LED_WIFI            8
#define PIN_LED_SENSOR          7
#define PIN_BUZZER              38
#define PIN_TRIG                5       // HC-SR04 trigger
#define PIN_ECHO                6       // HC-SR04 echo (requires voltage divider 5V→3.3V)
#define PIN_RELAY               4
#define PIN_DPDT                1

// ──────────────────────────────────────────────────────────────
// POLARITY FLAGS
// ──────────────────────────────────────────────────────────────
#define LED_ACTIVE_HIGH         true    // LEDs light when GPIO=HIGH
#define RELAY_ACTIVE_HIGH       false   // Relay ON when GPIO=LOW (invert for your relay)

// ──────────────────────────────────────────────────────────────
// TANK & PUMP CONFIGURATION
// ──────────────────────────────────────────────────────────────
#define DEFAULT_TANK_HEIGHT     80      // cm
#define DEFAULT_START_PCT       25      // Start pump when tank below this %
#define DEFAULT_STOP_PCT        70      // Stop pump when tank above this %
#define PUMP_OVERTIME_MS        1800000 // 30 minutes hard cutoff

// ──────────────────────────────────────────────────────────────
// TASK PRIORITIES (FreeRTOS)
// Note: Higher number = higher priority. Default is 1. Max is configMAX_PRIORITIES
// ──────────────────────────────────────────────────────────────
#define TASK_PRIO_WIFI          (tskIDLE_PRIORITY + 3)
#define TASK_PRIO_RAINMAKER     (tskIDLE_PRIORITY + 2)
#define TASK_PRIO_SENSOR        (tskIDLE_PRIORITY + 4)   // High priority - time-critical
#define TASK_PRIO_PUMP          (tskIDLE_PRIORITY + 3)
#define TASK_PRIO_INDICATOR     (tskIDLE_PRIORITY + 1)
#define TASK_PRIO_DPDT          (tskIDLE_PRIORITY + 2)

// ──────────────────────────────────────────────────────────────
// TASK STACK SIZES (bytes)
// ──────────────────────────────────────────────────────────────
#define TASK_STACK_WIFI         4096
#define TASK_STACK_RAINMAKER    4096
#define TASK_STACK_SENSOR       2048
#define TASK_STACK_PUMP         2048
#define TASK_STACK_INDICATOR    2048
#define TASK_STACK_DPDT         1024

// ──────────────────────────────────────────────────────────────
// TIMING CONSTANTS
// ──────────────────────────────────────────────────────────────
#define SENSOR_READ_INTERVAL_MS 2000    // Read ultrasonic every 2 seconds
#define DPDT_DEBOUNCE_MS        200     // Debounce mode switch
#define LED_TICK_MS             50      // LED blink tick rate
#define RM_REPORT_INTERVAL_MS   5000    // RainMaker report check
#define RM_HEARTBEAT_MS         5000    // Force heartbeat every 5 seconds (was 60)

// ──────────────────────────────────────────────────────────────
// RAINMAKER CONFIGURATION
// ──────────────────────────────────────────────────────────────
#define RMAKER_SERVICE_NAME     "Alif's Water Monitor"
#define RMAKER_POP              "southern123"
#define RMAKER_NODE_NAME        "Alif's Home Water Circuit"

// ──────────────────────────────────────────────────────────────
// RAINMAKER PARAMETER NAMES
// ──────────────────────────────────────────────────────────────
#define PARAM_WATER_LEVEL       "Water Level"
#define PARAM_PUMP_RUNNING      "Pump Running"
#define PARAM_MODE              "Mode"
#define PARAM_SENSOR_OK         "Sensor OK"
#define PARAM_START_PCT         "Start At %"
#define PARAM_STOP_PCT          "Stop At %"
#define PARAM_TANK_HEIGHT       "Tank Height cm"
#define PARAM_FORCE_PUMP        "Force Pump"

// ──────────────────────────────────────────────────────────────
// NVS FLASH KEYS
// ──────────────────────────────────────────────────────────────
#define NVS_NAMESPACE           "swm_cfg"
#define NVS_KEY_TANK_H          "tankH"
#define NVS_KEY_START_PCT       "startPct"
#define NVS_KEY_STOP_PCT        "stopPct"

// ──────────────────────────────────────────────────────────────
// SYSTEM STATE ENUM
// ──────────────────────────────────────────────────────────────
typedef enum {
    SYS_STATE_WIFI_CONNECTING,
    SYS_STATE_NORMAL,
    SYS_STATE_PUMP_RUNNING,
    SYS_STATE_SENSOR_FAULT,
    SYS_STATE_CRITICAL_ERROR,
    SYS_STATE_PROVISIONING
} swm_sys_state_t;

// ──────────────────────────────────────────────────────────────
// MODE ENUM
// ──────────────────────────────────────────────────────────────
typedef enum {
    MODE_AUTO,
    MODE_MANUAL
} swm_mode_t;

// ──────────────────────────────────────────────────────────────
// SYSTEM STATE STRUCTURE
// Global state accessible across tasks via mutex
// ──────────────────────────────────────────────────────────────
typedef struct {
    // WiFi & Cloud
    bool wifi_connected;
    bool rainmaker_claimed;
    bool rainmaker_ready;
    
    // Sensor
    bool sensor_ok;
    float water_level_pct;
    float water_depth_cm;
    float water_dist_cm;
    
    // Pump
    bool pump_running;
    uint32_t pump_runtime_s;
    
    // Configuration
    int tank_height_cm;
    int start_pct;
    int stop_pct;
    swm_mode_t current_mode;
    bool force_pump;
    
    // System
    swm_sys_state_t sys_state;
    uint32_t uptime_s;
} swm_state_t;

// Global state (protected by mutex in ipc/swm_ipc.c)
extern swm_state_t g_swm_state;

// Function to initialize and load config from NVS
void swm_config_init(void);

// Function to save config to NVS
void swm_config_save_tank_height(int height_cm);
void swm_config_save_start_pct(int pct);
void swm_config_save_stop_pct(int pct);

#endif // SWM_CONFIG_H
