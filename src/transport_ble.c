/**
 * @file transport_ble.c
 * @brief BLE GATT 传输层（基于 NimBLE）
 *
 * 通过 NimBLE GATT 服务收发 RPC 帧，与 WebSocket 传输并列。
 * 帧格式与 esprpc 一致: [1B method_id][2B invoke_id LE][2B payload_len LE][payload]
 *
 * 服务 UUID: 0xE5R0 (ESPRPC 自定义)
 * - 特征 TX (写): 客户端 -> ESP32 请求
 * - 特征 RX (通知): ESP32 -> 客户端 响应
 */

#include "esprpc_transport.h"
#include "esprpc.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "esprpc_ble";

#if defined(CONFIG_ESPRPC_ENABLE_BLE) && defined(CONFIG_BT_NIMBLE_ENABLED)

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* ESPRPC 服务 UUID: 0000E530-1212-EFDE-1523-785FEABCD123 (little-endian) */
#define ESPRPC_SVC_UUID 0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, \
                        0xde, 0xef, 0x12, 0x12, 0x30, 0xe5, 0x00, 0x00
/* TX 特征: 客户端写请求 0000E531-... */
#define ESPRPC_CHR_TX_UUID 0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, \
                           0xde, 0xef, 0x12, 0x12, 0x31, 0xe5, 0x00, 0x00
/* RX 特征: 服务端通知响应 0000E532-... */
#define ESPRPC_CHR_RX_UUID 0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, \
                           0xde, 0xef, 0x12, 0x12, 0x32, 0xe5, 0x00, 0x00

#define BLE_RPC_FRAME_MAX (512) /* 单帧最大长度，受 MTU 限制 */

static const ble_uuid128_t esprpc_svc_uuid = BLE_UUID128_INIT(
    ESPRPC_SVC_UUID);
static const ble_uuid128_t esprpc_chr_tx_uuid = BLE_UUID128_INIT(
    ESPRPC_CHR_TX_UUID);
static const ble_uuid128_t esprpc_chr_rx_uuid = BLE_UUID128_INIT(
    ESPRPC_CHR_RX_UUID);

static uint16_t chr_tx_val_handle;
static uint16_t chr_rx_val_handle;

/** BLE 传输上下文 */
typedef struct
{
    uint16_t conn_handle;
    bool connected;
    esprpc_transport_on_recv_fn on_recv;
    void *on_recv_ctx;
} ble_ctx_t;

static ble_ctx_t s_ble_ctx = {0};

static int rpc_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_chr_def esprpc_chrs[] = {
    {
        .uuid = &esprpc_chr_tx_uuid.u,
        .access_cb = rpc_chr_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .val_handle = &chr_tx_val_handle,
    },
    {
        .uuid = &esprpc_chr_rx_uuid.u,
        .access_cb = rpc_chr_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &chr_rx_val_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def esprpc_gatt_svc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &esprpc_svc_uuid.u,
        .characteristics = esprpc_chrs,
    },
    {0},
};

