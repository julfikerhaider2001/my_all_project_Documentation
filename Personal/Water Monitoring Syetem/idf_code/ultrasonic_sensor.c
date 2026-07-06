// drivers/ultrasonic_sensor.c
// HC-SR04 ultrasonic distance sensor driver

#include "ultrasonic_sensor.h"
#include "swm_config.h"
#include <driver/gpio.h>
#include <esp_rom_sys.h>
#include <esp_log.h>
#include <math.h>

static const char *TAG = "ULTRASONIC";

// ──────────────────────────────────────────────────────────────
// INITIALIZATION
// ──────────────────────────────────────────────────────────────
void ultrasonic_sensor_init(void) {
    if (!SENSOR_ENABLED) {
        ESP_LOGI(TAG, "Sensor disabled - skipping hardware init");
        return;
    }
    
    ESP_LOGI(TAG, "Initializing HC-SR04 ultrasonic sensor");
    
    // Configure TRIG pin as output
    gpio_config_t trig_cfg = {
        .pin_bit_mask = (1ULL << PIN_TRIG),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&trig_cfg);
    gpio_set_level(PIN_TRIG, 0);
    
    // Configure ECHO pin as input
    gpio_config_t echo_cfg = {
        .pin_bit_mask = (1ULL << PIN_ECHO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&echo_cfg);
    
    ESP_LOGI(TAG, "HC-SR04 initialized (TRIG=GPIO%d, ECHO=GPIO%d)", PIN_TRIG, PIN_ECHO);
}

// ──────────────────────────────────────────────────────────────
// SENSOR READING
// ──────────────────────────────────────────────────────────────
float ultrasonic_sensor_read(void) {
    // If sensor disabled, return safe dummy value
    if (!SENSOR_ENABLED) {
        return 50.0f;  // Middle distance
    }
    
    // Send 10µs trigger pulse
    gpio_set_level(PIN_TRIG, 0);
    esp_rom_delay_us(2);
    gpio_set_level(PIN_TRIG, 1);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_TRIG, 0);
    
    // Wait for ECHO pin to go HIGH (with timeout)
    uint32_t timeout = 1000000;  // 1 second timeout
    uint32_t count = 0;
    
    while (gpio_get_level(PIN_ECHO) == 0 && count < timeout) {
        count++;
        esp_rom_delay_us(1);
    }
    
    if (count >= timeout) {
        ESP_LOGW(TAG, "Timeout waiting for ECHO HIGH");
        return -1.0f;
    }
    
    // Measure ECHO pulse width
    uint32_t pulse_start = count;
    count = 0;
    
    while (gpio_get_level(PIN_ECHO) == 1 && count < timeout) {
        count++;
        esp_rom_delay_us(1);
    }
    
    if (count >= timeout) {
        ESP_LOGW(TAG, "Timeout waiting for ECHO LOW");
        return -1.0f;
    }
    
    uint32_t pulse_width = count;  // Duration in µs
    
    // Calculate distance: speed of sound = 343 m/s
    // distance = (pulse_width_us / 2) / 29.1
    // Divided by 2 because sound travels to object and back
    float distance_cm = pulse_width / 58.0f;
    
    // Sanity check: HC-SR04 typically works 2cm to 400cm
    if (distance_cm < 2.0f || distance_cm > 400.0f) {
        ESP_LOGW(TAG, "Distance out of range: %.1f cm (pulse: %lu µs)", distance_cm, pulse_width);
        return -1.0f;
    }
    
    ESP_LOGD(TAG, "Distance: %.1f cm (pulse: %lu µs)", distance_cm, pulse_width);
    return distance_cm;
}

// ──────────────────────────────────────────────────────────────
// WATER LEVEL CALCULATION
// ──────────────────────────────────────────────────────────────
water_level_t ultrasonic_calculate_level(float distance_cm, int tank_height_cm) {
    water_level_t result = {0};
    
    if (distance_cm < 0 || tank_height_cm <= 0) {
        result.depth_cm = 0;
        result.level_pct = 0;
        return result;
    }
    
    // Water depth = tank height - distance from top
    result.depth_cm = (float)tank_height_cm - distance_cm;
    if (result.depth_cm < 0) result.depth_cm = 0;
    
    // Water level percentage
    result.level_pct = (result.depth_cm / (float)tank_height_cm) * 100.0f;
    if (result.level_pct > 100.0f) result.level_pct = 100.0f;
    if (result.level_pct < 0.0f) result.level_pct = 0.0f;
    
    return result;
}
