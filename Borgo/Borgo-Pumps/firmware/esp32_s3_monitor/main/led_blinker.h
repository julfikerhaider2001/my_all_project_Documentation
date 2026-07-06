#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

typedef struct {
    gpio_num_t pin;
    bool       active_high;
    uint32_t   on_ms;
    uint32_t   off_ms;
    uint32_t   pause_ms;
    int        repeats;
    int        count;
    bool       state;
    bool       in_pause;
    int64_t    last_change_us;
} led_blinker_t;

void led_blinker_init(led_blinker_t *b, gpio_num_t pin, bool active_high);
void led_blinker_solid(led_blinker_t *b, bool on);
void led_blinker_blink(led_blinker_t *b, uint32_t on_ms, uint32_t off_ms,
                       int repeats, uint32_t pause_ms);
void led_blinker_update(led_blinker_t *b);
