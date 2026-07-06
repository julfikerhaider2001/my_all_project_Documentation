#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "config_store";
static nvs_handle_t s_nvs_handle;

void config_store_init(void)
{
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    }
}

static int nvs_get_int(const char *key, int default_val)
{
    int val = default_val;
    esp_err_t err = nvs_get_i32(s_nvs_handle, key, (int32_t *)&val);
    if (err != ESP_OK) {
        val = default_val;
    }
    return val;
}

static void nvs_set_int(const char *key, int val)
{
    esp_err_t err = nvs_set_i32(s_nvs_handle, key, (int32_t)val);
    if (err == ESP_OK) {
        nvs_commit(s_nvs_handle);
    } else {
        ESP_LOGE(TAG, "nvs_set failed for %s: %s", key, esp_err_to_name(err));
    }
}

int config_store_get_tank_height(void) { return nvs_get_int(NVS_KEY_TANK_H, DEFAULT_TANK_HEIGHT); }
int config_store_get_start_pct(void)   { return nvs_get_int(NVS_KEY_START_PCT, DEFAULT_START_PCT); }
int config_store_get_stop_pct(void)    { return nvs_get_int(NVS_KEY_STOP_PCT, DEFAULT_STOP_PCT); }

void config_store_set_tank_height(int val) { nvs_set_int(NVS_KEY_TANK_H, val); }
void config_store_set_start_pct(int val)   { nvs_set_int(NVS_KEY_START_PCT, val); }
void config_store_set_stop_pct(int val)    { nvs_set_int(NVS_KEY_STOP_PCT, val); }
