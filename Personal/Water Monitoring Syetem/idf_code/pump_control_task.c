// tasks/pump_control_task.c
// FreeRTOS task for pump control logic

#include "pump_control_task.h"
#include "swm_config.h"
#include "swm_ipc.h"
#include "relay_control.h"
#include "led_control.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char *TAG = "PUMP_TASK";

// ──────────────────────────────────────────────────────────────
// PUMP TASK
// ──────────────────────────────────────────────────────────────
void pump_control_task(void *pvParameters) {
    uint32_t pump_start_ms = 0;
    sensor_msg_t sensor_msg;
    
    // Initialize relay
    relay_init();
    
    ESP_LOGI(TAG, "Pump control task started");
    
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        bool should_run_pump = false;
        const char *reason = "";
        
        // Check for mode change or config update events
        EventBits_t events = swm_ipc_wait_event(
            EV_MODE_CHANGED | EV_SENSOR_DATA_READY | EV_CONFIG_UPDATED,
            pdMS_TO_TICKS(1000)  // Timeout every 1 second
        );
        
        // Process any sensor data in queue
        while (swm_ipc_recv_sensor(&sensor_msg, 0)) {
            // Sensor message received, state already updated by sensor task
        }
        
        // Get current state
        STATE_LOCK();
        swm_mode_t mode = g_swm_state.current_mode;
        float level_pct = g_swm_state.water_level_pct;
        bool sensor_ok = g_swm_state.sensor_ok;
        int start_pct = g_swm_state.start_pct;
        int stop_pct = g_swm_state.stop_pct;
        bool force_pump = g_swm_state.force_pump;
        bool pump_running = g_swm_state.pump_running;
        STATE_UNLOCK();
        
        // ──────────────────────────────────────────────────────────
        // PUMP CONTROL LOGIC
        // ──────────────────────────────────────────────────────────
        
        if (mode == MODE_AUTO) {
            // AUTO MODE: Sensor-based control (if sensor OK or disabled)
            if (sensor_ok || !SENSOR_ENABLED) {
                if (force_pump) {
                    // Force pump ON
                    should_run_pump = true;
                    reason = "FORCE";
                    
                    // Auto-stop at stop_pct
                    if (level_pct >= (float)stop_pct) {
                        should_run_pump = false;
                        reason = "FORCE_STOP";
                        STATE_LOCK();
                        g_swm_state.force_pump = false;
                        STATE_UNLOCK();
                    }
                } else {
                    // Normal AUTO logic
                    if (!pump_running && level_pct < (float)start_pct) {
                        should_run_pump = true;
                        reason = "AUTO_START";
                    } else if (pump_running && level_pct >= (float)stop_pct) {
                        should_run_pump = false;
                        reason = "AUTO_STOP";
                    } else {
                        should_run_pump = pump_running;  // Keep current state
                    }
                }
            } else {
                // Sensor not OK in AUTO mode - don't run pump (safety)
                should_run_pump = false;
                reason = "SENSOR_FAULT";
            }
        } else {
            // MANUAL MODE: Force control only
            if (force_pump) {
                should_run_pump = true;
                reason = "MANUAL_FORCE";
            } else {
                should_run_pump = false;
                reason = "MANUAL_OFF";
            }
        }
        
        // ──────────────────────────────────────────────────────────
        // SAFETY: Pump overtime cutoff (30 minutes)
        // ──────────────────────────────────────────────────────────
        if (should_run_pump && pump_running) {
            uint32_t pump_duration = now - pump_start_ms;
            if (pump_duration > PUMP_OVERTIME_MS) {
                ESP_LOGE(TAG, "OVERTIME CUTOFF - pump running for %lu ms", pump_duration);
                should_run_pump = false;
                reason = "SAFETY_OVERTIME";
                
                STATE_LOCK();
                g_swm_state.sys_state = SYS_STATE_CRITICAL_ERROR;
                g_swm_state.force_pump = false;
                STATE_UNLOCK();
                
                buzzer_beep(100, 80, 6);  // Critical alarm
            }
        }
        
        // ──────────────────────────────────────────────────────────
        // APPLY PUMP CONTROL
        // ──────────────────────────────────────────────────────────
        if (should_run_pump != pump_running) {
            relay_set(should_run_pump);
            
            STATE_LOCK();
            if (should_run_pump) {
                g_swm_state.pump_running = true;
                g_swm_state.sys_state = SYS_STATE_PUMP_RUNNING;
                pump_start_ms = now;
                buzzer_beep(100, 100, 2);
                led_set(LED_AUTO, BLINK_SOLID_ON);  // Indicate pump active
            } else {
                uint32_t runtime = now - pump_start_ms;
                g_swm_state.pump_runtime_s += runtime / 1000;
                g_swm_state.pump_running = false;
                g_swm_state.sys_state = (sensor_ok || !SENSOR_ENABLED) ? SYS_STATE_NORMAL : SYS_STATE_SENSOR_FAULT;
                buzzer_beep(60, 60, 1);
                led_set(LED_AUTO, BLINK_SOLID_OFF);
            }
            STATE_UNLOCK();
            
            ESP_LOGI(TAG, "Pump %s - reason: %s (level: %.1f%%, start: %d%%, stop: %d%%)",
                     should_run_pump ? "ON" : "OFF", reason, level_pct, start_pct, stop_pct);
            
            // Send pump update to RainMaker queue
            pump_msg_t msg = {
                .pump_on = should_run_pump,
                .timestamp_ms = now,
                .reason = reason
            };
            swm_ipc_send_pump(&msg, 100);
            
            // Set event for RainMaker to report update
            swm_ipc_set_event(EV_CONFIG_UPDATED);
        }
        
        // Prevent busy loop
        if (!(events & (EV_MODE_CHANGED | EV_SENSOR_DATA_READY | EV_CONFIG_UPDATED))) {
            vTaskDelay(pdMS_TO_TICKS(100));  // Sleep if no events
        }
    }
}

// ──────────────────────────────────────────────────────────────
// TASK CREATION
// ──────────────────────────────────────────────────────────────
void pump_control_task_create(void) {
    xTaskCreatePinnedToCore(
        pump_control_task,
        "PumpTask",
        TASK_STACK_PUMP,
        NULL,
        TASK_PRIO_PUMP,
        NULL,
        0  // Core 0
    );
    ESP_LOGI(TAG, "Pump control task created");
}
