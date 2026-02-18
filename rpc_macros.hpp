/**
 * @file rpc_macros.hpp
 * @brief RPC 定义宏（用于 .rpc.hpp 文件）
 *
 * 在 .rpc.hpp 中定义服务、结构体、枚举，供生成器解析并生成
 * .rpc.gen.hpp（声明）和 .rpc.gen.cpp（分发/序列化）。
 *
 * 纯 C++：使用模板 rpc_optional<T>、rpc_list<T>、rpc_map<K,V>
 */

#ifndef RPC_MACROS_HPP
#define RPC_MACROS_HPP

#include <cstddef>
#include <cstdint>

/* ---------- 基础类型 ---------- */
typedef char *string;

/* ---------- C++ 模板 ---------- */
template<typename T>
struct rpc_optional {
    bool present;
    T value;
};

template<typename T>
struct rpc_list {
    T *items;
    size_t len;
};

template<typename K, typename V>
struct rpc_map {
    K *keys;
    V *values;
    size_t len;
};

template<typename T>
struct rpc_stream {
    void *ctx;  /* 流上下文，供 esprpc_stream_emit 等使用 */
};

#define OPTIONAL(type) rpc_optional<type>
#define REQUIRED(type) type
#define LIST(type) rpc_list<type>
#define MAP(key, value) rpc_map<key, value>

/* 类型别名（兼容生成器/impl） */
typedef rpc_optional<char *> string_optional;
typedef rpc_list<char *> string_list;
typedef rpc_map<char *, char *> map_string_string;
typedef rpc_optional<int> int_optional;
typedef rpc_list<int> int_list;

/* ---------- 服务定义 ---------- */
#define RPC_SERVICE(name) typedef struct name {

#define RPC_METHOD(name, ret_type, ...) \
    ret_type (*name)(__VA_ARGS__);

#define RPC_METHOD_EX(name, ret_type, params, options) \
    ret_type (*name)(params); /* options */

#define RPC_SERVICE_END(svc_name) } svc_name;

/* ---------- 类型定义 ---------- */
#define RPC_ALIAS(alias, original) typedef original alias;

#define RPC_ENUM(name, ...) typedef enum name { __VA_ARGS__ } name;

#define RPC_STRUCT(name, ...) typedef struct name { __VA_ARGS__ } name;

#define STREAM(type) rpc_stream<type>

/** 结构体 LIST 类型定义：RPC_LIST_TYPEDEF(User) -> User_list */
#define RPC_LIST_TYPEDEF(T) typedef rpc_list<T> T##_list;

#endif /* RPC_MACROS_HPP */
