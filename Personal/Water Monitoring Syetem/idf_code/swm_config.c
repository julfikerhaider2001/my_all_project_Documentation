// config/swm_config.c
// Configuration storage (NVS) and global state initialization

#include "swm_config.h"
#include "swm_ipc.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "SWM_CONFIG";

// ──────────────────────────────────────────────────────────────
// GLOBAL STATE (protected by g_state_mutex)
// ──────────────────────────────────────────────────────────────
swm_state_t g_swm_state = {
    .wifi_connected = false,
    .rainmaker_claimed = false,
    .rainmaker_ready = false,
    .sensor_ok = !SENSOR_ENABLED,      // Start as OK if disabled, checking if enabled
    .water_level_pct = 50.0f,           // Safe default
    .water_depth_cm = 0.0f,
    .water_dist_cm = 0.0f,
    .pump_running = false,
    .pump_runtime_s = 0,
    .tank_height_cm = DEFAULT_TANK_HEIGHT,
    .start_pct = DEFAULT_START_PCT,
    .stop_pct = DEFAULT_STOP_PCT,
    .current_mode = MODE_AUTO,
    .force_pump = false,
    .sys_state = SYS_STATE_WIFI_CONNECTING,
    .uptime_s = 0
};

// ──────────────────────────────────────────────────────────────
// INITIALIZATION
// ──────────────────────────────────────────────────────────────
void swm_config_init(void) {
    ESP_LOGI(TAG, "Initializing configuration...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Open NVS namespace
    nvs_handle_t nvs_handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return;
    }
    
    // Load tank height
    int32_t tank_h = 0;
    ret = nvs_get_i32(nvs_handle, NVS_KEY_TANK_H, &tank_h);
    if (ret == ESP_OK && tank_h > 0) {
        g_swm_state.tank_height_cm = (int)tank_h;
        ESP_LOGI(TAG, "Loaded tank height: %d cm", g_swm_state.tank_height_cm);
    } else {
        g_swm_state.tank_height_cm = DEFAULT_TANK_HEIGHT;
        ESP_LOGI(TAG, "Using default tank height: %d cm", DEFAULT_TANK_HEIGHT);
    }
    
    // Load start %
    int32_t start_pct = 0;
    ret = nvs_get_i32(nvs_handle, NVS_KEY_START_PCT, &start_pct);
    if (ret == ESP_OK && start_pct >= 0 && start_pct <= 100) {
        g_swm_state.start_pct = (int)start_pct;
        ESP_LOGI(TAG, "Loaded start %%: %d", g_swm_state.start_pct);
    } else {
        g_swm_state.start_pct = DEFAULT_START_PCT;
        ESP_LOGI(TAG, "Using default start %%: %d", DEFAULT_START_PCT);
    }
    
    // Load stop %
    int32_t stop_pct = 0;
    ret = nvs_get_i32(nvs_handle, NVS_KEY_STOP_PCT, &stop_pct);
    if (ret == ESP_OK && stop_pct >= 0 && stop_pct <= 100) {
        g_swm_state.stop_pct = (int)stop_pct;
        ESP_LOGI(TAG, "Loaded stop %%: %d", g_swm_state.stop_pct);
    } else {
        g_swm_state.stop_pct = DEFAULT_STOP_PCT;
        ESP_LOGI(TAG, "Using default stop %%: %d", DEFAULT_STOP_PCT);
    }
    
    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Configuration loaded successfully");
    ESP_LOGI(TAG, "Tank: %d cm | Start: %d%% | Stop: %d%%",
             g_swm_state.tank_height_cm, g_swm_state.start_pct, g_swm_state.stop_pct);
}

// ──────────────────────────────────────────────────────────────
// CONFIGURATION SAVE FUNCTIONS
// ──────────────────────────────────────────────────────────────
void swm_config_save_tank_height(int height_cm) {
    if (height_cm <= 0 || height_cm > 500) {
        ESP_LOGW(TAG, "Invalid tank height: %d", height_cm);
        return;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for tank height: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = nvs_set_i32(nvs_handle, NVS_KEY_TANK_H, (int32_t)height_cm);
    if (ret == ESP_OK) {
        nvs_commit(nvs_handle);
        CONFIG_LOCK();
        g_swm_state.tank_height_cm = height_cm;
        CONFIG_UNLOCK();
        ESP_LOGI(TAG, "Tank height saved: %d cm", height_cm);
    } else {
        ESP_LOGE(TAG, "Failed to save tank height: %s", esp_err_to_name(ret));
    }
    
    nvs_close(nvs_handle);
}

void swm_config_save_start_pct(int pct) {
    if (pct < 0 || pct > 100) {
        ESP_LOGW(TAG, "Invalid start %%: %d", pct);
        return;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for start %%: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = nvs_set_i32(nvs_handle, NVS_KEY_START_PCT, (int32_t)pct);
    if (ret == ESP_OK) {
        nvs_commit(nvs_handle);
        CONFIG_LOCK();
        g_swm_state.start_pct = pct;
        CONFIG_UNLOCK();
        ESP_LOGI(TAG, "Start %% saved: %d", pct);
    } else {
        ESP_LOGE(TAG, "Failed to save start %%: %s", esp_err_to_name(ret));
    }
    
    nvs_close(nvs_handle);
}

void swm_config_save_stop_pct(int pct) {
    if (pct < 0 || pct > 100) {
        ESP_LOGW(TAG, "Invalid stop %%: %d", pct);
        return;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for stop %%: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = nvs_set_i32(nvs_handle, NVS_KEY_STOP_PCT, (int32_t)pct);
    if (ret == ESP_OK) {
        nvs_commit(nvs_handle);
        CONFIG_LOCK();
        g_swm_state.stop_pct = pct;
        CONFIG_UNLOCK();
        ESP_LOGI(TAG, "Stop %% saved: %d", pct);
    } else {
        ESP_LOGE(TAG, "Failed to save stop %%: %s", esp_err_to_name(ret));
    }
    
    nvs_close(nvs_handle);
}
