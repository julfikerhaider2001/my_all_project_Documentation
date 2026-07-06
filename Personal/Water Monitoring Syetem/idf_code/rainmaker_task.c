// tasks/rainmaker_task.c
// RainMaker cloud integration and parameter sync

#include "rainmaker_task.h"
#include "swm_config.h"
#include "swm_ipc.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_ota.h>

static const char *TAG = "RAINMAKER";

// ──────────────────────────────────────────────────────────────
// RainMaker PARAMETER CALLBACKS
// These are called when app sends parameter updates to device
// ──────────────────────────────────────────────────────────────
static esp_err_t rainmaker_write_callback(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param, const esp_rmaker_param_val_t val, void *priv_data) {
    const char *param_name = esp_rmaker_param_get_name(param);
    
    if (strcmp(param_name, PARAM_START_PCT) == 0) {
        // Start % parameter update
        int new_start = val.val.i;
        if (new_start >= 0 && new_start <= 100) {
            swm_config_save_start_pct(new_start);
            ESP_LOGI(TAG, "Start %% updated: %d%%", new_start);
        }
    }
    else if (strcmp(param_name, PARAM_STOP_PCT) == 0) {
        // Stop % parameter update
        int new_stop = val.val.i;
        if (new_stop >= 0 && new_stop <= 100) {
            swm_config_save_stop_pct(new_stop);
            ESP_LOGI(TAG, "Stop %% updated: %d%%", new_stop);
        }
    }
    else if (strcmp(param_name, PARAM_TANK_HEIGHT) == 0) {
        // Tank height parameter update
        int new_height = val.val.i;
        if (new_height > 0 && new_height <= 500) {
            swm_config_save_tank_height(new_height);
            ESP_LOGI(TAG, "Tank height updated: %d cm", new_height);
        }
    }
    else if (strcmp(param_name, PARAM_FORCE_PUMP) == 0) {
        // Force pump toggle
        bool force = val.val.b;
        STATE_LOCK();
        g_swm_state.force_pump = force;
        STATE_UNLOCK();
        
        ESP_LOGI(TAG, "Force pump: %s", force ? "ON" : "OFF");
        swm_ipc_set_event(EV_CONFIG_UPDATED);
    }
    
    return ESP_OK;
}

// ──────────────────────────────────────────────────────────────
// RainMaker SETUP
// Creates device, parameters, and registers callbacks
// ──────────────────────────────────────────────────────────────
static void rainmaker_setup(void) {
    ESP_LOGI(TAG, "Setting up RainMaker device and parameters...");
    
    // Create RainMaker node (device group)
    esp_rmaker_node_t *node = esp_rmaker_node_init(NULL, RMAKER_NODE_NAME, "esp.device.water-controller");
    if (!node) {
        ESP_LOGE(TAG, "Failed to create RainMaker node");
        return;
    }
    
    // Create water tank device
    esp_rmaker_device_t *device = esp_rmaker_device_create("Water Tank", "esp.device.water-sensor", NULL);
    if (!device) {
        ESP_LOGE(TAG, "Failed to create device");
        return;
    }
    
    // Add parameters to device
    
    // Water Level (read-only percentage)
    esp_rmaker_param_t *param = esp_rmaker_param_create(
        PARAM_WATER_LEVEL,
        "esp.param.percentage",
        esp_rmaker_float(0.0f),
        PROP_FLAG_READ
    );
    if (param) {
        esp_rmaker_param_add_ui_type(param, "esp.ui.slider");
        esp_rmaker_param_add_bounds(param, esp_rmaker_float(0.0f), esp_rmaker_float(100.0f), esp_rmaker_float(1.0f));
        esp_rmaker_device_add_param(device, param);
    }
    
    // Pump Running (read-only boolean)
    param = esp_rmaker_param_create(
        PARAM_PUMP_RUNNING,
        "esp.param.power",
        esp_rmaker_bool(false),
        PROP_FLAG_READ
    );
    if (param) esp_rmaker_device_add_param(device, param);
    
    // Mode (read-only string)
    param = esp_rmaker_param_create(
        PARAM_MODE,
        NULL,
        esp_rmaker_str("Auto"),
        PROP_FLAG_READ
    );
    if (param) esp_rmaker_device_add_param(device, param);
    
    // Sensor OK (read-only boolean)
    param = esp_rmaker_param_create(
        PARAM_SENSOR_OK,
        "esp.param.power",
        esp_rmaker_bool(true),
        PROP_FLAG_READ
    );
    if (param) esp_rmaker_device_add_param(device, param);
    
    // Force Pump (read-write boolean)
    param = esp_rmaker_param_create(
        PARAM_FORCE_PUMP,
        "esp.param.power",
        esp_rmaker_bool(false),
        PROP_FLAG_READ | PROP_FLAG_WRITE
    );
    if (param) {
        esp_rmaker_param_add_ui_type(param, "esp.ui.toggle");
        esp_rmaker_device_add_param(device, param);
    }
    
    // Start % (read-write integer)
    param = esp_rmaker_param_create(
        PARAM_START_PCT,
        NULL,
        esp_rmaker_int(DEFAULT_START_PCT),
        PROP_FLAG_READ | PROP_FLAG_WRITE
    );
    if (param) {
        esp_rmaker_param_add_ui_type(param, "esp.ui.slider");
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
        esp_rmaker_device_add_param(device, param);
    }
    
    // Stop % (read-write integer)
    param = esp_rmaker_param_create(
        PARAM_STOP_PCT,
        NULL,
        esp_rmaker_int(DEFAULT_STOP_PCT),
        PROP_FLAG_READ | PROP_FLAG_WRITE
    );
    if (param) {
        esp_rmaker_param_add_ui_type(param, "esp.ui.slider");
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1));
        esp_rmaker_device_add_param(device, param);
    }
    
    // Tank Height (read-write integer)
    param = esp_rmaker_param_create(
        PARAM_TANK_HEIGHT,
        NULL,
        esp_rmaker_int(DEFAULT_TANK_HEIGHT),
        PROP_FLAG_READ | PROP_FLAG_WRITE
    );
    if (param) {
        esp_rmaker_param_add_ui_type(param, "esp.ui.slider");
        esp_rmaker_param_add_bounds(param, esp_rmaker_int(10), esp_rmaker_int(500), esp_rmaker_int(1));
        esp_rmaker_device_add_param(device, param);
    }
    
    // Register write callback for parameter updates
    esp_rmaker_device_add_cb(device, rainmaker_write_callback, NULL);
    
    // Add device to node
    esp_rmaker_node_add_device(node, device);
    
    // Configure and enable OTA
    esp_rmaker_ota_enable_default();
    
    // Start RainMaker
    ESP_ERROR_CHECK(esp_rmaker_node_add(node));
    ESP_ERROR_CHECK(esp_rmaker_start());
    
    ESP_LOGI(TAG, "RainMaker setup complete");
}

