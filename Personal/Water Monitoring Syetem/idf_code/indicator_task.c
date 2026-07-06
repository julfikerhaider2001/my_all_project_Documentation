// tasks/indicator_task.c
// FreeRTOS task for LED and Buzzer indicators

#include "indicator_task.h"
#include "swm_config.h"
#include "swm_ipc.h"
#include "led_control.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char *TAG = "INDICATOR_TASK";

// ──────────────────────────────────────────────────────────────
// INDICATOR TASK
// ──────────────────────────────────────────────────────────────
void indicator_task(void *pvParameters) {
    // Initialize LEDs and buzzer
    led_init();
    buzzer_init();
    
    // Power-on beep
    buzzer_beep(80, 80, 3);
    
    ESP_LOGI(TAG, "Indicator task started (tick: %d ms)", LED_TICK_MS);
    
    while (1) {
        // Get current state
        STATE_LOCK();
        swm_sys_state_t sys_state = g_swm_state.sys_state;
        swm_mode_t mode = g_swm_state.current_mode;
        bool wifi_connected = g_swm_state.wifi_connected;
        bool sensor_ok = g_swm_state.sensor_ok;
        bool pump_running = g_swm_state.pump_running;
        STATE_UNLOCK();
        
        // ──────────────────────────────────────────────────────────
        // LED1 (AUTO mode indicator)
        // ──────────────────────────────────────────────────────────
        if (mode == MODE_AUTO) {
            led_set(LED_AUTO, BLINK_SOLID_ON);
        } else {
            led_set(LED_AUTO, BLINK_SOLID_OFF);
        }
        
        // ──────────────────────────────────────────────────────────
        // LED2 (MANUAL mode indicator)
        // ──────────────────────────────────────────────────────────
        if (mode == MODE_MANUAL) {
            led_set(LED_MANUAL, BLINK_SOLID_ON);
        } else {
            led_set(LED_MANUAL, BLINK_SOLID_OFF);
        }
        
        // ──────────────────────────────────────────────────────────
        // LED3 (WiFi status)
        // ──────────────────────────────────────────────────────────
        if (wifi_connected) {
            led_set(LED_WIFI, BLINK_SOLID_ON);  // Solid = connected
        } else {
            led_set(LED_WIFI, BLINK_SLOW);      // Slow blink = connecting
        }
        
        // ──────────────────────────────────────────────────────────
        // LED4 (Sensor status)
        // ──────────────────────────────────────────────────────────
        if (SENSOR_ENABLED) {
            if (sensor_ok) {
                led_set(LED_SENSOR, BLINK_SOLID_OFF);  // OFF = OK
            } else if (sys_state == SYS_STATE_CRITICAL_ERROR) {
                led_set(LED_SENSOR, BLINK_VERY_FAST);  // Very fast = critical error
            } else {
                led_set(LED_SENSOR, BLINK_FAST);       // Fast = fault
            }
        } else {
            led_set(LED_SENSOR, BLINK_SOLID_OFF);      // Always OFF if sensor disabled
        }
        
        // ──────────────────────────────────────────────────────────
        // LED TICK (handle blink updates)
        // ──────────────────────────────────────────────────────────
        led_tick();
        
        // ──────────────────────────────────────────────────────────
        // BUZZER TICK (handle beep sequence)
        // ──────────────────────────────────────────────────────────
        buzzer_tick();
        
        // Sleep for LED_TICK_MS (typically 50ms)
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

// ──────────────────────────────────────────────────────────────
// TASK CREATION
// ──────────────────────────────────────────────────────────────
void indicator_task_create(void) {
    xTaskCreatePinnedToCore(
        indicator_task,
        "IndicatorTask",
        TASK_STACK_INDICATOR,
        NULL,
        TASK_PRIO_INDICATOR,
        NULL,
        0  // Core 0
    );
    ESP_LOGI(TAG, "Indicator task created");
}
