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

/** 收到数据时调用，通常是递归掉哟个 esprpc_handle_request */
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
 * @brief 启动 WebSocket 端点 /ws（在 WiFi 获 IP 后调用）
 * @param httpd_server 可空：NULL 时内部创建并持有 httpd；非 NULL 时使用传入的 httpd_handle_t，仅注册 /ws，由调用方管理服务器生命周期
 */
esp_err_t esprpc_transport_ws_start_server(void *httpd_server);

/**
 * @brief 获取 WebSocket 传输实例
 */
esprpc_transport_t *esprpc_transport_ws_get(void);

/* ---------- BLE 传输 ---------- */

/**
 * @brief 初始化 BLE 传输（创建传输实例，需配合 esprpc_transport_add 使用）
 */
esp_err_t esprpc_transport_ble_init(void);

/**
 * @brief 获取 BLE 传输实例
 */
esprpc_transport_t *esprpc_transport_ble_get(void);

/* ---------- 串口（UART）传输 ---------- */

/**
 * @brief 初始化串口传输（创建传输实例，需配合 esprpc_transport_add 使用）
 *        串口由应用自行管理，esp-rpc 不配置 UART
 */
esp_err_t esprpc_transport_serial_init(void);

/**
 * @brief 获取串口传输实例
 */
esprpc_transport_t *esprpc_transport_serial_get(void);

/**
 * @brief 外部管理串口时：把已去掉前后缀的 RPC 整包交给框架处理
 * @param data 纯 RPC 帧 [1B method_id][2B invoke_id][2B payload_len][payload]
 * @param len  帧长度
 * @note 串口仅外部管理，由应用在识别前后缀后调用
 */
void esprpc_serial_feed_packet(const uint8_t *data, size_t len);

/**
 * @brief 外部管理串口时：把带前后缀的原始包交给框架（内部会按配置剥掉前后缀）
 * @param data 原始包 [prefix][RPC 帧][suffix]
 * @param len  总长度
 */
void esprpc_serial_feed_raw_packet(const uint8_t *data, size_t len);

/** 前后缀最大字节数（用于 get_packet_marker 缓冲区） */
#define ESPRPC_SERIAL_MARKER_MAX 16

/**
 * @brief 获取当前配置的前后缀字节，供应用层串口读任务做帧同步
 * @param prefix_buf    输出前缀，可为 NULL
 * @param prefix_max    prefix_buf 容量
 * @param prefix_len    输出前缀长度
 * @param suffix_buf    输出后缀，可为 NULL
 * @param suffix_max    suffix_buf 容量
 * @param suffix_len    输出后缀长度
 */
void esprpc_serial_get_packet_marker(uint8_t *prefix_buf, size_t prefix_max, size_t *prefix_len,
                                     uint8_t *suffix_buf, size_t suffix_max, size_t *suffix_len);

/**
 * @brief 注册发送回调，框架发送时会带上前缀+数据+后缀
 * @param tx_fn 应用提供的发送函数 (data 含 prefix+frame+suffix)
 * @param ctx   tx_fn 的 user_ctx
 * @return ESP_OK 成功
 */
esp_err_t esprpc_serial_set_tx_cb(void (*tx_fn)(const uint8_t *data, size_t len, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ESPRPC_TRANSPORT_H */
