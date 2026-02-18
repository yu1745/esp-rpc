/**
 * @file main.c
 * @brief ESP-IDF RPC 测试工程
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "esprpc.h"
#include "esprpc_service.h"
#include "esprpc_transport.h"
#include "user_service.rpc.h"
#include "user_service.rpc.dispatch.h"
#include "user_service.rpc.impl.h"
#include "wifi_config_local.h"
#include "wifi_sta.h"

static const char *TAG = "main";
static const char *TAG_WIFI = "main";

/* 传输层接收回调：将数据交给 esprpc 处理（二进制帧） */
static void transport_recv_to_rpc(const uint8_t *data, size_t len, void *user_ctx)
{
    (void)user_ctx;
    esprpc_handle_request(data, len);
}

static void wifi_init_sta(void)
{
    ESP_LOGI(TAG_WIFI, "WiFi init: SSID='%s' (len=%d), auth=WPA/WPA2-PSK",
             WIFI_SSID, (int)strlen(WIFI_SSID));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册 WiFi 事件（获取 IP + 状态更新，合并 handler） */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_event, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,  /* 兼容 WPA/WPA2 */
        },
    };
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASSWORD);

    ESP_LOGI(TAG_WIFI, "Setting config: ssid='%s', passwd_len=%d",
             (char *)wifi_config.sta.ssid, (int)strlen((char *)wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG_WIFI, "WiFi started, connecting to AP...");
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP RPC Test starting");
    ESP_LOGI(TAG_WIFI, "WiFi target: SSID='%s'", WIFI_SSID);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    /* RPC 初始化 */
    esprpc_init();

#if CONFIG_HTTPD_WS_SUPPORT
    /* WebSocket 传输：初始化、注册、启动 */
    esprpc_transport_ws_init();
    esprpc_transport_t *ws = esprpc_transport_ws_get();
    if (ws) {
        esprpc_transport_add(ws);
        ws->start(ws->ctx, transport_recv_to_rpc, NULL);
    }
    /* HTTP 服务器在 WiFi 获取 IP 后启动（见 on_wifi_ip_event） */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &on_wifi_event, NULL));
#endif

#if CONFIG_ESPRPC_ENABLE_BLE
    /* BLE 传输：初始化、注册 */
    esprpc_transport_ble_init();
    esprpc_transport_t *ble = esprpc_transport_ble_get();
    if (ble) {
        esprpc_transport_add(ble);
        ble->start(ble->ctx, transport_recv_to_rpc, NULL);
    }
#endif

    /* 注册 UserService（实现由 generator 生成占位） */
    esprpc_register_service_ex("UserService", &user_service_impl_instance, UserService_dispatch);

    ESP_LOGI(TAG, "RPC ready - WebSocket at ws://<ip>:80/ws when WiFi connected"
#if CONFIG_ESPRPC_ENABLE_BLE
             ", BLE advertising"
#endif
             );
}
