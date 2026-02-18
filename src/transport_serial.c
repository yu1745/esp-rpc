/**
 * @file transport_serial.c
 * @brief 串口（UART）传输层（仅外部管理）
 *
 * 帧格式与 WebSocket/BLE 一致: [1B method_id][2B invoke_id LE][2B payload_len LE][payload]
 * 可选：每个 packet 可配置前缀/后缀（字面量或 \\xNN），便于与其他协议复用串口。
 * 串口由外部代码管理：应用需注册发送回调 esprpc_serial_set_tx_cb()，
 * 识别前后缀后通过 esprpc_serial_feed_packet() / esprpc_serial_feed_raw_packet() 把 RPC 包交给本模块。
 */

#include "esprpc_transport.h"
#include "esprpc.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "esprpc_serial";

#if CONFIG_ESPRPC_ENABLE_SERIAL

#define SERIAL_RPC_FRAME_HEADER 5
#define SERIAL_RPC_PAYLOAD_MAX  (CONFIG_ESPRPC_SERIAL_PAYLOAD_MAX)
#define SERIAL_PREFIX_SUFFIX_MAX 16

/** 发送回调：由应用提供，用于把 RPC 帧（含可选前后缀）发到串口 */
typedef void (*serial_tx_fn_t)(const uint8_t *data, size_t len, void *ctx);

/** 串口传输上下文（仅外部管理，不创建 UART/任务） */
typedef struct {
    uint8_t prefix_buf[SERIAL_PREFIX_SUFFIX_MAX];
    uint8_t suffix_buf[SERIAL_PREFIX_SUFFIX_MAX];
    size_t prefix_len;
    size_t suffix_len;
    serial_tx_fn_t tx_cb;
    void *tx_cb_ctx;
    esprpc_transport_on_recv_fn on_recv;
    void *on_recv_ctx;
} serial_ctx_t;

static serial_ctx_t s_serial_ctx = {0};

/**
 * 解析前后缀字符串为字节序列。支持：
 * - 字面量：每个字符即一字节，如 ">>" "RPC"
 * - 十六进制：\\xNN 表示一字节，如 "\\xAA\\x55"
 * 返回解析出的字节数，最多 max_len。
 */
static size_t parse_packet_marker(const char *str, uint8_t *out, size_t max_len)
{
    if (!str || !out || max_len == 0) return 0;
    size_t n = 0;
    while (*str != '\0' && n < max_len) {
        if (str[0] == '\\' && str[1] == 'x' && isxdigit((unsigned char)str[2]) && isxdigit((unsigned char)str[3])) {
            unsigned int byte = 0;
            for (int i = 0; i < 2; i++) {
                char c = (char)toupper((unsigned char)str[2 + i]);
                byte = (byte << 4) + (c >= 'A' ? c - 'A' + 10 : c - '0');
            }
            out[n++] = (uint8_t)byte;
            str += 4;
        } else {
            out[n++] = (uint8_t)(*str++);
        }
    }
    return n;
}

/** 发送：把 prefix + data + suffix 通过 tx_cb 交给应用发送 */
static esp_err_t serial_send_impl(serial_ctx_t *sc, const uint8_t *data, size_t len)
{
    if (!sc->tx_cb) return ESP_ERR_INVALID_STATE;
    size_t total = sc->prefix_len + len + sc->suffix_len;
    if (total == 0) return ESP_OK;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return ESP_ERR_NO_MEM;
    size_t off = 0;
    if (sc->prefix_len) {
        memcpy(buf + off, sc->prefix_buf, sc->prefix_len);
        off += sc->prefix_len;
    }
    memcpy(buf + off, data, len);
    off += len;
    if (sc->suffix_len) memcpy(buf + off, sc->suffix_buf, sc->suffix_len);
    sc->tx_cb(buf, total, sc->tx_cb_ctx);
    free(buf);
    return ESP_OK;
}

static esp_err_t serial_send(void *ctx, const uint8_t *data, size_t len)
{
    serial_ctx_t *sc = (serial_ctx_t *)ctx;
    if (!sc) return ESP_ERR_INVALID_STATE;
    return serial_send_impl(sc, data, len);
}

static esp_err_t serial_start(void *ctx, esprpc_transport_on_recv_fn on_recv, void *user_ctx)
{
    serial_ctx_t *sc = (serial_ctx_t *)ctx;
    if (!sc) return ESP_ERR_INVALID_ARG;
    sc->on_recv = on_recv;
    sc->on_recv_ctx = user_ctx;
    return ESP_OK;
}

static void serial_stop(void *ctx)
{
    serial_ctx_t *sc = (serial_ctx_t *)ctx;
    if (sc) sc->on_recv = NULL;
}

static esprpc_transport_t s_serial_transport = {
    .send  = serial_send,
    .start = serial_start,
    .stop  = serial_stop,
    .ctx   = &s_serial_ctx,
};

