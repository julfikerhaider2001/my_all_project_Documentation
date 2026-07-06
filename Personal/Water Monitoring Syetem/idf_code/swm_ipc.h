// ipc/swm_ipc.h
// FreeRTOS IPC primitives: queues, mutexes, event groups

#ifndef SWM_IPC_H
#define SWM_IPC_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include <stdint.h>

// ──────────────────────────────────────────────────────────────
// EVENT GROUP BITS (for cross-task signaling)
// ──────────────────────────────────────────────────────────────
#define EV_WIFI_CONNECTED       (1 << 0)   // WiFi provisioning complete
#define EV_RAINMAKER_READY      (1 << 1)   // RainMaker node claimed & online
#define EV_SENSOR_DATA_READY    (1 << 2)   // New sensor reading available
#define EV_MODE_CHANGED         (1 << 3)   // AUTO/MANUAL mode switch detected
#define EV_PUMP_CONTROL         (1 << 4)   // Pump control event
#define EV_CONFIG_UPDATED       (1 << 5)   // Configuration parameter changed
#define EV_SHUTDOWN             (1 << 6)   // System shutdown signal

// ──────────────────────────────────────────────────────────────
// MESSAGE QUEUE STRUCTURES
// ──────────────────────────────────────────────────────────────

// Sensor reading message
typedef struct {
    float distance_cm;      // Raw ultrasonic reading
    float depth_cm;         // Calculated water depth
    float level_pct;        // Water level percentage
    uint32_t timestamp_ms;
    bool is_valid;
} sensor_msg_t;

// Pump control message
typedef struct {
    bool pump_on;
    uint32_t timestamp_ms;
    const char *reason;     // "AUTO", "MANUAL", "FORCE", "SAFETY"
} pump_msg_t;

// RainMaker parameter update message
typedef struct {
    const char *param_name;
    union {
        float float_val;
        int int_val;
        bool bool_val;
        char *str_val;
    } value;
    int type;               // 0=float, 1=int, 2=bool, 3=string
} rmaker_msg_t;

// ──────────────────────────────────────────────────────────────
// GLOBAL IPC HANDLES
// ──────────────────────────────────────────────────────────────

// Event group for cross-task events
extern EventGroupHandle_t g_event_group;

// Queues for message passing
extern QueueHandle_t g_sensor_queue;        // Sensor → Pump, Indicator
extern QueueHandle_t g_pump_queue;          // Pump controller → others
extern QueueHandle_t g_rmaker_queue;        // Any task → RainMaker task

// Mutexes for shared data protection
extern SemaphoreHandle_t g_state_mutex;     // Protects g_swm_state
extern SemaphoreHandle_t g_config_mutex;    // Protects NVS config

// ──────────────────────────────────────────────────────────────
// IPC INITIALIZATION
// ──────────────────────────────────────────────────────────────
void swm_ipc_init(void);

// ──────────────────────────────────────────────────────────────
// HELPER MACROS FOR MUTEX-PROTECTED STATE ACCESS
// ──────────────────────────────────────────────────────────────

// Acquire state mutex (blocks until available, timeout 1s)
#define STATE_LOCK()    xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(1000))

// Release state mutex
#define STATE_UNLOCK()  xSemaphoreGive(g_state_mutex)

// Acquire config mutex
#define CONFIG_LOCK()   xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(1000))

// Release config mutex
#define CONFIG_UNLOCK() xSemaphoreGive(g_config_mutex)

// ──────────────────────────────────────────────────────────────
// QUEUE OPERATIONS (inline convenience functions)
// ──────────────────────────────────────────────────────────────

// Send sensor data to queue (from sensor task)
static inline bool swm_ipc_send_sensor(const sensor_msg_t *msg, uint32_t timeout_ms) {
    return xQueueSend(g_sensor_queue, msg, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

// Receive sensor data (with timeout)
static inline bool swm_ipc_recv_sensor(sensor_msg_t *msg, uint32_t timeout_ms) {
    return xQueueReceive(g_sensor_queue, msg, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

// Send pump control message
static inline bool swm_ipc_send_pump(const pump_msg_t *msg, uint32_t timeout_ms) {
    return xQueueSend(g_pump_queue, msg, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

// Receive pump message
static inline bool swm_ipc_recv_pump(pump_msg_t *msg, uint32_t timeout_ms) {
    return xQueueReceive(g_pump_queue, msg, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

// Send RainMaker update
static inline bool swm_ipc_send_rmaker(const rmaker_msg_t *msg, uint32_t timeout_ms) {
    return xQueueSend(g_rmaker_queue, msg, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

// ──────────────────────────────────────────────────────────────
// EVENT GROUP OPERATIONS
// ──────────────────────────────────────────────────────────────

// Set event bits
static inline void swm_ipc_set_event(EventBits_t bits) {
    xEventGroupSetBits(g_event_group, bits);
}

// Clear event bits
static inline void swm_ipc_clear_event(EventBits_t bits) {
    xEventGroupClearBits(g_event_group, bits);
}

// Wait for event bits (blocks until set)
static inline EventBits_t swm_ipc_wait_event(EventBits_t bits, uint32_t timeout_ms) {
    return xEventGroupWaitBits(g_event_group, bits, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
}

#endif // SWM_IPC_H
