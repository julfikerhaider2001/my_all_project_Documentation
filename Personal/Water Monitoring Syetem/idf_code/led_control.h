// drivers/led_control.h
// LED indicator driver

#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

// ──────────────────────────────────────────────────────────────
// LED TYPES
// ──────────────────────────────────────────────────────────────
typedef enum {
    LED_AUTO = 0,
    LED_MANUAL = 1,
    LED_WIFI = 2,
    LED_SENSOR = 3
} led_t;

// ──────────────────────────────────────────────────────────────
// BLINK MODES
// ──────────────────────────────────────────────────────────────
typedef enum {
    BLINK_SOLID_OFF,
    BLINK_SOLID_ON,
    BLINK_SLOW,         // 500ms on, 500ms off
    BLINK_FAST,         // 100ms on, 100ms off
    BLINK_VERY_FAST     // 60ms on, 65ms off
} blink_mode_t;

// ──────────────────────────────────────────────────────────────
// LED INITIALIZATION
// ──────────────────────────────────────────────────────────────
void led_init(void);

// ──────────────────────────────────────────────────────────────
// LED CONTROL
// ──────────────────────────────────────────────────────────────
void led_set(led_t led, blink_mode_t mode);

// LED tick - call regularly from indicator task (e.g., every 50ms)
void led_tick(void);

// ──────────────────────────────────────────────────────────────
// BUZZER CONTROL
// ──────────────────────────────────────────────────────────────
void buzzer_init(void);
void buzzer_beep(uint32_t on_ms, uint32_t off_ms, int count);
void buzzer_tick(void);

#endif // LED_CONTROL_H
