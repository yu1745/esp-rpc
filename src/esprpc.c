/**
 * @file esprpc.c
 * @brief ESP-IDF RPC 框架核心实现
 *
 * 模块职责：
 * - 服务注册与分发：按 method_id 将请求路由到对应服务的 dispatch 函数
 * - 传输层管理：支持多路传输（WebSocket、BLE 等），收到数据后广播到所有传输
 * - 帧格式解析：[1B method_id][2B invoke_id][2B payload_len][N bytes payload]
 *   method_id 高 3 位为服务索引，低 5 位为方法索引
 */

#include "esprpc.h"
#include "esprpc_transport.h"
#include "esprpc_service.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

#ifndef CONFIG_ESPRPC_POOL_BLOCK_SIZE
#define CONFIG_ESPRPC_POOL_BLOCK_SIZE 2048
#endif

static const char *TAG = "esprpc";

#define MAX_TRANSPORTS 4   /* 最大传输层数量 */
#define MAX_SERVICES 8     /* 最大服务数量 */

/** 块头：释放后复用为 free 链表 next */
typedef struct {
    void *next;
} pool_block_t;
#define POOL_HEADER_SIZE ((size_t)sizeof(pool_block_t))

/** 内存池：固定大小块分配，释放的块放入 free 链表复用 */
static pool_block_t *s_pool_free; /* 空闲块链表 */
static SemaphoreHandle_t s_pool_mutex;

static void *pool_malloc(void)
{
    if (xSemaphoreTake(s_pool_mutex, portMAX_DELAY) != pdTRUE) {
        return NULL;
    }

    size_t total = POOL_HEADER_SIZE + (size_t)CONFIG_ESPRPC_POOL_BLOCK_SIZE;
    void *out = NULL;

    /* 从 free 链表取一个块 */
    if (s_pool_free) {
        pool_block_t *b = s_pool_free;
        s_pool_free = (pool_block_t *)b->next;
        out = (char *)b + POOL_HEADER_SIZE;
        goto done;
    }

    /* 向系统申请一个新块 */
    pool_block_t *b = (pool_block_t *)malloc(total);
    if (b) {
        out = (char *)b + POOL_HEADER_SIZE;
    }

done:
    xSemaphoreGive(s_pool_mutex);
    return out;
}

static void pool_free(void *ptr)
{
    if (!ptr) return;
    if (xSemaphoreTake(s_pool_mutex, portMAX_DELAY) != pdTRUE) return;
    pool_block_t *b = (pool_block_t *)((char *)ptr - POOL_HEADER_SIZE);
    b->next = s_pool_free;
    s_pool_free = b;
    xSemaphoreGive(s_pool_mutex);
}

/** 已注册服务条目 */
typedef struct {
    const char *name;           /* 服务名，用于日志 */
    void *impl;                 /* 服务实现（函数指针表） */
    esprpc_dispatch_fn dispatch;/* 分发函数，由 .rpc.dispatch 生成 */
} registered_service_t;

static registered_service_t s_services[MAX_SERVICES];
static int s_service_count = 0;

static esprpc_transport_t *s_transports[MAX_TRANSPORTS];
static int s_transport_count = 0;

/** 可选：通过 esprpc_set_recv_callback 设置的全局接收回调 */
static esprpc_on_recv_fn s_on_recv = NULL;
static void *s_recv_user_ctx = NULL;

/** 当前 stream 的 method_id（dispatch 设置，impl 可读取并保存） */
static uint16_t s_stream_method_id = ESPRPC_STREAM_METHOD_ID_NONE;

/** 传输层统一回调包装（若使用 esprpc_set_recv_callback 时可传入此函数） */
static void transport_recv_cb(const uint8_t *data, size_t len, void *user_ctx)
{
    if (s_on_recv) {
        s_on_recv(data, len, s_recv_user_ctx);
    }
}

