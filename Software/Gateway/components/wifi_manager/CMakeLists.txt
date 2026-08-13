idf_component_register(
    SRCS "src/WifiManager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_netif esp_timer esp_wifi lwip mdns nvs_flash
)
