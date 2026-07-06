#include "sensor.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "sensor";

#if USE_UART_SENSOR
#include "driver/uart.h"
#endif

static float   s_med_buf[MEDIAN_WINDOW];
static uint8_t s_med_count = 0;
static uint8_t s_med_head  = 0;

static void median_reset(void)
{
    s_med_count = 0;
    s_med_head  = 0;
}

static void median_push(float v)
{
    s_med_buf[s_med_head] = v;
    s_med_head = (s_med_head + 1) % MEDIAN_WINDOW;
    if (s_med_count < MEDIAN_WINDOW) s_med_count++;
}

static float median_get(void)
{
    float tmp[MEDIAN_WINDOW];
    for (uint8_t i = 0; i < s_med_count; i++) tmp[i] = s_med_buf[i];
    for (uint8_t i = 1; i < s_med_count; i++) {
        float key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }
    return tmp[s_med_count / 2];
}

static float s_ema_cm = -1.0f;

static uint8_t s_below_start_count = 0;
static uint8_t s_above_stop_count  = 0;

static bool  s_sensor_ever_seen = false;
static uint8_t s_sensor_fails   = 0;

static int64_t pulse_in_high_us(gpio_num_t pin, uint32_t timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) == 0) {
        if ((uint32_t)(esp_timer_get_time() - start) > timeout_us) return 0;
    }
    int64_t pulse_start = esp_timer_get_time();
    while (gpio_get_level(pin) == 1) {
        if ((uint32_t)(esp_timer_get_time() - pulse_start) > timeout_us) return 0;
    }
    return esp_timer_get_time() - pulse_start;
}

