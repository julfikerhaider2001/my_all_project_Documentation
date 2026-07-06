// ipc/swm_ipc.c
// FreeRTOS IPC initialization and implementation

#include "swm_ipc.h"
#include "esp_log.h"

static const char *TAG = "SWM_IPC";

// ──────────────────────────────────────────────────────────────
// GLOBAL HANDLES
// ──────────────────────────────────────────────────────────────
EventGroupHandle_t g_event_group = NULL;

QueueHandle_t g_sensor_queue = NULL;
QueueHandle_t g_pump_queue = NULL;
QueueHandle_t g_rmaker_queue = NULL;

SemaphoreHandle_t g_state_mutex = NULL;
SemaphoreHandle_t g_config_mutex = NULL;

// ──────────────────────────────────────────────────────────────
// INITIALIZATION
// ──────────────────────────────────────────────────────────────
void swm_ipc_init(void) {
    ESP_LOGI(TAG, "Initializing IPC primitives...");
    
    // Create event group
    g_event_group = xEventGroupCreate();
    if (!g_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }
    
    // Create message queues (fixed sizes)
    // Sensor queue: 10 messages max
    g_sensor_queue = xQueueCreate(10, sizeof(sensor_msg_t));
    if (!g_sensor_queue) {
        ESP_LOGE(TAG, "Failed to create sensor queue");
        return;
    }
    
    // Pump queue: 5 messages max
    g_pump_queue = xQueueCreate(5, sizeof(pump_msg_t));
    if (!g_pump_queue) {
        ESP_LOGE(TAG, "Failed to create pump queue");
        return;
    }
    
    // RainMaker queue: 20 messages max (may accumulate if cloud slow)
    g_rmaker_queue = xQueueCreate(20, sizeof(rmaker_msg_t));
    if (!g_rmaker_queue) {
        ESP_LOGE(TAG, "Failed to create RainMaker queue");
        return;
    }
    
    // Create binary semaphores (mutexes)
    g_state_mutex = xSemaphoreCreateMutex();
    if (!g_state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return;
    }
    
    g_config_mutex = xSemaphoreCreateMutex();
    if (!g_config_mutex) {
        ESP_LOGE(TAG, "Failed to create config mutex");
        return;
    }
    
    ESP_LOGI(TAG, "IPC initialized successfully");
}
