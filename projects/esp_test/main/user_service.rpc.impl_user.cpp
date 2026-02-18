/* 用户实现 - 此文件不会被生成器覆盖，请在此编写业务逻辑 */
#include "user_service.rpc.gen.hpp"
#include "esprpc.h"
#include "esprpc_binary.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

static const char *TAG = "UserService";

#define MAX_USERS  8
#define MAX_NAME   32
#define MAX_EMAIL  64

/* 简单内存存储 */
static struct {
    int id;
    char name[MAX_NAME];
    char email[MAX_EMAIL];
    UserStatus status;
} s_users[MAX_USERS];
static size_t s_user_count = 0;
static int s_next_id = 1;

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = dst_size - 1;
    if (n > 0) {
        strncpy(dst, src, n);
        dst[n] = '\0';
    }
}

/* 序列化单个 User 到 buf，返回写入字节数，失败返回 -1 */
static int serialize_user(const User *u, uint8_t *buf, size_t buf_size)
{
    uint8_t *wp = buf;
    const uint8_t *wend = buf + buf_size;
    if (esprpc_bin_write_i32(&wp, wend, u->id) != 0) return -1;
    if (esprpc_bin_write_str(&wp, wend, u->name ? u->name : "") != 0) return -1;
    if (esprpc_bin_write_optional_tag(&wp, wend, u->email.present) != 0) return -1;
    if (u->email.present && esprpc_bin_write_str(&wp, wend, u->email.value) != 0) return -1;
    if (esprpc_bin_write_i32(&wp, wend, u->status) != 0) return -1;
    /* LIST(string) tags: [4B count][for each: 2B len][bytes] */
    if (esprpc_bin_write_u32(&wp, wend, (uint32_t)u->tags.len) != 0) return -1;
    for (size_t i = 0; i < u->tags.len && u->tags.items; i++) {
        if (esprpc_bin_write_str(&wp, wend, u->tags.items[i] ? u->tags.items[i] : "") != 0) return -1;
    }
    return (int)(wp - buf);
}

rpc_stream<User> watch_users_impl(void)
{
    ESP_LOGI(TAG, "WatchUsers()");
    uint16_t method_id = esprpc_get_stream_method_id();
    if (method_id == ESPRPC_STREAM_METHOD_ID_NONE) {
        return (rpc_stream<User>){ nullptr };
    }
    ESP_LOGI(TAG, "WatchUsers: user count %d", s_user_count);
    uint8_t buf[256];
    for (size_t i = 0; i < s_user_count; i++) {
        User u = {
            s_users[i].id,
            s_users[i].name,
            { true, s_users[i].email },
            s_users[i].status,
            { nullptr, 0 },
        };
        int n = serialize_user(&u, buf, sizeof(buf));
        if (n > 0) {
            esp_err_t err = esprpc_stream_emit(method_id, (const uint8_t *)buf, (size_t)n);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "WatchUsers: stream_emit failed %d", err);
            }
            ESP_LOGI(TAG, "WatchUsers: stream_emit success %d", n);
        }
    }
    return (rpc_stream<User>){ .ctx = NULL };
}

UserResponse get_user_impl(int id)
{
    ESP_LOGI(TAG, "GetUser(id=%d)", id);
    for (size_t i = 0; i < s_user_count; i++) {
        if (s_users[i].id == id) {
            return (UserResponse){
                s_users[i].id,
                s_users[i].name,
                s_users[i].email,
                s_users[i].status,
            };
        }
    }
    return (UserResponse){};
}

UserResponse create_user_impl(CreateUserRequest request)
{
    ESP_LOGI(TAG, "CreateUser(name=%s, email=%s, password=%s)",
             request.name ? request.name : "(null)",
             request.email ? request.email : "(null)",
             request.password.present && request.password.value ? request.password.value : "(null)");
    if (s_user_count >= MAX_USERS) {
        return (UserResponse){};
    }
    size_t i = s_user_count++;
    s_users[i].id = s_next_id++;
    copy_str(s_users[i].name, MAX_NAME, request.name);
    copy_str(s_users[i].email, MAX_EMAIL, request.email);
    s_users[i].status = ACTIVE;
    return (UserResponse){
        s_users[i].id,
        s_users[i].name,
        s_users[i].email,
        s_users[i].status,
    };
}

UserResponse update_user_impl(int id, CreateUserRequest request)
{
    ESP_LOGI(TAG, "UpdateUser(id=%d, name=%s, email=%s)",
             id,
             request.name ? request.name : "(null)",
             request.email ? request.email : "(null)");
    for (size_t i = 0; i < s_user_count; i++) {
        if (s_users[i].id == id) {
            copy_str(s_users[i].name, MAX_NAME, request.name);
            copy_str(s_users[i].email, MAX_EMAIL, request.email);
            return (UserResponse){
                s_users[i].id,
                s_users[i].name,
                s_users[i].email,
                s_users[i].status,
            };
        }
    }
    return (UserResponse){};
}

bool delete_user_impl(int id)
{
    ESP_LOGI(TAG, "DeleteUser(id=%d)", id);
    for (size_t i = 0; i < s_user_count; i++) {
        if (s_users[i].id == id) {
            memmove(&s_users[i], &s_users[i + 1], (s_user_count - 1 - i) * sizeof(s_users[0]));
            s_user_count--;
            return true;
        }
    }
    return false;
}

/* 用于 ListUsers 返回的静态缓冲区（避免动态分配） */
static User s_list_buffer[MAX_USERS];

User_list list_users_impl(int_optional page)
{
    ESP_LOGI(TAG, "ListUsers(page=%s)", page.present ? "present" : "absent");
    if (page.present) {
        ESP_LOGI(TAG, "  page.value=%d", page.value);
    }

    /* 返回当前用户数组的完整列表 */
    for (size_t i = 0; i < s_user_count; i++) {
        s_list_buffer[i] = (User){
            s_users[i].id,
            s_users[i].name,
            { true, s_users[i].email },
            s_users[i].status,
            { nullptr, 0 },
        };
    }
    return (User_list){ s_list_buffer, s_user_count };
}
