idf_component_register(
    SRCS "src/GatewayApi.cpp" "src/GatewayAssets.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json uart_v1 wifi_manager
)
