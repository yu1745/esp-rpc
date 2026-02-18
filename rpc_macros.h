/**
 * @file rpc_macros.h
 * @brief RPC 定义宏（用于 .rpc.h 文件）
 *
 * 在 .rpc.h 中定义服务、结构体、枚举，供生成器解析并生成
 * .rpc.c（实现骨架）和 .rpc.dispatch.h（分发逻辑）。
 */

#ifndef RPC_MACROS_H
#define RPC_MACROS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------- 基础类型 ---------- */
typedef char *string;

/** OPTIONAL/LIST/MAP 展开后的运行时类型 */
typedef struct { char *value; bool present; } string_optional;
typedef struct { char **items; size_t len; } string_list;
typedef struct { char **keys; char **values; size_t len; } map_string_string;
typedef struct { int value; bool present; } int_optional;

/* ---------- 服务定义 ---------- */
/** 开始服务定义，生成 struct name { ... } name; */
#define RPC_SERVICE(name) typedef struct name {

/** 普通方法：ret_type (*name)(params); */
#define RPC_METHOD(name, ret_type, ...) \
    ret_type (*name)(__VA_ARGS__);

/** 带选项的方法（timeout 等，仅作元数据） */
#define RPC_METHOD_EX(name, ret_type, params, options) \
    ret_type (*name)(params); /* options */

/** 结束服务定义，svc_name 需与 RPC_SERVICE 一致 */
#define RPC_SERVICE_END(svc_name) } svc_name;

/* ---------- 类型定义 ---------- */
#define RPC_ALIAS(alias, original) typedef original alias;

/** 枚举：typedef enum name { ... } name; */
#define RPC_ENUM(name, ...) typedef enum name { __VA_ARGS__ } name;

/** 结构体：typedef struct name { ... } name; */
#define RPC_STRUCT(name, ...) typedef struct name { __VA_ARGS__ } name;

/* ---------- 类型修饰（生成器解析用） ---------- */
#define OPTIONAL(type) type##_optional
#define REQUIRED(type) type
#define LIST(type) type##_list
#define MAP(key, value) map_##key##_##value

/** 流式返回：struct type##_stream */
#define STREAM(type) struct type##_stream

#endif /* RPC_MACROS_H */
