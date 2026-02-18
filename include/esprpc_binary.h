/**
 * @file esprpc_binary.h
 * @brief 二进制协议序列化/反序列化（可复用，供生成代码调用）
 *
 * 帧格式: [1B method_id][2B invoke_id LE][2B payload_len LE][binary payload]
 * invoke_id: 0=流式, 非0=请求-响应匹配
 * 编码规则: int=4B LE, bool=1B, string=[2B len LE][utf8], optional=[1B tag][value?]
 */

#ifndef ESPRPC_BINARY_H
#define ESPRPC_BINARY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 从 *p 读取 int32，成功时推进 p，返回 0 */
int esprpc_bin_read_i32(const uint8_t **p, const uint8_t *end, int *out);

/** 从 *p 读取 uint32 */
int esprpc_bin_read_u32(const uint8_t **p, const uint8_t *end, uint32_t *out);

/** 从 *p 读取 bool */
int esprpc_bin_read_bool(const uint8_t **p, const uint8_t *end, bool *out);

/** 从 *p 读取 string 到 buf，buf_size 含 NUL */
int esprpc_bin_read_str(const uint8_t **p, const uint8_t *end, char *buf, size_t buf_size);

/** 从 *p 读取 optional 标记 */
int esprpc_bin_read_optional_tag(const uint8_t **p, const uint8_t *end, bool *present);

/** 写入 int32 到 *p */
int esprpc_bin_write_i32(uint8_t **p, const uint8_t *end, int v);

/** 写入 uint32 到 *p */
int esprpc_bin_write_u32(uint8_t **p, const uint8_t *end, uint32_t v);

/** 写入 bool 到 *p */
int esprpc_bin_write_bool(uint8_t **p, const uint8_t *end, bool v);

/** 写入 string 到 *p，s 可为 NULL 视为空串 */
int esprpc_bin_write_str(uint8_t **p, const uint8_t *end, const char *s);

/** 写入 optional 标记 */
int esprpc_bin_write_optional_tag(uint8_t **p, const uint8_t *end, bool present);

#ifdef __cplusplus
}
#endif

#endif /* ESPRPC_BINARY_H */
