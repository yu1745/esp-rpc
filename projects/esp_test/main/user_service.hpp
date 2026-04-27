#include "esprpc.hpp"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

/* ---------- 数据结构 ---------- */

enum UserStatus { ACTIVE = 1, INACTIVE = 2, DELETED = 3 };

template <> struct esprpc::Serializer<UserStatus> {
    static void write(esprpc::Buffer &buf, UserStatus v) { buf.write_i32((int32_t)v); }
    static void read(esprpc::Reader &r, UserStatus &v) { int32_t tmp = r.read_i32(); v = (UserStatus)tmp; }
};

ESPRPC_STRUCT(User,
    (id, int32_t),
    (name, esprpc::StringBuf),
    (email, esprpc::Optional<esprpc::StringBuf>),
    (status, UserStatus)
)

ESPRPC_STRUCT(CreateUserRequest,
    (name, esprpc::StringBuf),
    (email, esprpc::Optional<esprpc::StringBuf>),
    (password, esprpc::Optional<esprpc::StringBuf>)
)

ESPRPC_STRUCT(UserResponse,
    (id, int32_t),
    (name, esprpc::StringBuf),
    (email, esprpc::StringBuf),
    (status, UserStatus)
)

/* ---------- 服务实现 ---------- */

static User s_users[16];
static size_t s_user_count = 0;

class UserService {
public:
    UserResponse GetUser(int32_t id) {
        UserResponse resp{};
        for (size_t i = 0; i < s_user_count; i++) {
            if (s_users[i].id == id) {
                resp.id = s_users[i].id;
                resp.name = s_users[i].name;
                if (s_users[i].email.present)
                    resp.email = s_users[i].email.value;
                resp.status = s_users[i].status;
                return resp;
            }
        }
        resp.name = "not_found";
        return resp;
    }

    UserResponse CreateUser(CreateUserRequest req) {
        UserResponse resp{};
        if (s_user_count >= 16) return resp;
        int32_t id = (int32_t)(s_user_count + 1);
        s_users[s_user_count].id = id;
        s_users[s_user_count].name = req.name;
        if (req.email.present)
            s_users[s_user_count].email = req.email.value;
        s_users[s_user_count].status = ACTIVE;
        s_user_count++;
        resp.id = id;
        resp.name = req.name;
        if (req.email.present)
            resp.email = req.email.value;
        resp.status = ACTIVE;
        ESP_LOGI("UserService", "Created user id=%d", id);
        return resp;
    }

    void CreateUserV2(CreateUserRequest req) {
        CreateUser(req);
    }

    UserResponse UpdateUser(int32_t id, CreateUserRequest req) {
        UserResponse resp{};
        for (size_t i = 0; i < s_user_count; i++) {
            if (s_users[i].id == id) {
                s_users[i].name = req.name;
                if (req.email.present)
                    s_users[i].email = req.email.value;
                resp.id = id;
                resp.name = req.name;
                if (req.email.present)
                    resp.email = req.email.value;
                resp.status = s_users[i].status;
                return resp;
            }
        }
        return resp;
    }

    bool DeleteUser(int32_t id) {
        for (size_t i = 0; i < s_user_count; i++) {
            if (s_users[i].id == id) {
                memmove(&s_users[i], &s_users[i + 1],
                        (s_user_count - i - 1) * sizeof(User));
                s_user_count--;
                ESP_LOGI("UserService", "Deleted user id=%d", id);
                return true;
            }
        }
        return false;
    }

    esprpc::List<User> ListUsers(esprpc::Optional<int32_t> page) {
        (void)page;
        return {s_users, s_user_count};
    }

    rpc_stream<User> WatchUsers() {
        uint16_t mid = esprpc_get_stream_method_id();
        if (mid == ESPRPC_STREAM_METHOD_ID_NONE) return {nullptr};
        for (size_t i = 0; i < s_user_count; i++) {
            esprpc::stream_emit(mid, s_users[i]);
        }
        return {nullptr};
    }

    void Ping() {
        ESP_LOGI("UserService", "pong");
    }
};

static UserService s_user_service;

ESPRPC_SERVICE(UserService, s_user_service,
    // @rpc GetUser(int32_t id) -> UserResponse
    esprpc::method<UserService, &UserService::GetUser>(0),
    // @rpc CreateUser(CreateUserRequest req) -> UserResponse
    esprpc::method<UserService, &UserService::CreateUser>(1),
    // @rpc CreateUserV2(CreateUserRequest req) -> void
    esprpc::method<UserService, &UserService::CreateUserV2>(2, esprpc::MF_VOID),
    // @rpc UpdateUser(int32_t id, CreateUserRequest req) -> UserResponse
    esprpc::method<UserService, &UserService::UpdateUser>(3),
    // @rpc DeleteUser(int32_t id) -> bool
    esprpc::method<UserService, &UserService::DeleteUser>(4),
    // @rpc ListUsers(esprpc::Optional<int32_t> page) -> esprpc::List<User>
    esprpc::method<UserService, &UserService::ListUsers>(5),
    // @rpc WatchUsers() -> stream<User>
    esprpc::method<UserService, &UserService::WatchUsers>(6, esprpc::MF_STREAM),
    // @rpc Ping() -> void
    esprpc::method<UserService, &UserService::Ping>(7, esprpc::MF_VOID)
)
