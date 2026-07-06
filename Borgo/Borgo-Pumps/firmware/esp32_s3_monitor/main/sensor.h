#pragma once

#include <stdint.h>
#include <stdbool.h>

#define USE_UART_SENSOR    0

#define TRIG_PIN           4
#define ECHO_PIN           5
#define SENSOR_UART_TX     TRIG_PIN
#define SENSOR_UART_RX     ECHO_PIN

#define SENSOR_MIN_CM      2.0f
#define SENSOR_MAX_CM      450.0f
#define MEDIAN_WINDOW      5
#define EMA_ALPHA          0.40f
#define START_CONFIRM      2
#define STOP_CONFIRM       4

#if USE_UART_SENSOR
  #define SENSOR_INTERVAL_MS    250UL
  #define SENSOR_FAIL_LIMIT     8
  #define NO_SENSOR_BOOT_MS     4000UL
  #define UART_BAUD             9600
  #define UART_FRAME_TIMEOUT_MS 1000UL
#else
  #define SENSOR_INTERVAL_MS    500UL
  #define SENSOR_FAIL_LIMIT     4
  #define NO_SENSOR_BOOT_MS     5000UL
#endif

typedef enum {
    SENSOR_STAT_INIT,
    SENSOR_STAT_OK,
    SENSOR_STAT_FAULT,
    SENSOR_STAT_NONE,
} sensor_state_t;

typedef struct {
    float           water_dist_cm;
    float           water_depth_cm;
    float           water_pct;
    bool            sensor_ok;
    sensor_state_t  state;
    uint32_t        boot_ms;
    int             tank_height;
    int             start_pct;
    int             stop_pct;
} sensor_data_t;

void     sensor_init(sensor_data_t *data);
void     sensor_read_and_update(sensor_data_t *data);
void     sensor_set_sensor_state(sensor_data_t *data, sensor_state_t new_state);
const char *sensor_status_text(sensor_state_t s);
uint8_t  sensor_get_below_start_count(void);
uint8_t  sensor_get_above_stop_count(void);
void     sensor_reset_above_stop(void);
void     sensor_reset_counts(void);
