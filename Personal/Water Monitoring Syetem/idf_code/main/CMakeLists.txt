# Main component CMakeLists.txt

idf_component_register(
    SRCS 
        main.c
        tasks/wifi_provisioning_task.c
        tasks/rainmaker_task.c
        tasks/sensor_task.c
        tasks/pump_control_task.c
        tasks/indicator_task.c
        tasks/dpdt_switch_task.c
        drivers/ultrasonic_sensor.c
        drivers/relay_control.c
        drivers/led_control.c
        drivers/buzzer_control.c
        config/swm_config.c
        ipc/swm_ipc.c
    
    INCLUDE_DIRS 
        .
        ./tasks
        ./drivers
        ./config
        ./ipc
    
    REQUIRES 
        freertos
        driver
        esp_wifi
        esp_provisioning
        esp_http_server
        esp_https_server
        nvs_flash
        esp_rainmaker
        esp_event
        esp_netif
        lwip
)

# Enable C++17 for better code quality
target_compile_features(${COMPONENT_LIB} PRIVATE c_std_gnu99)
