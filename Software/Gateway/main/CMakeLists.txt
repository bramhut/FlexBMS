idf_component_register(
    SRCS "GatewayMain.cpp"
    REQUIRES status_led uart_v1 wifi_manager gateway_api firmware_update mqtt_client
)
