idf_component_register(
    SRCS "src/WifiManager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_http_server esp_netif esp_wifi nvs_flash
)
