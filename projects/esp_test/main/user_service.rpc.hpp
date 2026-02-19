/**
 * @file user_service.rpc.hpp
 * @brief UserService RPC 定义（与 example 同步）
 */

#ifndef USER_SERVICE_RPC_HPP
#define USER_SERVICE_RPC_HPP

#include "rpc_macros.hpp"

RPC_ENUM(UserStatus, ACTIVE = 1, INACTIVE = 2, DELETED = 3)

RPC_STRUCT(User,
    int id;
    REQUIRED(string) name;
    OPTIONAL(string) email;
    UserStatus status;
    LIST(string) tags;
)

RPC_LIST_TYPEDEF(User)

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
    RPC_METHOD(CreateUserV2, VOID, CreateUserRequest request)
    RPC_METHOD(UpdateUser, UserResponse, int id, CreateUserRequest request)
    RPC_METHOD(DeleteUser, bool, int id)
    RPC_METHOD_EX(ListUsers, LIST(User), OPTIONAL(int) page, "timeout:5000")
    RPC_METHOD(WatchUsers, STREAM(User), void)
    RPC_METHOD(Ping, VOID, void)
RPC_SERVICE_END(UserService)

#endif /* USER_SERVICE_RPC_HPP */
