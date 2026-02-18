/* Auto-generated - do not edit */
#include "user_service.rpc.gen.hpp"
#include "esprpc.h"
#include "esprpc_service.h"
#include "esprpc_binary.h"
#include <cstdlib>
#include <cstring>

static int bin_read_CreateUserRequest(const uint8_t **p, const uint8_t *end, CreateUserRequest *out) {
    memset(out, 0, sizeof(*out));
    {
        static char name_buf[128];
        if (esprpc_bin_read_str(p, end, name_buf, sizeof(name_buf)) != 0) return -1;
        out->name = name_buf;
    }
    {
        static char email_buf[128];
        if (esprpc_bin_read_str(p, end, email_buf, sizeof(email_buf)) != 0) return -1;
        out->email = email_buf;
    }
    {
        static char password_buf[128];
        bool password_present = false;
        if (esprpc_bin_read_optional_tag(p, end, &password_present) != 0) return -1;
        if (password_present) {
            if (esprpc_bin_read_str(p, end, password_buf, sizeof(password_buf)) != 0) return -1;
            out->password.present = true; out->password.value = password_buf;
        } else { out->password.present = false; }
    }
    return 0;
}

/* UserService - 仅 vtable 组装，实现请在 impl_user.cpp 中编写 */

extern UserResponse get_user_impl(int id);
extern UserResponse create_user_impl(CreateUserRequest request);
extern UserResponse update_user_impl(int id, CreateUserRequest request);
extern bool delete_user_impl(int id);
extern User_list list_users_impl(int_optional page);
extern rpc_stream<User> watch_users_impl(void);