esp_err_t esprpc_transport_serial_init(void)
{
    memset(&s_serial_ctx, 0, sizeof(s_serial_ctx));
    s_serial_ctx.prefix_len = parse_packet_marker(CONFIG_ESPRPC_SERIAL_PREFIX,
                                                  s_serial_ctx.prefix_buf, SERIAL_PREFIX_SUFFIX_MAX);
    s_serial_ctx.suffix_len = parse_packet_marker(CONFIG_ESPRPC_SERIAL_SUFFIX,
                                                  s_serial_ctx.suffix_buf, SERIAL_PREFIX_SUFFIX_MAX);
    ESP_LOGI(TAG, "Serial transport init (external only, prefix=%zu suffix=%zu)",
             s_serial_ctx.prefix_len, s_serial_ctx.suffix_len);
    return ESP_OK;
}

esprpc_transport_t *esprpc_transport_serial_get(void)
{
    return &s_serial_transport;
}

/* 外部管理时：管理串口的代码把已去掉前后缀的 RPC 整包喂进来而不是esp-rpc自己控制串口读取 */
void esprpc_serial_feed_packet(const uint8_t *data, size_t len)
{
    serial_ctx_t *sc = &s_serial_ctx;
    if (!data || len < SERIAL_RPC_FRAME_HEADER) return;
    if (len > SERIAL_RPC_FRAME_HEADER + SERIAL_RPC_PAYLOAD_MAX) return;
    uint16_t payload_len = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
    if (len < SERIAL_RPC_FRAME_HEADER + payload_len) return;
    if (sc->on_recv) {
        ESP_LOGI(TAG, "RPC frame feed len=%zu methodId=%d", len, data[0]);
        sc->on_recv(data, len, sc->on_recv_ctx);
    }
}

/* 外部管理时：接受带前后缀的原始包，内部按配置剥掉前后缀后交给 on_recv（无需调用方自己脱壳） */
void esprpc_serial_feed_raw_packet(const uint8_t *data, size_t len)
{
    serial_ctx_t *sc = &s_serial_ctx;
    if (!data) return;
    size_t pl = sc->prefix_len;
    size_t sl = sc->suffix_len;
    if (len < pl + SERIAL_RPC_FRAME_HEADER + sl) return;
    if (pl > 0 && (pl > len || memcmp(data, sc->prefix_buf, pl) != 0)) return;
    const uint8_t *frame = data + pl;
    uint16_t payload_len = (uint16_t)frame[3] | ((uint16_t)frame[4] << 8);
    if (payload_len > SERIAL_RPC_PAYLOAD_MAX) return;
    size_t frame_len = SERIAL_RPC_FRAME_HEADER + payload_len;
    if (len < pl + frame_len + sl) return;
    if (sl > 0 && memcmp(data + pl + frame_len, sc->suffix_buf, sl) != 0) return;
    if (sc->on_recv) {
        ESP_LOGI(TAG, "RPC raw frame feed len=%zu methodId=%d", frame_len, frame[0]);
        sc->on_recv(frame, frame_len, sc->on_recv_ctx);
    }
}

void esprpc_serial_get_packet_marker(uint8_t *prefix_buf, size_t prefix_max, size_t *prefix_len,
                                    uint8_t *suffix_buf, size_t suffix_max, size_t *suffix_len)
{
    serial_ctx_t *sc = &s_serial_ctx;
    if (prefix_len) *prefix_len = sc->prefix_len;
    if (suffix_len) *suffix_len = sc->suffix_len;
    if (prefix_buf && prefix_max > 0 && sc->prefix_len > 0) {
        size_t n = sc->prefix_len < prefix_max ? sc->prefix_len : prefix_max;
        memcpy(prefix_buf, sc->prefix_buf, n);
    }
    if (suffix_buf && suffix_max > 0 && sc->suffix_len > 0) {
        size_t n = sc->suffix_len < suffix_max ? sc->suffix_len : suffix_max;
        memcpy(suffix_buf, sc->suffix_buf, n);
    }
}

esp_err_t esprpc_serial_set_tx_cb(void (*tx_fn)(const uint8_t *data, size_t len, void *ctx), void *ctx)
{
    serial_ctx_t *sc = &s_serial_ctx;
    sc->tx_cb = (serial_tx_fn_t)tx_fn;
    sc->tx_cb_ctx = ctx;
    return ESP_OK;
}

#else /* !CONFIG_ESPRPC_ENABLE_SERIAL */

esp_err_t esprpc_transport_serial_init(void)
{
    ESP_LOGW(TAG, "Serial transport disabled (CONFIG_ESPRPC_ENABLE_SERIAL not set)");
    return ESP_ERR_NOT_SUPPORTED;
}

esprpc_transport_t *esprpc_transport_serial_get(void)
{
    return NULL;
}

void esprpc_serial_feed_packet(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
}

void esprpc_serial_feed_raw_packet(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
}

void esprpc_serial_get_packet_marker(uint8_t *prefix_buf, size_t prefix_max, size_t *prefix_len,
                                     uint8_t *suffix_buf, size_t suffix_max, size_t *suffix_len)
{
    if (prefix_len) *prefix_len = 0;
    if (suffix_len) *suffix_len = 0;
    (void)prefix_buf;
    (void)prefix_max;
    (void)suffix_buf;
    (void)suffix_max;
}

esp_err_t esprpc_serial_set_tx_cb(void (*tx_fn)(const uint8_t *data, size_t len, void *ctx), void *ctx)
{
    (void)tx_fn;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* CONFIG_ESPRPC_ENABLE_SERIAL */
