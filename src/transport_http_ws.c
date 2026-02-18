/**
 * @file transport_http_ws.c
 * @brief WebSocket 传输层（基于 esp_http_server）
 *
 * 依赖 CONFIG_HTTPD_WS_SUPPORT。流程：
 * 1. esprpc_transport_ws_init()
 * 2. esprpc_transport_add(esprpc_transport_ws_get())
 * 3. transport->start(ctx, esprpc_handle_request, NULL)
 * 4. WiFi 获 IP 后调用 esprpc_transport_ws_start_server(NULL) 或传入已有 httpd
 * 端点: ws://<ip>:80/ws；传入非空 httpd 时与调用方共用同一服务器
 */

#include "esprpc_transport.h"
#include "esprpc.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "esprpc_ws";

#if CONFIG_HTTPD_WS_SUPPORT

/** WebSocket 传输上下文 */
typedef struct {
    httpd_handle_t server;
    bool server_owned;  /* true=内部创建需负责 stop，false=外部传入不 stop */
    int sockfd;  /* 当前连接的客户端 fd，-1 表示无连接 */
    httpd_req_t *current_req;  /* handler 内当前请求，用于同步发送（避免 httpd_queue_work 死锁） */
    esprpc_transport_on_recv_fn on_recv;
    void *on_recv_ctx;
} ws_ctx_t;

static ws_ctx_t s_ws_ctx = {0};

static void ws_send_complete_cb(esp_err_t err, int socket, void *arg)
{
    (void)err;
    (void)socket;
    free(arg);
}

/** 通过 WebSocket 发送二进制帧
 * 在 handler 内调用时用 current_req 直接同步发送；否则用 async（handler 外调用时 current_req 为 NULL） */
static esp_err_t ws_send(void *ctx, const uint8_t *data, size_t len)
{
    ws_ctx_t *wc = (ws_ctx_t *)ctx;
    if (!wc || wc->sockfd < 0 || !wc->server) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = (uint8_t *)data,
        .len = len,
    };

    /* handler 内：直接同步发送，避免 httpd_queue_work 导致同任务死锁 */
    if (wc->current_req) {
        return httpd_ws_send_frame(wc->current_req, &frame);
    }

    /* handler 外（如流式推送）：异步发送 */
    uint8_t *buf = malloc(len);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(buf, data, len);
    frame.payload = buf;
    esp_err_t ret = httpd_ws_send_data_async(wc->server, wc->sockfd, &frame,
                                            ws_send_complete_cb, buf);
    if (ret != ESP_OK) {
        free(buf);
    }
    return ret;
}

/** 保存接收回调，收到二进制帧时调用 */
static esp_err_t ws_start(void *ctx, esprpc_transport_on_recv_fn on_recv, void *user_ctx)
{
    ws_ctx_t *wc = (ws_ctx_t *)ctx;
    if (!wc) return ESP_ERR_INVALID_ARG;
    wc->on_recv = on_recv;
    wc->on_recv_ctx = user_ctx;
    return ESP_OK;
}

static void ws_stop(void *ctx)
{
    ws_ctx_t *wc = (ws_ctx_t *)ctx;
    if (wc) {
        wc->sockfd = -1;
        wc->on_recv = NULL;
    }
}

/** /ws URI 处理：GET=握手，后续=二进制帧收发 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    ws_ctx_t *wc = &s_ws_ctx;
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake, client connected");
        wc->sockfd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    /* 收到 WebSocket 二进制帧，转发给 on_recv（即 esprpc_handle_request） */
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame len failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (frame.len == 0) {
        return ESP_OK;
    }

    uint8_t *buf = malloc(frame.len);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    frame.payload = buf;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }

    if (wc->on_recv && frame.type == HTTPD_WS_TYPE_BINARY) {
        uint8_t method_id = (frame.len >= 1) ? buf[0] : 0;
        ESP_LOGI(TAG, "RPC frame recv len=%d methodId=%d", (int)frame.len, method_id);
        wc->current_req = req;
        wc->on_recv(buf, frame.len, wc->on_recv_ctx);
        wc->current_req = NULL;
    }
    free(buf);
    return ESP_OK;
}

static const httpd_uri_t ws_uri = {
    .uri       = "/ws",
    .method    = HTTP_GET,
    .handler   = ws_handler,
    .user_ctx  = NULL,
    .is_websocket = true,
};

static esprpc_transport_t s_ws_transport = {
    .send  = ws_send,
    .start = ws_start,
    .stop  = ws_stop,
    .ctx   = &s_ws_ctx,
};

esp_err_t esprpc_transport_ws_init(void)
{
    memset(&s_ws_ctx, 0, sizeof(s_ws_ctx));
    s_ws_ctx.sockfd = -1;
    ESP_LOGI(TAG, "WebSocket transport init (stub - call esprpc_transport_ws_start_server when WiFi ready)");
    return ESP_OK;
}

/**
 * @brief 启动 WebSocket 端点：httpd_server 为空则内部创建 httpd，非空则使用传入的服务器并仅注册 /ws
 */
esp_err_t esprpc_transport_ws_start_server(void *httpd_server)
{
    if (s_ws_ctx.server) {
        ESP_LOGW(TAG, "WebSocket already registered");
        return ESP_OK;
    }

    httpd_handle_t server = (httpd_handle_t)httpd_server;

    if (!server) {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 8;
        config.lru_purge_enable = true;
        esp_err_t ret = httpd_start(&server, &config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_ws_ctx.server = server;
        s_ws_ctx.server_owned = true;
    } else {
        s_ws_ctx.server = server;
        s_ws_ctx.server_owned = false;
    }

    esp_err_t ret = httpd_register_uri_handler(s_ws_ctx.server, &ws_uri);
    if (ret != ESP_OK) {
        if (s_ws_ctx.server_owned) {
            httpd_stop(s_ws_ctx.server);
        }
        s_ws_ctx.server = NULL;
        s_ws_ctx.server_owned = false;
        ESP_LOGE(TAG, "httpd_register_uri_handler /ws failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WebSocket at /ws (server %s)", s_ws_ctx.server_owned ? "owned" : "external");
    return ESP_OK;
}

/**
 * @brief 获取 WebSocket 传输实例（用于 esprpc_transport_add）
 */
esprpc_transport_t *esprpc_transport_ws_get(void)
{
    return &s_ws_transport;
}

#else /* !CONFIG_HTTPD_WS_SUPPORT */

esp_err_t esprpc_transport_ws_init(void)
{
    ESP_LOGW(TAG, "WebSocket transport disabled (CONFIG_HTTPD_WS_SUPPORT not set)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esprpc_transport_ws_start_server(void *httpd_server)
{
    (void)httpd_server;
    return ESP_ERR_NOT_SUPPORTED;
}

esprpc_transport_t *esprpc_transport_ws_get(void)
{
    return NULL;
}

#endif /* CONFIG_HTTPD_WS_SUPPORT */