// ──────────────────────────────────────────────────────────────
// RainMaker TASK
// Waits for WiFi, initializes RainMaker, and syncs parameters
// ──────────────────────────────────────────────────────────────
void rainmaker_task(void *pvParameters) {
    ESP_LOGI(TAG, "RainMaker task started, waiting for WiFi...");
    
    // Wait for WiFi to connect
    EventBits_t bits = swm_ipc_wait_event(EV_WIFI_CONNECTED, pdMS_TO_TICKS(60000));
    if (!(bits & EV_WIFI_CONNECTED)) {
        ESP_LOGE(TAG, "WiFi timeout - RainMaker task aborting");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "WiFi connected, initializing RainMaker...");
    
    // Setup RainMaker device and parameters
    rainmaker_setup();
    
    STATE_LOCK();
    g_swm_state.rainmaker_ready = true;
    STATE_UNLOCK();
    swm_ipc_set_event(EV_RAINMAKER_READY);
    
    // Periodic parameter update loop
    uint32_t last_report_ms = 0;
    
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Wait for update events (or timeout for periodic heartbeat)
        EventBits_t events = swm_ipc_wait_event(
            EV_CONFIG_UPDATED,
            pdMS_TO_TICKS(RM_HEARTBEAT_MS)
        );
        
        // Process RainMaker messages from queue
        rmaker_msg_t rmaker_msg;
        while (swm_ipc_recv_sensor(&rmaker_msg, 0)) {
            // Update RainMaker parameter
            // (Implementation depends on esp_rmaker_param_update API)
        }
        
        // Send periodic heartbeat update
        if (now - last_report_ms >= RM_REPORT_INTERVAL_MS || (events & EV_CONFIG_UPDATED)) {
            STATE_LOCK();
            float level = g_swm_state.water_level_pct;
            bool pump = g_swm_state.pump_running;
            bool sensor_ok = g_swm_state.sensor_ok;
            const char *mode_str = g_swm_state.current_mode == MODE_AUTO ? "Auto" : "Manual";
            bool force = g_swm_state.force_pump;
            int start = g_swm_state.start_pct;
            int stop = g_swm_state.stop_pct;
            int tank = g_swm_state.tank_height_cm;
            STATE_UNLOCK();
            
            // Update parameters in RainMaker
            // Note: This would use esp_rmaker_device_update_and_report() 
            // The exact API depends on your RainMaker version
            
            ESP_LOGD(TAG, "Reporting: level=%.1f%% pump=%d mode=%s sensor=%d", 
                     level, pump, mode_str, sensor_ok);
            
            last_report_ms = now;
            swm_ipc_clear_event(EV_CONFIG_UPDATED);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ──────────────────────────────────────────────────────────────
// TASK CREATION
// ──────────────────────────────────────────────────────────────
void rainmaker_task_create(void) {
    xTaskCreatePinnedToCore(
        rainmaker_task,
        "RainMakerTask",
        TASK_STACK_RAINMAKER,
        NULL,
        TASK_PRIO_RAINMAKER,
        NULL,
        1  // Core 1 for network tasks
    );
    ESP_LOGI(TAG, "RainMaker task created");
}
