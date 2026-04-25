idf_component_register(
    SRCS "src/esprpc.c" "src/esprpc_binary.c" "src/transport_ble.c" "src/transport_http_ws.c" "src/transport_serial.c"
    INCLUDE_DIRS "include" "."
    REQUIRES esp_timer esp_http_server bt driver
)
