/**
 * @file user_service.rpc.h
 * @brief UserService RPC 定义（与 example 同步）
 */

#ifndef USER_SERVICE_RPC_H
#define USER_SERVICE_RPC_H

#include "rpc_macros.h"

RPC_ENUM(UserStatus, ACTIVE = 1, INACTIVE = 2, DELETED = 3)

RPC_STRUCT(User,
    int id;
    REQUIRED(string) name;
    OPTIONAL(string) email;
    UserStatus status;
    LIST(string) tags;
    MAP(string, string) metadata;
)

/* LIST(User) 展开为 User_list */
typedef struct { User *items; size_t len; } User_list;

/* STREAM(User) 展开为 struct User_stream - 流式句柄 */
struct User_stream {
    void *ctx;  /* 流上下文，供 esprpc_stream_emit 等使用 */
};

RPC_STRUCT(CreateUserRequest,
    REQUIRED(string) name;
    REQUIRED(string) email;
    OPTIONAL(string) password;
)

RPC_STRUCT(UserResponse,
    int id;
    string name;
    string email;
    UserStatus status;
)

RPC_SERVICE(UserService)
    RPC_METHOD(GetUser, UserResponse, int id)
    RPC_METHOD(CreateUser, UserResponse, CreateUserRequest request)
    RPC_METHOD(UpdateUser, UserResponse, int id, CreateUserRequest request)
    RPC_METHOD(DeleteUser, bool, int id)
    RPC_METHOD_EX(ListUsers, LIST(User), OPTIONAL(int) page, "timeout:5000")
    RPC_METHOD(WatchUsers, STREAM(User), void)
RPC_SERVICE_END(UserService)

#endif /* USER_SERVICE_RPC_H */