UserService user_service_impl_instance = {
    get_user_impl,
    create_user_impl,
    update_user_impl,
    delete_user_impl,
    list_users_impl,
    watch_users_impl,
};
int UserService_dispatch(uint16_t method_id, const uint8_t *req_buf, size_t req_len,
                      uint8_t **resp_buf, size_t *resp_len, void *svc_ctx) {
    UserService *svc = (UserService *)svc_ctx;
    uint8_t mth = method_id & 0x0F;

    if (mth == 0) {
        const uint8_t *p = req_buf;
        const uint8_t *end = req_buf + req_len;
        int id_val = 0;
        if (esprpc_bin_read_i32((const uint8_t **)&p, end, &id_val) != 0) return -1;
        UserResponse r = svc->GetUser(id_val);
        *resp_len = 1024;
        *resp_buf = (uint8_t *)malloc(*resp_len);
        if (!*resp_buf) return -1;
        uint8_t *wp = *resp_buf;
        const uint8_t *wend = *resp_buf + *resp_len;
            if (esprpc_bin_write_i32(&wp, wend, r.id) != 0) return -1;
            if (esprpc_bin_write_str(&wp, wend, r.name ? r.name : "") != 0) return -1;
            if (esprpc_bin_write_str(&wp, wend, r.email ? r.email : "") != 0) return -1;
            if (esprpc_bin_write_i32(&wp, wend, r.status) != 0) return -1;
        *resp_len = (size_t)(wp - *resp_buf);
        return 0;
    }

    if (mth == 1) {
        const uint8_t *p = req_buf;
        const uint8_t *end = req_buf + req_len;
        CreateUserRequest request = {};
        if (bin_read_CreateUserRequest((const uint8_t **)&p, end, &request) != 0) return -1;
        UserResponse r = svc->CreateUser(request);
        *resp_len = 1024;
        *resp_buf = (uint8_t *)malloc(*resp_len);
        if (!*resp_buf) return -1;
        uint8_t *wp = *resp_buf;
        const uint8_t *wend = *resp_buf + *resp_len;
            if (esprpc_bin_write_i32(&wp, wend, r.id) != 0) return -1;
            if (esprpc_bin_write_str(&wp, wend, r.name ? r.name : "") != 0) return -1;
            if (esprpc_bin_write_str(&wp, wend, r.email ? r.email : "") != 0) return -1;
            if (esprpc_bin_write_i32(&wp, wend, r.status) != 0) return -1;
        *resp_len = (size_t)(wp - *resp_buf);
        return 0;
    }

    if (mth == 2) {
        const uint8_t *p = req_buf;
        const uint8_t *end = req_buf + req_len;
        int id_val = 0;
        if (esprpc_bin_read_i32((const uint8_t **)&p, end, &id_val) != 0) return -1;
        CreateUserRequest request = {};
        if (bin_read_CreateUserRequest((const uint8_t **)&p, end, &request) != 0) return -1;
        UserResponse r = svc->UpdateUser(id_val, request);
        *resp_len = 1024;
        *resp_buf = (uint8_t *)malloc(*resp_len);
        if (!*resp_buf) return -1;
        uint8_t *wp = *resp_buf;
        const uint8_t *wend = *resp_buf + *resp_len;
            if (esprpc_bin_write_i32(&wp, wend, r.id) != 0) return -1;
            if (esprpc_bin_write_str(&wp, wend, r.name ? r.name : "") != 0) return -1;
            if (esprpc_bin_write_str(&wp, wend, r.email ? r.email : "") != 0) return -1;
            if (esprpc_bin_write_i32(&wp, wend, r.status) != 0) return -1;
        *resp_len = (size_t)(wp - *resp_buf);
        return 0;
    }

    if (mth == 3) {
        const uint8_t *p = req_buf;
        const uint8_t *end = req_buf + req_len;
        int id_val = 0;
        if (esprpc_bin_read_i32((const uint8_t **)&p, end, &id_val) != 0) return -1;
        bool r = svc->DeleteUser(id_val);
        *resp_len = 1024;
        *resp_buf = (uint8_t *)malloc(*resp_len);
        if (!*resp_buf) return -1;
        uint8_t *wp = *resp_buf;
        const uint8_t *wend = *resp_buf + *resp_len;
        if (esprpc_bin_write_bool(&wp, wend, r) != 0) { free(*resp_buf); *resp_buf = NULL; return -1; }
        *resp_len = (size_t)(wp - *resp_buf);
        return 0;
    }

    if (mth == 4) {
        const uint8_t *p = req_buf;
        const uint8_t *end = req_buf + req_len;
        int_optional page = { false, 0 };
        { bool pr = false; if (esprpc_bin_read_optional_tag((const uint8_t **)&p, end, &pr) != 0) return -1;
          if (pr) { int v = 0; if (esprpc_bin_read_i32((const uint8_t **)&p, end, &v) != 0) return -1;
            page.present = true; page.value = v; } }
        User_list r = svc->ListUsers(page);
        *resp_len = 1024;
        *resp_buf = (uint8_t *)malloc(*resp_len);
        if (!*resp_buf) return -1;
        uint8_t *wp = *resp_buf;
        const uint8_t *wend = *resp_buf + *resp_len;
        if (esprpc_bin_write_u32(&wp, wend, (uint32_t)(r.len)) != 0) { free(*resp_buf); *resp_buf = NULL; return -1; }
        if (r.items && r.len > 0) {
            for (size_t i = 0; i < r.len; i++) {
                    if (esprpc_bin_write_i32(&wp, wend, r.items[i].id) != 0) return -1;
                    if (esprpc_bin_write_str(&wp, wend, r.items[i].name ? r.items[i].name : "") != 0) return -1;
                    if (esprpc_bin_write_optional_tag(&wp, wend, r.items[i].email.present) != 0) return -1;
                    if (r.items[i].email.present && esprpc_bin_write_str(&wp, wend, r.items[i].email.value) != 0) return -1;
                    if (esprpc_bin_write_i32(&wp, wend, r.items[i].status) != 0) return -1;
                    if (esprpc_bin_write_u32(&wp, wend, (uint32_t)(r.items[i].tags.len)) != 0) return -1;
                    if (r.items[i].tags.items && r.items[i].tags.len > 0) {
                        for (size_t j = 0; j < r.items[i].tags.len; j++) {
                            if (esprpc_bin_write_str(&wp, wend, r.items[i].tags.items[j] ? r.items[i].tags.items[j] : "") != 0) return -1;
                        }
                    }
            }
        }
        *resp_len = (size_t)(wp - *resp_buf);
        return 0;
    }

    if (mth == 5) {
        const uint8_t *p = req_buf;
        const uint8_t *end = req_buf + req_len;
        esprpc_set_stream_method_id(method_id);
        rpc_stream<User> r = svc->WatchUsers();
        esprpc_set_stream_method_id(ESPRPC_STREAM_METHOD_ID_NONE);
        (void)r;
        *resp_buf = NULL;
        *resp_len = 0;
        return 0;
    }

    return -1;
}