esp_err_t esprpc_init(void)
{
    memset(s_services, 0, sizeof(s_services));
    memset(s_transports, 0, sizeof(s_transports));
    s_service_count = 0;
    s_transport_count = 0;
    s_on_recv = NULL;
    s_pool_free = NULL;
    s_pool_mutex = xSemaphoreCreateMutex();
    if (s_pool_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create pool mutex");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "RPC initialized");
    return ESP_OK;
}

void esprpc_deinit(void)
{
    if (s_pool_mutex) {
        vSemaphoreDelete(s_pool_mutex);
        s_pool_mutex = NULL;
    }
    /* 释放 free 链表中的所有块 */
    while (s_pool_free) {
        pool_block_t *b = s_pool_free;
        s_pool_free = (pool_block_t *)b->next;
        free(b);
    }
    s_pool_free = NULL;
    s_service_count = 0;
    s_transport_count = 0;
    s_on_recv = NULL;
}

/* ---------- 服务注册 ---------- */

esp_err_t esprpc_register_service(const char *name, void *svc_impl, void *dispatch_fn)
{
    return esprpc_register_service_ex(name, svc_impl, (esprpc_dispatch_fn)dispatch_fn);
}

esp_err_t esprpc_register_service_ex(const char *name, void *svc_impl,
                                    esprpc_dispatch_fn dispatch_fn)
{
    if (s_service_count >= MAX_SERVICES) {
        ESP_LOGE(TAG, "Max services reached");
        return ESP_ERR_NO_MEM;
    }
    s_services[s_service_count].name = name;
    s_services[s_service_count].impl = svc_impl;
    s_services[s_service_count].dispatch = dispatch_fn;
    s_service_count++;
    ESP_LOGI(TAG, "Registered service: %s", name);
    return ESP_OK;
}

/* ---------- 传输层管理 ---------- */

esp_err_t esprpc_transport_add(esprpc_transport_t *transport)
{
    if (s_transport_count >= MAX_TRANSPORTS) {
        ESP_LOGE(TAG, "Max transports reached");
        return ESP_ERR_NO_MEM;
    }
    s_transports[s_transport_count++] = transport;
    return ESP_OK;
}

void esprpc_transport_remove(esprpc_transport_t *transport)
{
    for (int i = 0; i < s_transport_count; i++) {
        if (s_transports[i] == transport) {
            for (int j = i; j < s_transport_count - 1; j++) {
                s_transports[j] = s_transports[j + 1];
            }
            s_transport_count--;
            break;
        }
    }
}

void esprpc_set_recv_callback(esprpc_on_recv_fn fn, void *user_ctx)
{
    s_on_recv = fn;
    s_recv_user_ctx = user_ctx;
}

/** 向所有已注册传输层广播发送数据（响应、流式推送等） */
esp_err_t esprpc_send(const uint8_t *data, size_t len)
{
    esp_err_t err = ESP_OK;
    for (int i = 0; i < s_transport_count; i++) {
        if (s_transports[i] && s_transports[i]->send) {
            esp_err_t e = s_transports[i]->send(s_transports[i]->ctx, data, len);
            if (e != ESP_OK) err = e;
        }
    }
    return err;
}

void esprpc_set_stream_method_id(uint16_t method_id)
{
    s_stream_method_id = method_id;
}

uint16_t esprpc_get_stream_method_id(void)
{
    return s_stream_method_id;
}

esp_err_t esprpc_stream_emit(uint16_t method_id, const uint8_t *data, size_t len)
{
    if (5 + len > CONFIG_ESPRPC_POOL_BLOCK_SIZE) {
        ESP_LOGE(TAG, "Stream data too large (%zu > %d), drop", 5 + len,
                 (int)CONFIG_ESPRPC_POOL_BLOCK_SIZE);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *frame = (uint8_t *)pool_malloc();
    if (!frame) return ESP_ERR_NO_MEM;
    frame[0] = (uint8_t)(method_id & 0xFF);
    frame[1] = 0;  /* invoke_id = 0 表示流式推送 */
    frame[2] = 0;
    frame[3] = (uint8_t)(len & 0xFF);
    frame[4] = (uint8_t)((len >> 8) & 0xFF);
    memcpy(frame + 5, data, len);
    esp_err_t err = esprpc_send(frame, 5 + len);
    pool_free(frame);
    return err;
}

/* ---------- 请求处理 ---------- */

/**
 * 解析 RPC 帧并分发到对应服务
 * 帧格式: [1B method_id][2B invoke_id LE][2B payload_len LE][N bytes payload]
 * method_id: 高 3 位=服务索引, 低 5 位=方法索引
 * invoke_id: 调用 ID，响应帧回显以匹配并发请求
 */
void esprpc_handle_request(const uint8_t *data, size_t len)
{
    if (len < 5) return;

    uint8_t method_id = data[0];
    uint16_t invoke_id = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    uint16_t payload_len = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
    if (len < 5 + payload_len) return;

    const uint8_t *payload = data + 5;

    uint8_t svc_idx = method_id >> 5;
    uint8_t mth_idx = method_id & 0x1F;

    if (svc_idx < s_service_count && s_services[svc_idx].dispatch) {
        uint8_t *resp_buf = NULL;
        size_t resp_len = 0;
        uint16_t full_id = (uint16_t)(svc_idx << 5) | mth_idx;
        int ret = s_services[svc_idx].dispatch(full_id, payload, payload_len,
                                              &resp_buf, &resp_len,
                                              s_services[svc_idx].impl);
        /* 构造响应帧并广播到所有传输（回显 invoke_id）；使用固定大小池缓冲区 */
        if (ret == 0 && resp_buf && resp_len > 0) {
            size_t frame_len = 5 + resp_len;
            if (frame_len > CONFIG_ESPRPC_POOL_BLOCK_SIZE) {
                ESP_LOGE(TAG, "Response frame too large (%zu > %d), drop", frame_len,
                         (int)CONFIG_ESPRPC_POOL_BLOCK_SIZE);
                free(resp_buf);
            } else {
                uint8_t *frame = (uint8_t *)pool_malloc();
                if (frame) {
                    frame[0] = method_id;
                    frame[1] = invoke_id & 0xFF;
                    frame[2] = (invoke_id >> 8) & 0xFF;
                    frame[3] = resp_len & 0xFF;
                    frame[4] = (resp_len >> 8) & 0xFF;
                    memcpy(frame + 5, resp_buf, resp_len);
                    esprpc_send(frame, frame_len);
                    pool_free(frame);
                } else {
                    ESP_LOGE(TAG, "Failed to alloc response frame buffer");
                }
                free(resp_buf);
            }
        }
    }
}
