/**
 * @file transport_ble.c
 * @brief BLE GATT 传输层（占位实现）
 *
 * 计划通过 NimBLE GATT 服务收发 RPC 帧，与 WebSocket 传输并列。
 * 当前仅提供 init 占位，待实现 esprpc_transport_t 及 esprpc_transport_ble_get。
 */

#include "esprpc_transport.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "esprpc_ble";

esp_err_t esprpc_transport_ble_init(void)
{
    ESP_LOGI(TAG, "BLE transport init (stub)");
    return ESP_OK;
}
