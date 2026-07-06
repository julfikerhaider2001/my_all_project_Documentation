// main/main.c
// Main entry point for Southern Water Monitor ESP-IDF FreeRTOS application

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>

// Include all task and component headers
#include "swm_config.h"
#include "swm_ipc.h"
#include "sensor_task.h"
#include "pump_control_task.h"
#include "indicator_task.h"
#include "dpdt_switch_task.h"
#include "wifi_provisioning_task.h"
#include "rainmaker_task.h"

static const char *TAG = "MAIN";

// ──────────────────────────────────────────────────────────────
// HARDWARE WATCHDOG CONFIGURATION
// ──────────────────────────────────────────────────────────────
#include <esp_task_wdt.h>

static void configure_watchdog(void) {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,  // Watch both cores
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_config);
    
    // Subscribe main task to watchdog
    esp_task_wdt_add(NULL);
}

// ──────────────────────────────────────────────────────────────
// SYSTEM INITIALIZATION TASK
// Responsible for:
//   - Network initialization (WiFi, mDNS)
//   - RainMaker setup
//   - Initial state setup
// ──────────────────────────────────────────────────────────────
void system_init_task(void *pvParameters) {
    ESP_LOGI(TAG, "System initialization task started");
    
    // Initialize network stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize WiFi (default init)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    ESP_LOGI(TAG, "Network stack initialized");
    
    // Give other tasks time to be created
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Create WiFi provisioning task (handles BLE + WiFi credentials)
    wifi_provisioning_task_create();
    
    // Create RainMaker task (handles cloud sync)
    rainmaker_task_create();
    
    ESP_LOGI(TAG, "All tasks created successfully");
    
    // This task is done - delete it to free memory
    vTaskDelete(NULL);
}

// ──────────────────────────────────────────────────────────────
// MAIN APPLICATION ENTRY POINT
// ──────────────────────────────────────────────────────────────
void app_main(void) {
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Southern Water Monitor v4.5.1 (ESP-IDF + FreeRTOS)      ║");
    ESP_LOGI(TAG, "║  Board: ESP32-S3 SuperMini                               ║");
    ESP_LOGI(TAG, "║  Sensor Mode: %s                                     ║",
             SENSOR_ENABLED ? "ENABLED " : "DISABLED");
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════════════════════╝");
    
    // ──────────────────────────────────────────────────────────────
    // PHASE 1: Initialize core subsystems
    // ──────────────────────────────────────────────────────────────
    
    // Configure watchdog timer
    configure_watchdog();
    ESP_LOGI(TAG, "Watchdog configured");
    
    // Initialize IPC primitives (queues, mutexes, events)
    swm_ipc_init();
    ESP_LOGI(TAG, "IPC subsystem initialized");
    
    // Load configuration from NVS flash
    swm_config_init();
    ESP_LOGI(TAG, "Configuration loaded from NVS");
    
    // ──────────────────────────────────────────────────────────────
    // PHASE 2: Create sensor and control tasks
    // These don't require network connectivity
    // ──────────────────────────────────────────────────────────────
    
    // Sensor task: reads HC-SR04 ultrasonic sensor every 2 seconds
    sensor_task_create();
    ESP_LOGI(TAG, "Sensor task created");
    
    // Pump control task: controls relay based on water level & mode
    pump_control_task_create();
    ESP_LOGI(TAG, "Pump control task created");
    
    // Indicator task: controls LEDs and buzzer (feedback)
    indicator_task_create();
    ESP_LOGI(TAG, "Indicator task created");
    
    // DPDT switch task: monitors AUTO/MANUAL mode switch
    dpdt_switch_task_create();
    ESP_LOGI(TAG, "DPDT switch task created");
    
    // ──────────────────────────────────────────────────────────────
    // PHASE 3: Initialize network and create network tasks
    // This is done in system_init_task to allow time for core tasks
    // ──────────────────────────────────────────────────────────────
    
    xTaskCreatePinnedToCore(
        system_init_task,
        "SysInitTask",
        4096,
        NULL,
        tskIDLE_PRIORITY + 2,
        NULL,
        1  // Core 1 for network tasks
    );
    
    // ──────────────────────────────────────────────────────────────
    // STARTUP COMPLETE
    // ──────────────────────────────────────────────────────────────
    
    ESP_LOGI(TAG, "Boot sequence complete - system operational");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Task Summary:");
    ESP_LOGI(TAG, "  ✓ Sensor Task       (GPIO%d TRIG, GPIO%d ECHO)", PIN_TRIG, PIN_ECHO);
    ESP_LOGI(TAG, "  ✓ Pump Control      (GPIO%d RELAY, AUTO/MANUAL)", PIN_RELAY);
    ESP_LOGI(TAG, "  ✓ Indicator         (LED: %d,%d,%d,%d + Buzzer %d)", 
             PIN_LED_AUTO, PIN_LED_MANUAL, PIN_LED_WIFI, PIN_LED_SENSOR, PIN_BUZZER);
    ESP_LOGI(TAG, "  ✓ DPDT Switch       (GPIO%d for mode)", PIN_DPDT);
    ESP_LOGI(TAG, "  ✓ WiFi Provisioning (BLE: '%s')", RMAKER_SERVICE_NAME);
    ESP_LOGI(TAG, "  ✓ RainMaker Cloud   (Node: '%s')", RMAKER_NODE_NAME);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Waiting for WiFi provisioning via BLE...");
    ESP_LOGI(TAG, "Once provisioned, device will connect to RainMaker cloud");
}

// ──────────────────────────────────────────────────────────────
// HEAP STATISTICS (optional debugging)
// Call this from any task to check memory usage
// ──────────────────────────────────────────────────────────────
void print_heap_stats(void) {
    esp_himem_info_t himem_info;
    esp_himem_get_info(&himem_info);
    
    ESP_LOGD(TAG, "Heap Stats:");
    ESP_LOGD(TAG, "  Free: %u bytes", esp_get_free_heap_size());
    ESP_LOGD(TAG, "  Min Free: %u bytes", esp_get_minimum_free_heap_size());
}
