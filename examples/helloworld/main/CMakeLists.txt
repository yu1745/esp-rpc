idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-rpc nvs_flash esp_netif esp_event esp_wifi
)
