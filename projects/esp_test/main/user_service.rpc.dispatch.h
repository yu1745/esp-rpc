/* Auto-generated - do not edit */
#ifndef USER_SERVICE_RPC_DISPATCH_H
#define USER_SERVICE_RPC_DISPATCH_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

int UserService_dispatch(uint16_t method_id, const uint8_t *req_buf, size_t req_len,
                      uint8_t **resp_buf, size_t *resp_len, void *svc_ctx);

#endif