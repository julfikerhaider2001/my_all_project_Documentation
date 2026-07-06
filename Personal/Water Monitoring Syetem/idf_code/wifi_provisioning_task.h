// tasks/wifi_provisioning_task.h
// WiFi provisioning task header

#ifndef WIFI_PROVISIONING_TASK_H
#define WIFI_PROVISIONING_TASK_H

void wifi_provisioning_task(void *pvParameters);
void wifi_provisioning_task_create(void);

// WiFi event callback for internal use
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

#endif // WIFI_PROVISIONING_TASK_H
