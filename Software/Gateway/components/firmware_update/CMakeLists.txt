idf_component_register(
    SRCS "src/FirmwareUpdate.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_update driver esp_partition esp_timer uart_v1 wifi_manager
)
