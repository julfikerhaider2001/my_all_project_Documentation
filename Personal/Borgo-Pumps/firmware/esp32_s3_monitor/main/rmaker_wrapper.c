#include "rmaker_wrapper.h"
#include "config_store.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_ota.h"
#include "esp_rmaker_schedule.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "rmaker";

static esp_rmaker_device_t *s_water_dev = NULL;

static float s_rep_level  = -1000.0f;
static int   s_rep_pump   = -1;
static int   s_rep_mode   = -1;
static int   s_rep_sensor = -1;
static int   s_rep_sstate = -1;
static int   s_rep_start  = -1;
static int   s_rep_stop   = -1;
static int   s_rep_tankH  = -1;
static int   s_rep_force  = -1;

static void reset_report_cache(void)
{
    s_rep_level  = -1000.0f;
    s_rep_pump   = -1;
    s_rep_mode   = -1;
    s_rep_sensor = -1;
    s_rep_sstate = -1;
    s_rep_start  = -1;
    s_rep_stop   = -1;
    s_rep_tankH  = -1;
    s_rep_force  = -1;
}

static void (*s_write_cb)(const char *name, esp_rmaker_param_val_t val) = NULL;

static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                           const esp_rmaker_param_val_t val,
                           void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    const char *name = esp_rmaker_param_get_name(param);
    if (s_write_cb) {
        s_write_cb(name, val);
    }
    return ESP_OK;
}

void rmaker_write_callback_set(void (*cb)(const char *name, esp_rmaker_param_val_t val))
{
    s_write_cb = cb;
}

static esp_rmaker_param_val_t val_bool(bool b) {
    esp_rmaker_param_val_t v = { .type = RMAKER_VAL_TYPE_BOOLEAN, .val = { .b = b } };
    return v;
}

static esp_rmaker_param_val_t val_int(int i) {
    esp_rmaker_param_val_t v = { .type = RMAKER_VAL_TYPE_INTEGER, .val = { .i = i } };
    return v;
}

static esp_rmaker_param_val_t val_float(float f) {
    esp_rmaker_param_val_t v = { .type = RMAKER_VAL_TYPE_FLOAT, .val = { .f = f } };
    return v;
}

static esp_rmaker_param_val_t val_str(const char *s) {
    esp_rmaker_param_val_t v = { .type = RMAKER_VAL_TYPE_STRING, .val = { .s = (char *)s } };
    return v;
}

