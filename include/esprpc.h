/**
 * @file esprpc.h
 * @brief ESP-IDF RPC 框架主头文件
 *
 * 使用流程：
 * 1. esprpc_init()
 * 2. esprpc_register_service() 注册服务
 * 3. esprpc_transport_add() 添加传输层（WebSocket/BLE）
 * 4. transport->start(ctx, esprpc_handle_request, NULL) 设置接收回调
 * 5. 传输层收到数据时调用 esprpc_handle_request() 处理
 */

#ifndef ESPRPC_H
#define ESPRPC_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 收到数据时的回调（传输层收到数据后调用 esprpc_handle_request）
 * @param data 接收到的数据
 * @param len 数据长度
 * @param user_ctx 用户上下文
 */
typedef void (*esprpc_on_recv_fn)(const uint8_t *data, size_t len, void *user_ctx);

/**
 * @brief 初始化 RPC 框架
 * @return ESP_OK 成功
 */
esp_err_t esprpc_init(void);

/**
 * @brief 反初始化 RPC 框架
 */
void esprpc_deinit(void);

/**
 * @brief 注册服务
 * @param name 服务名
 * @param svc_impl 服务实现（函数指针表）
 * @param dispatch_fn 分发函数（由生成器生成）
 * @return ESP_OK 成功
 */
esp_err_t esprpc_register_service(const char *name, void *svc_impl, void *dispatch_fn);

/**
 * @brief 发送 RPC 响应/流数据
 * @param data 数据
 * @param len 长度
 * @return ESP_OK 成功
 */
esp_err_t esprpc_send(const uint8_t *data, size_t len);

/**
 * @brief 设置接收回调（由传输层在收到数据时调用）
 */
void esprpc_set_recv_callback(esprpc_on_recv_fn fn, void *user_ctx);

/**
 * @brief 处理收到的 RPC 请求（由接收回调调用）
 * @param data 完整帧数据
 * @param len 帧长度
 */
void esprpc_handle_request(const uint8_t *data, size_t len);

/** 清除 stream 上下文时使用的 sentinel 值（避免与 method_id 0 冲突） */
#define ESPRPC_STREAM_METHOD_ID_NONE 0xFFFF

/**
 * @brief 设置当前 stream 的 method_id（由 dispatch 在调用 stream 方法前设置）
 * @param method_id 方法 ID，ESPRPC_STREAM_METHOD_ID_NONE 表示清除
 */
void esprpc_set_stream_method_id(uint16_t method_id);

/**
 * @brief 获取当前 stream 的 method_id（由 stream 实现调用以保存到 ctx）
 * @return 当前 method_id，ESPRPC_STREAM_METHOD_ID_NONE 表示非 stream 上下文
 */
uint16_t esprpc_get_stream_method_id(void);

/**
 * @brief 流式推送数据（由服务实现调用）
 * @param method_id 方法 ID（可用 esprpc_get_stream_method_id 获取并保存）
 * @param data payload 数据（不含帧头）
 * @param len 长度
 * @return ESP_OK 成功
 */
esp_err_t esprpc_stream_emit(uint16_t method_id, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ESPRPC_H */
