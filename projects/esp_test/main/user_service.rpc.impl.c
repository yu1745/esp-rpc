/* Auto-generated - do not edit */
#include "user_service.rpc.h"

/* UserService - 仅 vtable 组装，实现请在 impl_user.c 中编写 */

extern struct User_stream watch_users_impl(void);
extern UserResponse get_user_impl(int id);
extern UserResponse create_user_impl(CreateUserRequest request);
extern UserResponse update_user_impl(int id, CreateUserRequest request);
extern bool delete_user_impl(int id);
extern User_list list_users_impl(int_optional page);

UserService user_service_impl_instance = {
    .WatchUsers = watch_users_impl,
    .GetUser = get_user_impl,
    .CreateUser = create_user_impl,
    .UpdateUser = update_user_impl,
    .DeleteUser = delete_user_impl,
    .ListUsers = list_users_impl,
};