static int rpc_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    ble_ctx_t *ctx = &s_ble_ctx;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && attr_handle == chr_tx_val_handle)
    {
        uint32_t len = os_mbuf_len(ctxt->om);
        if (len < 5 || len > BLE_RPC_FRAME_MAX)
        {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint8_t *buf = (uint8_t *)malloc(len);
        if (!buf)
        {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        int rc = os_mbuf_copydata(ctxt->om, 0, len, buf);
        if (rc != 0)
        {
            free(buf);
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (ctx->on_recv)
        {
            ESP_LOGI(TAG, "RPC frame recv len=%lu methodId=%d", (unsigned long)len, buf[0]);
            ctx->on_recv(buf, len, ctx->on_recv_ctx);
        }
        free(buf);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static void ble_hs_sync_cb(void);
static void ble_hs_reset_cb(int reason);
static int ble_gap_event(struct ble_gap_event *event, void *arg);

static void ble_hs_sync_cb(void)
{
    int rc;
    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    /* 广播 ESPRPC 服务 UUID，便于 Web Bluetooth 过滤发现 */
    struct ble_hs_adv_fields adv_fields = {0};
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.name = (uint8_t *)ble_svc_gap_device_name();
    adv_fields.name_len = strlen(ble_svc_gap_device_name());
    adv_fields.name_is_complete = 1;
    adv_fields.uuids128 = &esprpc_svc_uuid;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;
    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "ble_gap_adv_set_fields failed: %d", rc);
    }
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 0x20,
        .itvl_max = 0x40,
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params,
                           ble_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE advertising started, RPC service UUID 0xE530");
}

static void ble_hs_reset_cb(int reason)
{
    ESP_LOGW(TAG, "BLE stack reset, reason=%d", reason);
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    ble_ctx_t *ctx = &s_ble_ctx;
    (void)arg;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ctx->conn_handle = event->connect.conn_handle;
            ctx->connected = true;
            ESP_LOGI(TAG, "BLE connected, conn_handle=%d", ctx->conn_handle);
        }
        else
        {
            ESP_LOGI(TAG, "BLE connect failed, status=%d", event->connect.status);
            /* 连接失败时重新开始广播 */
            struct ble_gap_adv_params adv_params = {
                .conn_mode = BLE_GAP_CONN_MODE_UND,
                .disc_mode = BLE_GAP_DISC_MODE_GEN,
                .itvl_min = 0x20,
                .itvl_max = 0x40,
            };
            ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                              &adv_params, ble_gap_event, NULL);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ctx->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ctx->connected = false;
        ESP_LOGI(TAG, "BLE disconnected");
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        struct ble_gap_adv_params adv_params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
            .itvl_min = 0x20,
            .itvl_max = 0x40,
        };
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_params,
                          ble_gap_event, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static esp_err_t ble_send(void *ctx, const uint8_t *data, size_t len)
{
    ble_ctx_t *bc = (ble_ctx_t *)ctx;
    if (!bc || !bc->connected || bc->conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > BLE_RPC_FRAME_MAX)
    {
        ESP_LOGW(TAG, "Frame too large (%zu), truncating", len);
        len = BLE_RPC_FRAME_MAX;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om)
    {
        return ESP_ERR_NO_MEM;
    }
    int rc = ble_gatts_notify_custom(bc->conn_handle, chr_rx_val_handle, om);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_notify_custom failed: %d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ble_start(void *ctx, esprpc_transport_on_recv_fn on_recv, void *user_ctx)
{
    ble_ctx_t *bc = (ble_ctx_t *)ctx;
    if (!bc)
        return ESP_ERR_INVALID_ARG;
    bc->on_recv = on_recv;
    bc->on_recv_ctx = user_ctx;
    return ESP_OK;
}

static void ble_stop(void *ctx)
{
    ble_ctx_t *bc = (ble_ctx_t *)ctx;
    if (bc)
    {
        bc->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        bc->connected = false;
        bc->on_recv = NULL;
    }
}

static esprpc_transport_t s_ble_transport = {
    .send = ble_send,
    .start = ble_start,
    .stop = ble_stop,
    .ctx = &s_ble_ctx,
};

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t esprpc_transport_ble_init(void)
{
    int rc;
    memset(&s_ble_ctx, 0, sizeof(s_ble_ctx));
    s_ble_ctx.conn_handle = BLE_HS_CONN_HANDLE_NONE;

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 必须在 nimble_port_freertos_init 之前设置回调和初始化服务 */
    ble_hs_cfg.reset_cb = ble_hs_reset_cb;
    ble_hs_cfg.sync_cb = ble_hs_sync_cb;

    /* 初始化 GAP/GATT 标准服务（NimBLE 必需） */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_svc_gap_device_name_set("ESPRPC");
    if (rc != 0)
    {
        ESP_LOGW(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
    }

    rc = ble_gatts_count_cfg(esprpc_gatt_svc);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(esprpc_gatt_svc);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "BLE transport init OK");
    return ESP_OK;
}

esprpc_transport_t *esprpc_transport_ble_get(void)
{
    return &s_ble_transport;
}

#else /* !CONFIG_ESPRPC_ENABLE_BLE || !CONFIG_BT_NIMBLE_ENABLED */

esp_err_t esprpc_transport_ble_init(void)
{
    ESP_LOGW(TAG, "BLE transport disabled (CONFIG_ESPRPC_ENABLE_BLE or CONFIG_BT_NIMBLE_ENABLED not set)");
    return ESP_ERR_NOT_SUPPORTED;
}

esprpc_transport_t *esprpc_transport_ble_get(void)
{
    return NULL;
}

#endif /* CONFIG_ESPRPC_ENABLE_BLE && CONFIG_BT_NIMBLE_ENABLED */
