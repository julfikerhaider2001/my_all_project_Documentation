// tasks/sensor_task.c
// FreeRTOS task for ultrasonic sensor reading

#include "sensor_task.h"
#include "swm_config.h"
#include "swm_ipc.h"
#include "ultrasonic_sensor.h"
#include "led_control.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <math.h>

static const char *TAG = "SENSOR_TASK";

#define SENSOR_FAIL_THRESHOLD 3  // Mark sensor fault after 3 consecutive fails

// ──────────────────────────────────────────────────────────────
// SENSOR TASK
// ──────────────────────────────────────────────────────────────
void sensor_task(void *pvParameters) {
    int fail_count = 0;
    uint32_t last_report_ms = 0;
    
    // Initialize ultrasonic sensor hardware
    ultrasonic_sensor_init();
    ESP_LOGI(TAG, "Sensor task started (interval: %d ms)", SENSOR_READ_INTERVAL_MS);
    
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Read sensor every SENSOR_READ_INTERVAL_MS
        float distance_cm = ultrasonic_sensor_read();
        
        if (distance_cm < 0) {
            // Read failed
            fail_count++;
            ESP_LOGW(TAG, "Sensor read failed #%d", fail_count);
            
            if (SENSOR_ENABLED && fail_count >= SENSOR_FAIL_THRESHOLD) {
                // Mark sensor as faulty
                STATE_LOCK();
                g_swm_state.sensor_ok = false;
                g_swm_state.sys_state = SYS_STATE_SENSOR_FAULT;
                STATE_UNLOCK();
                
                swm_ipc_set_event(EV_CONFIG_UPDATED);
                ESP_LOGE(TAG, "Sensor marked FAULT");
            }
        } else {
            // Read successful
            fail_count = 0;
            
            // Calculate water level
            water_level_t level = ultrasonic_calculate_level(distance_cm, g_swm_state.tank_height_cm);
            
            // Update global state
            STATE_LOCK();
            bool was_ok = g_swm_state.sensor_ok;
            g_swm_state.sensor_ok = true;
            g_swm_state.water_dist_cm = distance_cm;
            g_swm_state.water_depth_cm = level.depth_cm;
            g_swm_state.water_level_pct = level.level_pct;
            STATE_UNLOCK();
            
            // Log reading
            ESP_LOGI(TAG, "dist=%.1f cm | depth=%.1f cm | level=%.1f%%",
                     distance_cm, level.depth_cm, level.level_pct);
            
            // Send to queue for other tasks
            sensor_msg_t msg = {
                .distance_cm = distance_cm,
                .depth_cm = level.depth_cm,
                .level_pct = level.level_pct,
                .timestamp_ms = now,
                .is_valid = true
            };
            swm_ipc_send_sensor(&msg, 100);
            
            // If sensor just recovered from fault, signal event
            if (!was_ok) {
                swm_ipc_set_event(EV_SENSOR_DATA_READY);
                ESP_LOGI(TAG, "Sensor recovered from fault");
            }
        }
        
        // Sleep until next read
        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

// ──────────────────────────────────────────────────────────────
// TASK CREATION
// ──────────────────────────────────────────────────────────────
void sensor_task_create(void) {
    xTaskCreatePinnedToCore(
        sensor_task,
        "SensorTask",
        TASK_STACK_SENSOR,
        NULL,
        TASK_PRIO_SENSOR,
        NULL,
        0  // Core 0 (protocol stuff on core 1)
    );
    ESP_LOGI(TAG, "Sensor task created");
}
