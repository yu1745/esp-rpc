/**
 * @file esprpc_binary.c
 * @brief 二进制协议序列化/反序列化实现
 */

#include "esprpc_binary.h"
#include <string.h>

int esprpc_bin_read_i32(const uint8_t **p, const uint8_t *end, int *out)
{
    if (*p + 4 > end) return -1;
    *out = (int)(*p)[0] | ((int)(*p)[1] << 8) | ((int)(*p)[2] << 16) | ((int)(*p)[3] << 24);
    *p += 4;
    return 0;
}

int esprpc_bin_read_u32(const uint8_t **p, const uint8_t *end, uint32_t *out)
{
    if (*p + 4 > end) return -1;
    *out = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) | ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
    *p += 4;
    return 0;
}

int esprpc_bin_read_bool(const uint8_t **p, const uint8_t *end, bool *out)
{
    if (*p + 1 > end) return -1;
    *out = ((*p)[0] != 0);
    *p += 1;
    return 0;
}

int esprpc_bin_read_str(const uint8_t **p, const uint8_t *end, char *buf, size_t buf_size)
{
    if (*p + 2 > end) return -1;
    uint16_t len = (uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8);
    *p += 2;
    if (*p + len > end || len >= buf_size) return -1;
    memcpy(buf, *p, len);
    buf[len] = 0;
    *p += len;
    return 0;
}

int esprpc_bin_read_optional_tag(const uint8_t **p, const uint8_t *end, bool *present)
{
    if (*p + 1 > end) return -1;
    *present = ((*p)[0] != 0);
    *p += 1;
    return 0;
}

int esprpc_bin_write_i32(uint8_t **p, const uint8_t *end, int v)
{
    if (*p + 4 > end) return -1;
    (*p)[0] = (uint8_t)(v & 0xff);
    (*p)[1] = (uint8_t)((v >> 8) & 0xff);
    (*p)[2] = (uint8_t)((v >> 16) & 0xff);
    (*p)[3] = (uint8_t)((v >> 24) & 0xff);
    *p += 4;
    return 0;
}

int esprpc_bin_write_u32(uint8_t **p, const uint8_t *end, uint32_t v)
{
    if (*p + 4 > end) return -1;
    (*p)[0] = (uint8_t)(v & 0xff);
    (*p)[1] = (uint8_t)((v >> 8) & 0xff);
    (*p)[2] = (uint8_t)((v >> 16) & 0xff);
    (*p)[3] = (uint8_t)((v >> 24) & 0xff);
    *p += 4;
    return 0;
}

int esprpc_bin_write_bool(uint8_t **p, const uint8_t *end, bool v)
{
    if (*p + 1 > end) return -1;
    (*p)[0] = v ? 1 : 0;
    *p += 1;
    return 0;
}

int esprpc_bin_write_str(uint8_t **p, const uint8_t *end, const char *s)
{
    if (!s) s = "";
    size_t len = strlen(s);
    if (len > 65535 || *p + 2 + len > end) return -1;
    (*p)[0] = (uint8_t)(len & 0xff);
    (*p)[1] = (uint8_t)((len >> 8) & 0xff);
    memcpy(*p + 2, s, len);
    *p += 2 + len;
    return 0;
}

int esprpc_bin_write_optional_tag(uint8_t **p, const uint8_t *end, bool present)
{
    if (*p + 1 > end) return -1;
    (*p)[0] = present ? 1 : 0;
    *p += 1;
    return 0;
}
