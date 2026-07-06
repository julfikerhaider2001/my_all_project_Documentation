// drivers/led_control.c
// LED and Buzzer driver implementation

#include "led_control.h"
#include "swm_config.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "LED";

// ──────────────────────────────────────────────────────────────
// LED STATE STRUCTURE
// ──────────────────────────────────────────────────────────────
typedef struct {
    uint8_t pin;
    bool active_high;
    blink_mode_t mode;
    bool state;
    uint32_t last_change_ms;
    uint32_t on_ms, off_ms;
} led_state_t;

static led_state_t leds[4] = {
    {PIN_LED_AUTO, LED_ACTIVE_HIGH, BLINK_SOLID_OFF, false, 0, 0, 0},
    {PIN_LED_MANUAL, LED_ACTIVE_HIGH, BLINK_SOLID_OFF, false, 0, 0, 0},
    {PIN_LED_WIFI, LED_ACTIVE_HIGH, BLINK_SOLID_OFF, false, 0, 0, 0},
    {PIN_LED_SENSOR, LED_ACTIVE_HIGH, BLINK_SOLID_OFF, false, 0, 0, 0}
};

// Buzzer state
typedef struct {
    uint8_t pin;
    bool active;
    bool state;
    int beeps_remaining;
    uint32_t on_ms, off_ms;
    uint32_t last_change_ms;
} buzzer_state_t;

static buzzer_state_t buzzer = {
    .pin = PIN_BUZZER,
    .active = false,
    .state = false,
    .beeps_remaining = 0,
    .on_ms = 0,
    .off_ms = 0,
    .last_change_ms = 0
};

// ──────────────────────────────────────────────────────────────
// HELPER: Set GPIO based on active_high polarity
// ──────────────────────────────────────────────────────────────
static void _led_write(led_state_t *led, bool on) {
    bool gpio_level = led->active_high ? on : !on;
    gpio_set_level(led->pin, gpio_level ? 1 : 0);
}

// ──────────────────────────────────────────────────────────────
// LED INITIALIZATION
// ──────────────────────────────────────────────────────────────
void led_init(void) {
    ESP_LOGI(TAG, "Initializing LEDs");
    
    uint64_t pin_mask = (1ULL << PIN_LED_AUTO) | (1ULL << PIN_LED_MANUAL) | 
                        (1ULL << PIN_LED_WIFI) | (1ULL << PIN_LED_SENSOR);
    
    gpio_config_t cfg = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    gpio_config(&cfg);
    
    // Turn all LEDs off
    for (int i = 0; i < 4; i++) {
        _led_write(&leds[i], false);
    }
    
    ESP_LOGI(TAG, "LEDs initialized (AUTO=%d, MANUAL=%d, WIFI=%d, SENSOR=%d)", 
             PIN_LED_AUTO, PIN_LED_MANUAL, PIN_LED_WIFI, PIN_LED_SENSOR);
}

// ──────────────────────────────────────────────────────────────
// LED CONTROL
// ──────────────────────────────────────────────────────────────
void led_set(led_t led_idx, blink_mode_t mode) {
    if (led_idx >= 4) return;
    
    led_state_t *led = &leds[led_idx];
    
    // Update mode
    led->mode = mode;
    
    // Set timing and initial state based on mode
    switch (mode) {
        case BLINK_SOLID_OFF:
            _led_write(led, false);
            led->state = false;
            break;
            
        case BLINK_SOLID_ON:
            _led_write(led, true);
            led->state = true;
            break;
            
        case BLINK_SLOW:
            led->on_ms = 500;
            led->off_ms = 500;
            led->state = true;
            _led_write(led, true);
            led->last_change_ms = 0;  // Force update on next tick
            break;
            
        case BLINK_FAST:
            led->on_ms = 100;
            led->off_ms = 100;
            led->state = true;
            _led_write(led, true);
            led->last_change_ms = 0;
            break;
            
        case BLINK_VERY_FAST:
            led->on_ms = 60;
            led->off_ms = 65;
            led->state = true;
            _led_write(led, true);
            led->last_change_ms = 0;
            break;
    }
}

// ──────────────────────────────────────────────────────────────
// LED TICK - Call regularly (e.g., every 50ms)
// ──────────────────────────────────────────────────────────────
void led_tick(void) {
    uint32_t now = xTaskGetTickCount();
    
    for (int i = 0; i < 4; i++) {
        led_state_t *led = &leds[i];
        
        // Skip solid modes
        if (led->mode == BLINK_SOLID_OFF || led->mode == BLINK_SOLID_ON) {
            continue;
        }
        
        uint32_t elapsed = (now - led->last_change_ms) * portTICK_PERIOD_MS;
        uint32_t threshold = led->state ? led->on_ms : led->off_ms;
        
        if (elapsed >= threshold) {
            led->state = !led->state;
            _led_write(led, led->state);
            led->last_change_ms = now;
        }
    }
}

// ──────────────────────────────────────────────────────────────
// BUZZER INITIALIZATION
// ──────────────────────────────────────────────────────────────
void buzzer_init(void) {
    ESP_LOGI(TAG, "Initializing buzzer (GPIO%d)", PIN_BUZZER);
    
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    gpio_config(&cfg);
    gpio_set_level(PIN_BUZZER, 0);
}

// ──────────────────────────────────────────────────────────────
// BUZZER CONTROL
// ──────────────────────────────────────────────────────────────
void buzzer_beep(uint32_t on_ms, uint32_t off_ms, int count) {
    if (count <= 0) return;
    
    buzzer.on_ms = on_ms;
    buzzer.off_ms = off_ms;
    buzzer.beeps_remaining = count;
    buzzer.state = true;
    buzzer.active = true;
    buzzer.last_change_ms = xTaskGetTickCount();
    
    gpio_set_level(PIN_BUZZER, 1);  // Start beeping
}

// ──────────────────────────────────────────────────────────────
// BUZZER TICK - Call regularly (e.g., every 50ms)
// ──────────────────────────────────────────────────────────────
void buzzer_tick(void) {
    if (!buzzer.active) return;
    
    uint32_t now = xTaskGetTickCount();
    uint32_t elapsed = (now - buzzer.last_change_ms) * portTICK_PERIOD_MS;
    uint32_t threshold = buzzer.state ? buzzer.on_ms : buzzer.off_ms;
    
    if (elapsed >= threshold) {
        if (buzzer.state) {
            // Just finished ON period
            buzzer.state = false;
            gpio_set_level(PIN_BUZZER, 0);
        } else {
            // Just finished OFF period
            buzzer.beeps_remaining--;
            if (buzzer.beeps_remaining > 0) {
                buzzer.state = true;
                gpio_set_level(PIN_BUZZER, 1);
            } else {
                buzzer.active = false;
            }
        }
        buzzer.last_change_ms = now;
    }
}
