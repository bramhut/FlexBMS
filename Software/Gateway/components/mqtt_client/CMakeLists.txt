idf_component_register(
    SRCS "src/MqttClient.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_timer json mqtt nvs_flash uart_v1 wifi_manager
)
