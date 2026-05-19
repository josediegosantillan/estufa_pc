idf_component_register(
    SRCS
        "main.c"
        "app_state.c"
        "app_config.c"
        "actuators.c"
        "relay_control.c"
        "temperature_control.c"
        "network_services.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_ledc esp_event esp_http_server esp_netif esp_wifi esp_timer mdns nvs_flash
)
