#pragma once

#include <stdint.h>
#include <stdbool.h>

#define RELAY_ACTIVE_HIGH false

void pump_init(void);
void pump_relay_write(bool running);
void pump_set(bool on);
bool pump_is_on(void);
void pump_add_runtime(uint32_t seconds);
uint32_t pump_get_total_runtime(void);
uint32_t pump_get_start_tick(void);
