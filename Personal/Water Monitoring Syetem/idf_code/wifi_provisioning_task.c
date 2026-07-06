// tasks/wifi_provisioning_task.c
// WiFi provisioning via BLE + credential management

#include "wifi_provisioning_task.h"
#include "swm_config.h"
#include "swm_ipc.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

static const char *TAG = "WIFI_PROV";

// ──────────────────────────────────────────────────────────────
// WiFi EVENT HANDLER
// ──────────────────────────────────────────────────────────────
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started");
        esp_wifi_connect();
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        STATE_LOCK();
        g_swm_state.wifi_connected = false;
        STATE_UNLOCK();
        swm_ipc_clear_event(EV_WIFI_CONNECTED);
        
        esp_wifi_connect();  // Retry connection
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        STATE_LOCK();
        g_swm_state.wifi_connected = true;
        STATE_UNLOCK();
        swm_ipc_set_event(EV_WIFI_CONNECTED);
    }
}

// ──────────────────────────────────────────────────────────────
// PROVISIONING SUCCESS/FAILURE CALLBACKS
// ──────────────────────────────────────────────────────────────
static void prov_event_handler(void *user_data, wifi_prov_sta_fail_reason_t *reason) {
    if (reason == NULL) {
        ESP_LOGI(TAG, "Provisioning successful!");
        return;
    }
    ESP_LOGE(TAG, "Provisioning failed! Reason: %d", *reason);
}

// ──────────────────────────────────────────────────────────────
// WiFi PROVISIONING TASK
// ──────────────────────────────────────────────────────────────
void wifi_provisioning_task(void *pvParameters) {
    ESP_LOGI(TAG, "WiFi provisioning task started");
    
    // Register WiFi event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    
    // Initialize provisioning manager
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = {
            .event_cb = prov_event_handler,
            .user_data = NULL
        }
    };
    
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
    
    // Check if WiFi is already provisioned
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    
    if (!provisioned) {
        // Start BLE provisioning
        ESP_LOGI(TAG, "Starting BLE provisioning...");
        
        // Create BLE transport config
        wifi_prov_scheme_ble_set_service_uuid(
            (uint8_t []){0xb4, 0xdf, 0x5a, 0x7d, 0xfe, 0x48, 0x12, 0xf1,
                         0x15, 0x53, 0x82, 0x73, 0xc2, 0xd7, 0xa0, 0xa7}
        );
        
        // Start provisioning
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_1,
            (const char *)RMAKER_POP,
            RMAKER_SERVICE_NAME,
            NULL
        ));
        
        // Print BLE QR code or provisioning info
        wifi_prov_print_qr(RMAKER_SERVICE_NAME, (const char *)RMAKER_POP, WIFI_PROV_SCHEME_BLE);
        
        ESP_LOGI(TAG, "BLE Provisioning started. Waiting for app...");
    } else {
        // Already provisioned, start WiFi connection
        ESP_LOGI(TAG, "Device already provisioned, connecting to WiFi...");
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    
    // Wait for provisioning to complete
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Check if still provisioning
        bool provisioning_done = false;
        
        // If provisioning is done, clean up and continue
        if (provisioned || provisioning_done) {
            // Stop the provisioning manager
            wifi_prov_mgr_wait();
            ESP_LOGI(TAG, "Provisioning completed, starting WiFi...");
            ESP_ERROR_CHECK(esp_wifi_start());
            
            STATE_LOCK();
            g_swm_state.sys_state = SYS_STATE_NORMAL;
            STATE_UNLOCK();
            
            break;
        }
    }
    
    // Provisioning done - stay connected and reconnect on disconnect
    ESP_LOGI(TAG, "WiFi provisioning task complete, staying online");
    
    // Monitor WiFi connection
    while (1) {
        STATE_LOCK();
        bool wifi_ok = g_swm_state.wifi_connected;
        STATE_UNLOCK();
        
        if (!wifi_ok) {
            // Try to reconnect
            ESP_LOGW(TAG, "WiFi lost, attempting reconnect...");
            esp_wifi_connect();
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    vTaskDelete(NULL);
}

// ──────────────────────────────────────────────────────────────
// TASK CREATION
// ──────────────────────────────────────────────────────────────
void wifi_provisioning_task_create(void) {
    xTaskCreatePinnedToCore(
        wifi_provisioning_task,
        "WiFiProvTask",
        TASK_STACK_WIFI,
        NULL,
        TASK_PRIO_WIFI,
        NULL,
        1  // Core 1 for network tasks
    );
    ESP_LOGI(TAG, "WiFi provisioning task created");
}
