// drivers/ultrasonic_sensor.h
// HC-SR04 ultrasonic distance sensor driver

#ifndef ULTRASONIC_SENSOR_H
#define ULTRASONIC_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

// ──────────────────────────────────────────────────────────────
// SENSOR DRIVER INITIALIZATION
// ──────────────────────────────────────────────────────────────
void ultrasonic_sensor_init(void);

// ──────────────────────────────────────────────────────────────
// SENSOR READING
// Returns: distance in cm, or -1.0f if read failed
// ──────────────────────────────────────────────────────────────
float ultrasonic_sensor_read(void);

// ──────────────────────────────────────────────────────────────
// HELPER: Calculate water depth and percentage
// Requires: tank height (cm), sensor distance (cm)
// Returns: depth in cm, calculates percentage
// ──────────────────────────────────────────────────────────────
typedef struct {
    float depth_cm;
    float level_pct;
} water_level_t;

water_level_t ultrasonic_calculate_level(float distance_cm, int tank_height_cm);

#endif // ULTRASONIC_SENSOR_H
