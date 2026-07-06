#include "pump.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pump";

#define RELAY_PIN 23

static bool     s_pump_on       = false;
static int64_t  s_pump_start_us = 0;
static uint32_t s_total_runtime = 0;
static int      s_relay_physical = -1;

void pump_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << RELAY_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    pump_relay_write(false);
}

void pump_relay_write(bool running)
{
    int level = (RELAY_ACTIVE_HIGH ? running : !running) ? 1 : 0;
    if (level != s_relay_physical) {
        gpio_set_level(RELAY_PIN, level);
        s_relay_physical = level;
    }
}

void pump_set(bool on)
{
    if (on == s_pump_on) return;

    s_pump_on = on;
    pump_relay_write(on);

    if (on) {
        s_pump_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "ON");
    } else {
        uint32_t elapsed = (uint32_t)((esp_timer_get_time() - s_pump_start_us) / 1000000);
        s_total_runtime += elapsed;
        ESP_LOGI(TAG, "OFF  (session=%lus  total=%lus)", (unsigned long)elapsed, (unsigned long)s_total_runtime);
    }
}

bool pump_is_on(void) { return s_pump_on; }

void pump_add_runtime(uint32_t seconds) { s_total_runtime += seconds; }
uint32_t pump_get_total_runtime(void)   { return s_total_runtime; }
uint32_t pump_get_start_tick(void)      { return (uint32_t)(s_pump_start_us / 1000); }
