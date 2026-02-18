/**
 * @file esprpc_service.h
 * @brief RPC 服务注册 API（内部使用）
 *
 * esprpc_register_service 为对外接口；
 * esprpc_register_service_ex 接受强类型 dispatch_fn，供生成代码调用。
 */

#ifndef ESPRPC_SERVICE_H
#define ESPRPC_SERVICE_H

#include "esprpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 服务分发函数类型（由 .rpc.dispatch 生成器生成）
 * @param method_id 方法 ID
 * @param req_buf 请求数据
 * @param req_len 请求长度
 * @param resp_buf 输出：响应数据（调用方负责释放）
 * @param resp_len 输出：响应长度
 * @param svc_ctx 服务实现上下文
 * @return ESP_OK 成功
 */
typedef int (*esprpc_dispatch_fn)(uint16_t method_id, const uint8_t *req_buf, size_t req_len,
                                  uint8_t **resp_buf, size_t *resp_len, void *svc_ctx);

/**
 * @brief 注册服务（扩展版）
 */
esp_err_t esprpc_register_service_ex(const char *name, void *svc_impl,
                                    esprpc_dispatch_fn dispatch_fn);

#ifdef __cplusplus
}
#endif

#endif /* ESPRPC_SERVICE_H */
