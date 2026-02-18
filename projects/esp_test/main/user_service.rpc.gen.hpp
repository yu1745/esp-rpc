/* Auto-generated - do not edit */
#ifndef USER_SERVICE_RPC_GEN_HPP
#define USER_SERVICE_RPC_GEN_HPP
#include "user_service.rpc.hpp"
#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

int UserService_dispatch(uint16_t method_id, const uint8_t *req_buf, size_t req_len,
                      uint8_t **resp_buf, size_t *resp_len, void *svc_ctx);

extern UserService user_service_impl_instance;

#ifdef __cplusplus
}
#endif

#endif