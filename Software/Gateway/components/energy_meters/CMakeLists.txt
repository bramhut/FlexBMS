idf_component_register(
    SRCS "src/EnergyMeters.cpp" "src/MeterJson.cpp" "src/MeterPublisher.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_uart esp_driver_gpio esp_timer nvs_flash json esp-modbus
)
