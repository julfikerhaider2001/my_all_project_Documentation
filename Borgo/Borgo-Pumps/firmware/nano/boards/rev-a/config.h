#pragma once

#include <Arduino.h>

#define BORGO_HARDWARE_REV "Rev A"

// Nano receives from SIM800L TX on D10 and transmits to SIM800L RX on D11.
const uint8_t SIM800_RX_PIN = 10;
const uint8_t SIM800_TX_PIN = 11;
const uint32_t SIM800_BAUD = 9600;

const uint8_t RELAY_PIN = 7;
const uint8_t RELAY_ACTIVE_LEVEL = HIGH;
const uint8_t RELAY_INACTIVE_LEVEL = LOW;
const uint8_t PAIR_BUTTON_PIN = 4;
const uint8_t STATUS_LED_PIN = LED_BUILTIN;
