#pragma once

#include "esp_rmaker_core.h"
#include "sensor.h"

#define PARAM_LEVEL    "Water Level"
#define PARAM_PUMP     "Pump Running"
#define PARAM_MODE     "Mode"
#define PARAM_SENSOR   "Sensor OK"
#define PARAM_SSTATUS  "Sensor Status"
#define PARAM_START    "Start At %"
#define PARAM_STOP     "Stop At %"
#define PARAM_TANKH    "Tank Height cm"
#define PARAM_FORCE    "Force Pump"

#define RM_TICK_MS          5000UL
#define RM_HEARTBEAT_MS     60000UL
#define LEVEL_REPORT_DELTA  1.0f

void rmaker_init(int tank_height, int start_pct, int stop_pct);
void rmaker_start(void);
void rmaker_report(bool force_level, float water_pct, bool pump_on,
                   bool auto_mode, sensor_state_t sensor_state,
                   int start_pct, int stop_pct, int tank_height, bool force_pump);
void rmaker_write_callback_set(
    void (*cb)(const char *name, esp_rmaker_param_val_t val));