static float sensor_read_raw_cm(void)
{
    float total = 0;
    int valid = 0;

    for (int i = 0; i < 3; i++) {
        gpio_set_level(TRIG_PIN, 0);
        ets_delay_us(5);
        gpio_set_level(TRIG_PIN, 1);
        ets_delay_us(10);
        gpio_set_level(TRIG_PIN, 0);

        long dur = pulse_in_high_us(ECHO_PIN, 23200);
        if (dur > 0) {
            float d = dur * 0.01715f;
            if (d >= SENSOR_MIN_CM && d <= SENSOR_MAX_CM) {
                total += d;
                valid++;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return (valid == 0) ? -1.0f : total / valid;
}

#if USE_UART_SENSOR
static uint8_t  s_uart_buf[4];
static uint8_t  s_uart_idx = 0;
static float    s_uart_last_cm = -1.0f;
static int64_t  s_uart_last_frame_us = 0;

static void poll_sensor_uart(void)
{
    uint8_t byte;
    while (uart_read_bytes(UART_NUM_1, &byte, 1, 0) == 1) {
        if (s_uart_idx == 0) {
            if (byte == 0xFF) s_uart_buf[s_uart_idx++] = byte;
        } else {
            s_uart_buf[s_uart_idx++] = byte;
            if (s_uart_idx >= 4) {
                s_uart_idx = 0;
                uint8_t sum = (s_uart_buf[0] + s_uart_buf[1] + s_uart_buf[2]) & 0xFF;
                if (sum == s_uart_buf[3]) {
                    uint16_t mm = ((uint16_t)s_uart_buf[1] << 8) | s_uart_buf[2];
                    float cm = mm / 10.0f;
                    if (cm >= SENSOR_MIN_CM && cm <= SENSOR_MAX_CM) {
                        s_uart_last_cm = cm;
                        s_uart_last_frame_us = esp_timer_get_time();
                    }
                }
            }
        }
    }
}

static float uart_read_raw_cm(void)
{
    poll_sensor_uart();
    if (s_uart_last_cm > 0 &&
        (uint32_t)((esp_timer_get_time() - s_uart_last_frame_us) / 1000) < UART_FRAME_TIMEOUT_MS) {
        return s_uart_last_cm;
    }
    return -1.0f;
}
#endif

void sensor_init(sensor_data_t *data)
{
    memset(data, 0, sizeof(*data));
    data->boot_ms = (uint32_t)(esp_timer_get_time() / 1000);
    data->state   = SENSOR_STAT_INIT;

    gpio_config_t trig_cfg = {
        .pin_bit_mask = (1ULL << TRIG_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&trig_cfg);

    gpio_config_t echo_cfg = {
        .pin_bit_mask = (1ULL << ECHO_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&echo_cfg);

#if USE_UART_SENSOR
    uart_config_t uart_cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_cfg);
    uart_set_pin(UART_NUM_1, SENSOR_UART_TX, SENSOR_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#endif

    ESP_LOGI(TAG, "Initialized (HC-SR04 trig=%d echo=%d)", TRIG_PIN, ECHO_PIN);
}

const char *sensor_status_text(sensor_state_t s)
{
    switch (s) {
        case SENSOR_STAT_OK:    return "OK";
        case SENSOR_STAT_FAULT: return "Fault";
        case SENSOR_STAT_NONE:  return "No Sensor";
        default:                return "Starting";
    }
}

void sensor_set_sensor_state(sensor_data_t *data, sensor_state_t new_state)
{
    if (new_state == data->state) return;
    data->state  = new_state;
    data->sensor_ok = (new_state == SENSOR_STAT_OK);
    ESP_LOGI(TAG, "state -> %s", sensor_status_text(new_state));
}

void sensor_read_and_update(sensor_data_t *data)
{
#if USE_UART_SENSOR
    float raw = uart_read_raw_cm();
#else
    float raw = sensor_read_raw_cm();
#endif

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (raw < 0) {
        if (s_sensor_fails < 250) s_sensor_fails++;

        if (!s_sensor_ever_seen) {
            if ((now_ms - data->boot_ms) > NO_SENSOR_BOOT_MS &&
                data->state != SENSOR_STAT_NONE) {
                sensor_set_sensor_state(data, SENSOR_STAT_NONE);
            }
        } else {
            if (s_sensor_fails >= SENSOR_FAIL_LIMIT && data->state != SENSOR_STAT_FAULT) {
                sensor_set_sensor_state(data, SENSOR_STAT_FAULT);
            }
        }
        return;
    }

    bool recovering = (data->state != SENSOR_STAT_OK);
    s_sensor_ever_seen = true;
    s_sensor_fails     = 0;

    if (recovering) {
        median_reset();
        s_ema_cm            = -1.0f;
        s_below_start_count = 0;
        s_above_stop_count  = 0;
    }

    median_push(raw);
    float m = median_get();
    if (s_ema_cm < 0) s_ema_cm = m;
    else              s_ema_cm += EMA_ALPHA * (m - s_ema_cm);

    data->water_dist_cm  = s_ema_cm;
    data->water_depth_cm = fmaxf(0.0f, (float)data->tank_height - s_ema_cm);
    data->water_pct      = fminf(100.0f, fmaxf(0.0f, data->water_depth_cm / data->tank_height * 100.0f));

    ESP_LOGI(TAG, "raw=%.1f  med=%.1f  smooth=%.1f  level=%.1f%%",
             raw, m, s_ema_cm, data->water_pct);

    sensor_set_sensor_state(data, SENSOR_STAT_OK);

    if (data->water_pct < (float)data->start_pct) {
        if (s_below_start_count < 250) s_below_start_count++;
    } else {
        s_below_start_count = 0;
    }
    if (data->water_pct >= (float)data->stop_pct) {
        if (s_above_stop_count < 250) s_above_stop_count++;
    } else {
        s_above_stop_count = 0;
    }
}

uint8_t sensor_get_below_start_count(void) { return s_below_start_count; }
uint8_t sensor_get_above_stop_count(void)  { return s_above_stop_count; }
void    sensor_reset_above_stop(void)      { s_above_stop_count = 0; }
void    sensor_reset_counts(void)          { s_below_start_count = 0; s_above_stop_count = 0; }
