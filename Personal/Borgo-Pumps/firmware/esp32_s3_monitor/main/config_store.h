#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NVS_NAMESPACE       "swm_cfg"
#define NVS_KEY_TANK_H      "tankH"
#define NVS_KEY_START_PCT   "startPct"
#define NVS_KEY_STOP_PCT    "stopPct"

#define DEFAULT_TANK_HEIGHT  80
#define DEFAULT_START_PCT    25
#define DEFAULT_STOP_PCT     70

void     config_store_init(void);
int      config_store_get_tank_height(void);
int      config_store_get_start_pct(void);
int      config_store_get_stop_pct(void);
void     config_store_set_tank_height(int val);
void     config_store_set_start_pct(int val);
void     config_store_set_stop_pct(int val);
