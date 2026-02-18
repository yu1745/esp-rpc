# 若 wifi_config_local.h 不存在则阻止构建，需用户自行创建并填写 WiFi 配置
# 使用 CMAKE_CURRENT_LIST_DIR 而非 CMAKE_CURRENT_SOURCE_DIR，因 ESP-IDF 中本文件会多次参与构建、
# 每次目录上下文可能不同，CMAKE_CURRENT_LIST_DIR 始终指向本 CMakeLists.txt 所在目录
set(WIFI_CONFIG_LOCAL "${CMAKE_CURRENT_LIST_DIR}/wifi_config_local.h")
if(NOT EXISTS "${WIFI_CONFIG_LOCAL}")
    message(FATAL_ERROR "wifi_config_local.h does not exist. Copy wifi_config_local.h.example to wifi_config_local.h and fill in your WiFi credentials.")
endif()

idf_component_register(
    SRCS "wifi_sta.cpp" "main.cpp" "user_service.rpc.gen.cpp" "user_service.rpc.impl_user.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-rpc nvs_flash esp_netif esp_event esp_wifi esp_http_server driver esp_driver_usb_serial_jtag
)
