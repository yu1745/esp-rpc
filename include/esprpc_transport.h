/**
 * @file esprpc_transport.h
 * @brief RPC 传输层抽象接口
 *
 * 传输层负责底层收发，框架不关心具体实现（WebSocket/BLE/UART 等）。
 * 实现者需提供 send/start/stop 及 ctx，并在收到数据时调用 on_recv。
 */

#ifndef ESPRPC_TRANSPORT_H
#define ESPRPC_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 收到数据时调用，通常传入 esprpc_handle_request */
typedef void (*esprpc_transport_on_recv_fn)(const uint8_t *data, size_t len, void *user_ctx);

/**
 * @brief 传输层接口
 * 实现者需实现 send/start/stop，start 时保存 on_recv 供收到数据时调用
 */
typedef struct esprpc_transport {
    /** 发送数据 */
    esp_err_t (*send)(void *ctx, const uint8_t *data, size_t len);
    /** 启动传输，注册接收回调 */
    esp_err_t (*start)(void *ctx, esprpc_transport_on_recv_fn on_recv, void *user_ctx);
    /** 停止传输 */
    void (*stop)(void *ctx);
    /** 传输上下文 */
    void *ctx;
} esprpc_transport_t;

/**
 * @brief 添加传输层
 * @param transport 传输实例
 * @return ESP_OK 成功
 */
esp_err_t esprpc_transport_add(esprpc_transport_t *transport);

/**
 * @brief 移除传输层
 * @param transport 传输实例
 */
void esprpc_transport_remove(esprpc_transport_t *transport);

/* ---------- WebSocket 传输 ---------- */

/**
 * @brief 初始化 WebSocket 传输（创建传输实例，需配合 esprpc_transport_add 使用）
 */
esp_err_t esprpc_transport_ws_init(void);

/**
 * @brief 启动 HTTP 服务器及 /ws WebSocket 端点（在 WiFi 获取 IP 后调用）
 */
esp_err_t esprpc_transport_ws_start_server(void);

/**
 * @brief 获取 WebSocket 传输实例
 */
esprpc_transport_t *esprpc_transport_ws_get(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPRPC_TRANSPORT_H */