void rmaker_init(int tank_height, int start_pct, int stop_pct)
{
    esp_rmaker_node_t *node = esp_rmaker_node_init(NULL, "Alif's Home Water Circuit", "Water Monitor");
    if (!node) {
        ESP_LOGE(TAG, "Failed to create RainMaker node");
        return;
    }

    s_water_dev = esp_rmaker_device_create("Alif's Water Node", "esp.device.water-sensor", NULL);
    if (!s_water_dev) {
        ESP_LOGE(TAG, "Failed to create device");
        return;
    }

    esp_rmaker_param_t *p;

    p = esp_rmaker_param_create(PARAM_LEVEL, "esp.param.percentage",
                                val_float(0.0f), PROP_FLAG_READ);
    esp_rmaker_device_add_param(s_water_dev, p);

    p = esp_rmaker_param_create(PARAM_PUMP, "esp.param.power",
                                val_bool(false), PROP_FLAG_READ);
    esp_rmaker_device_add_param(s_water_dev, p);

    p = esp_rmaker_param_create(PARAM_MODE, "esp.param.mode",
                                val_str("Auto"), PROP_FLAG_READ);
    esp_rmaker_device_add_param(s_water_dev, p);

    p = esp_rmaker_param_create(PARAM_SENSOR, "esp.param.power",
                                val_bool(false), PROP_FLAG_READ);
    esp_rmaker_device_add_param(s_water_dev, p);

    p = esp_rmaker_param_create(PARAM_SSTATUS, "esp.param.name",
                                val_str("Starting"), PROP_FLAG_READ);
    esp_rmaker_device_add_param(s_water_dev, p);

    p = esp_rmaker_param_create(PARAM_START, "esp.param.level",
                                val_int(start_pct),
                                PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_device_add_param(s_water_dev, p);

    p = esp_rmaker_param_create(PARAM_STOP, "esp.param.level",
                                val_int(stop_pct),
                                PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_device_add_param(s_water_dev, p);

    p = esp_rmaker_param_create(PARAM_TANKH, "esp.param.length",
                                val_int(tank_height),
                                PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_device_add_param(s_water_dev, p);

    p = esp_rmaker_param_create(PARAM_FORCE, "esp.param.power",
                                val_bool(false),
                                PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_device_add_param(s_water_dev, p);

    esp_rmaker_device_add_cb(s_water_dev, write_cb, NULL);
    esp_rmaker_node_add_device(node, s_water_dev);

    reset_report_cache();
}

void rmaker_start(void)
{
    esp_rmaker_ota_config_t ota_config = {
        .server_cert = NULL,
    };
    esp_rmaker_ota_enable(&ota_config, OTA_USING_TOPICS);
    esp_rmaker_timezone_service_enable();
    esp_rmaker_schedule_enable();
    esp_rmaker_start();

    ESP_LOGI(TAG, "RainMaker started");
}

void rmaker_report(bool force_level, float water_pct, bool pump_on,
                   bool auto_mode, sensor_state_t sensor_state,
                   int start_pct, int stop_pct, int tank_height, bool force_pump)
{
    if (!s_water_dev) return;

    esp_rmaker_param_t *p;

    if (force_level || fabsf(water_pct - s_rep_level) >= LEVEL_REPORT_DELTA) {
        p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_LEVEL);
        if (p && esp_rmaker_param_update(p, val_float(water_pct)) == ESP_OK) {
            s_rep_level = water_pct;
        }
    }

    {
        int v = pump_on ? 1 : 0;
        if (v != s_rep_pump) {
            p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_PUMP);
            if (p && esp_rmaker_param_update(p, val_bool(pump_on)) == ESP_OK) {
                s_rep_pump = v;
            }
        }
    }

    {
        int v = auto_mode ? 0 : 1;
        if (v != s_rep_mode) {
            p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_MODE);
            if (p && esp_rmaker_param_update(p, val_str(auto_mode ? "Auto" : "Manual")) == ESP_OK) {
                s_rep_mode = v;
            }
        }
    }

    {
        int v = (sensor_state == SENSOR_STAT_OK) ? 1 : 0;
        if (v != s_rep_sensor) {
            p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_SENSOR);
            if (p && esp_rmaker_param_update(p, val_bool(v == 1)) == ESP_OK) {
                s_rep_sensor = v;
            }
        }
    }

    {
        int v = (int)sensor_state;
        if (v != s_rep_sstate) {
            p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_SSTATUS);
            if (p && esp_rmaker_param_update(p, val_str(sensor_status_text(sensor_state))) == ESP_OK) {
                s_rep_sstate = v;
            }
        }
    }

    if (start_pct != s_rep_start) {
        p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_START);
        if (p && esp_rmaker_param_update(p, val_int(start_pct)) == ESP_OK) {
            s_rep_start = start_pct;
        }
    }

    if (stop_pct != s_rep_stop) {
        p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_STOP);
        if (p && esp_rmaker_param_update(p, val_int(stop_pct)) == ESP_OK) {
            s_rep_stop = stop_pct;
        }
    }

    if (tank_height != s_rep_tankH) {
        p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_TANKH);
        if (p && esp_rmaker_param_update(p, val_int(tank_height)) == ESP_OK) {
            s_rep_tankH = tank_height;
        }
    }

    {
        int v = force_pump ? 1 : 0;
        if (v != s_rep_force) {
            p = esp_rmaker_device_get_param_by_name(s_water_dev, PARAM_FORCE);
            if (p && esp_rmaker_param_update(p, val_bool(force_pump)) == ESP_OK) {
                s_rep_force = v;
            }
        }
    }
}
