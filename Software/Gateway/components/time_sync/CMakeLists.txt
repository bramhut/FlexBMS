idf_component_register(
    SRCS "src/TimeSync.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_timer
)
