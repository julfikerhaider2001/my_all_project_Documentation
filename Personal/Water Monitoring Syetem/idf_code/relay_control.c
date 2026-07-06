// drivers/relay_control.c
// Relay control for pump

#include "relay_control.h"
#include "swm_config.h"
#include <driver/gpio.h>
#include <esp_log.h>

static const char *TAG = "RELAY";
static bool relay_state = false;

// ──────────────────────────────────────────────────────────────
// INITIALIZATION
// ──────────────────────────────────────────────────────────────
void relay_init(void) {
    ESP_LOGI(TAG, "Initializing relay (GPIO%d)", PIN_RELAY);
    
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_RELAY),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    gpio_config(&cfg);
    relay_set(false);  // Start with pump OFF
    
    ESP_LOGI(TAG, "Relay initialized (ACTIVE_%s)", 
             RELAY_ACTIVE_HIGH ? "HIGH" : "LOW");
}

// ──────────────────────────────────────────────────────────────
// RELAY CONTROL
// ──────────────────────────────────────────────────────────────
void relay_set(bool pump_on) {
    // Apply polarity: if RELAY_ACTIVE_HIGH is false, invert logic
    bool gpio_level = RELAY_ACTIVE_HIGH ? pump_on : !pump_on;
    gpio_set_level(PIN_RELAY, gpio_level ? 1 : 0);
    relay_state = pump_on;
    
    ESP_LOGI(TAG, "Pump %s (GPIO%d = %d)", 
             pump_on ? "ON" : "OFF", 
             PIN_RELAY, 
             gpio_level ? 1 : 0);
}

bool relay_get_state(void) {
    return relay_state;
}
