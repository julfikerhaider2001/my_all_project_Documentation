// drivers/relay_control.h
// Relay control for pump

#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdbool.h>

// ──────────────────────────────────────────────────────────────
// RELAY INITIALIZATION
// ──────────────────────────────────────────────────────────────
void relay_init(void);

// ──────────────────────────────────────────────────────────────
// RELAY CONTROL
// pump_on = true  → Enable pump (energize relay)
// pump_on = false → Disable pump (de-energize relay)
// ──────────────────────────────────────────────────────────────
void relay_set(bool pump_on);

// Get current relay state
bool relay_get_state(void);

#endif // RELAY_CONTROL_H
