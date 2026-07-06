// tasks/dpdt_switch_task.c
// FreeRTOS task for DPDT mode switch monitoring

#include "dpdt_switch_task.h"
#include "swm_config.h"
#include "swm_ipc.h"
#include "led_control.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char *TAG = "DPDT_TASK";

// ──────────────────────────────────────────────────────────────
// DPDT TASK
// ──────────────────────────────────────────────────────────────
void dpdt_switch_task(void *pvParameters) {
    bool last_dpdt = false;
    uint32_t last_change_ms = 0;
    
    // Initialize DPDT pin as input
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_DPDT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);
    
    last_dpdt = gpio_get_level(PIN_DPDT) == 1 ? true : false;
    
    ESP_LOGI(TAG, "DPDT switch task started (debounce: %d ms)", DPDT_DEBOUNCE_MS);
    ESP_LOGI(TAG, "Initial mode: %s", last_dpdt ? "MANUAL" : "AUTO");
    
    while (1) {
        bool dpdt_now = gpio_get_level(PIN_DPDT) == 1 ? true : false;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if (dpdt_now != last_dpdt) {
            // Debounce: wait for stable state
            if (now - last_change_ms >= DPDT_DEBOUNCE_MS) {
                last_dpdt = dpdt_now;
                last_change_ms = now;
                
                swm_mode_t new_mode = dpdt_now ? MODE_MANUAL : MODE_AUTO;
                
                // Update global state
                STATE_LOCK();
                bool mode_changed = (g_swm_state.current_mode != new_mode);
                if (mode_changed) {
                    g_swm_state.current_mode = new_mode;
                    
                    // In MANUAL mode, disable any active force/pump on mode switch
                    if (new_mode == MODE_MANUAL && g_swm_state.pump_running) {
                        g_swm_state.force_pump = false;
                    }
                }
                STATE_UNLOCK();
                
                if (mode_changed) {
                    ESP_LOGI(TAG, "Mode changed → %s", new_mode == MODE_AUTO ? "AUTO" : "MANUAL");
                    buzzer_beep(60, 60, 2);  // Beep to confirm mode change
                    
                    // Signal event for pump task to react
                    swm_ipc_set_event(EV_MODE_CHANGED);
                    
                    // Signal for RainMaker to report update
                    swm_ipc_set_event(EV_CONFIG_UPDATED);
                }
            }
        } else {
            // Reset timer if no change detected
            last_change_ms = now;
        }
        
        // Check switch every 50ms for responsive feel
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ──────────────────────────────────────────────────────────────
// TASK CREATION
// ──────────────────────────────────────────────────────────────
void dpdt_switch_task_create(void) {
    xTaskCreatePinnedToCore(
        dpdt_switch_task,
        "DPDTTask",
        TASK_STACK_DPDT,
        NULL,
        TASK_PRIO_DPDT,
        NULL,
        0  // Core 0
    );
    ESP_LOGI(TAG, "DPDT switch task created");
}
