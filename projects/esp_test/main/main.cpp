/**
 * @file main.cpp
 * @brief ESP-IDF RPC 测试工程
 */

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstring>

#include "esprpc.h"
#include "esprpc_service.h"
#include "esprpc_transport.h"
#include "user_service.rpc.gen.hpp"
#include "wifi_config_local.h"
#include "wifi_sta.hpp"

#if CONFIG_ESPRPC_ENABLE_SERIAL
#include "driver/usb_serial_jtag.h"

/* 使用 ESP32-C3 原生 USB Serial/JTAG（与 idf.py monitor 同口时，可在 menuconfig 将控制台改为 UART 避免占用） */
static constexpr size_t kSerialFrameHeader = 5;
static constexpr size_t kSerialFrameBufSize =
    ESPRPC_SERIAL_MARKER_MAX + kSerialFrameHeader + CONFIG_ESPRPC_SERIAL_PAYLOAD_MAX + ESPRPC_SERIAL_MARKER_MAX;

static void serial_usb_jtag_tx_cb(const uint8_t *data, size_t len, void *ctx)
{
  (void)ctx;
  if (data && len)
  {
    usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(100));
  }
}

static size_t find_prefix(const uint8_t *buf, size_t buf_len,
                          const uint8_t *prefix, size_t prefix_len)
{
  if (prefix_len == 0 || buf_len < prefix_len)
    return buf_len;
  for (size_t i = 0; i <= buf_len - prefix_len; i++)
  {
    if (memcmp(buf + i, prefix, prefix_len) == 0)
      return i;
  }
  return buf_len;
}

static void serial_recv_task(void *arg)
{
  (void)arg;
  uint8_t prefix_buf[ESPRPC_SERIAL_MARKER_MAX], suffix_buf[ESPRPC_SERIAL_MARKER_MAX];
  size_t prefix_len = 0, suffix_len = 0;
  esprpc_serial_get_packet_marker(prefix_buf, sizeof(prefix_buf), &prefix_len,
                                  suffix_buf, sizeof(suffix_buf), &suffix_len);

  static uint8_t frame_buf[kSerialFrameBufSize];
  uint8_t sync_buf[32];
  size_t sync_len = 0;

  while (1)
  {
    if (prefix_len > 0)
    {
      while (sync_len < prefix_len)
      {
        int n = usb_serial_jtag_read_bytes(sync_buf + sync_len, prefix_len - sync_len, pdMS_TO_TICKS(100));
        if (n > 0)
          sync_len += static_cast<size_t>(n);
        if (n <= 0 && sync_len > 0)
          break;
      }
      if (sync_len < prefix_len)
        continue;
      size_t idx = find_prefix(sync_buf, sync_len, prefix_buf, prefix_len);
      if (idx >= sync_len)
      {
        memmove(sync_buf, sync_buf + 1, sync_len - 1);
        sync_len--;
        continue;
      }
      memcpy(frame_buf, prefix_buf, prefix_len);
      size_t after_prefix = sync_len - idx - prefix_len;
      if (after_prefix > 0)
        memcpy(frame_buf + prefix_len, sync_buf + idx + prefix_len, after_prefix);
      sync_len = 0;

      size_t need = kSerialFrameHeader;
      size_t got = after_prefix;
      while (got < need)
      {
        int n = usb_serial_jtag_read_bytes(frame_buf + prefix_len + got, need - got, pdMS_TO_TICKS(100));
        if (n > 0)
          got += static_cast<size_t>(n);
        if (n <= 0 && got > 0)
          break;
      }
      if (got < need)
        continue;

      uint16_t payload_len = static_cast<uint16_t>(frame_buf[prefix_len + 3]) |
                             (static_cast<uint16_t>(frame_buf[prefix_len + 4]) << 8);
      if (payload_len > CONFIG_ESPRPC_SERIAL_PAYLOAD_MAX)
      {
        sync_len = 0;
        continue;
      }
      need = kSerialFrameHeader + payload_len;
      while (got < need)
      {
        int n = usb_serial_jtag_read_bytes(frame_buf + prefix_len + got, need - got, pdMS_TO_TICKS(500));
        if (n > 0)
          got += static_cast<size_t>(n);
        if (n <= 0 && got > 0)
          break;
      }
      if (got < need)
        continue;

      if (suffix_len > 0)
      {
        size_t left = suffix_len;
        while (left > 0)
        {
          int n = usb_serial_jtag_read_bytes(frame_buf + prefix_len + got, left, pdMS_TO_TICKS(100));
          if (n > 0)
          {
            got += static_cast<size_t>(n);
            left -= static_cast<size_t>(n);
          }
          if (n <= 0)
            break;
        }
        if (left > 0)
          continue;
      }

      esprpc_serial_feed_raw_packet(frame_buf, prefix_len + got + suffix_len);
      continue;
    }

    size_t got = 0;
    while (got < kSerialFrameHeader)
    {
      int n = usb_serial_jtag_read_bytes(frame_buf + got, kSerialFrameHeader - got, pdMS_TO_TICKS(100));
      if (n > 0)
        got += static_cast<size_t>(n);
      if (n <= 0 && got > 0)
        break;
    }
    if (got < kSerialFrameHeader)
      continue;

    uint16_t payload_len = static_cast<uint16_t>(frame_buf[3]) | (static_cast<uint16_t>(frame_buf[4]) << 8);
    if (payload_len > CONFIG_ESPRPC_SERIAL_PAYLOAD_MAX)
      continue;
    size_t need = kSerialFrameHeader + payload_len;
    while (got < need)
    {
      int n = usb_serial_jtag_read_bytes(frame_buf + got, need - got, pdMS_TO_TICKS(500));
      if (n > 0)
        got += static_cast<size_t>(n);
      if (n <= 0 && got > 0)
        break;
    }
    if (got < need)
      continue;

    if (suffix_len > 0)
    {
      size_t left = suffix_len;
      while (left > 0)
      {
        int n = usb_serial_jtag_read_bytes(frame_buf + got, left, pdMS_TO_TICKS(100));
        if (n > 0)
        {
          got += static_cast<size_t>(n);
          left -= static_cast<size_t>(n);
        }
        if (n <= 0)
          break;
      }
      if (left > 0)
        continue;
    }

    esprpc_serial_feed_raw_packet(frame_buf, got);
  }
}

static esp_err_t serial_usb_jtag_init(void)
{
  usb_serial_jtag_driver_config_t config = {
      .tx_buffer_size = 1024,
      .rx_buffer_size = 1024,
  };
  esp_err_t ret = usb_serial_jtag_driver_install(&config);
  if (ret != ESP_OK)
    return ret;

  BaseType_t ok = xTaskCreate(serial_recv_task, "esprpc_serial", 4096, nullptr, 5, nullptr);
  if (ok != pdPASS)
  {
    usb_serial_jtag_driver_uninstall();
    return ESP_FAIL;
  }
  return ESP_OK;
}
#endif

static const char *TAG = "main";
static const char *TAG_WIFI = "main";

/* 传输层接收回调：将数据交给 esprpc 处理（二进制帧） */
static void transport_recv_to_rpc(const uint8_t *data, size_t len,
                                  void *user_ctx)
{
  (void)user_ctx;
  esprpc_handle_request(data, len);
}

static void wifi_init_sta(void)
{
  ESP_LOGI(TAG_WIFI, "WiFi init: SSID='%s' (len=%d), auth=WPA/WPA2-PSK",
           WIFI_SSID, (int)strlen(WIFI_SSID));

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  /* 注册 WiFi 事件（获取 IP + 状态更新，合并 handler） */
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_event, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_event, NULL));

  wifi_config_t wifi_config = {
      .sta = {.threshold =
                  {
                      .authmode = WIFI_AUTH_WPA_WPA2_PSK, /* 兼容 WPA/WPA2 */
                  }},
  };
  snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s",
           WIFI_SSID);
  snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password),
           "%s", WIFI_PASSWORD);

  ESP_LOGI(TAG_WIFI, "Setting config: ssid='%s', passwd_len=%d",
           (char *)wifi_config.sta.ssid,
           (int)strlen((char *)wifi_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());

  ESP_LOGI(TAG_WIFI, "WiFi started, connecting to AP...");
}

extern "C" void app_main(void)
{
  ESP_LOGI(TAG, "ESP RPC Test starting");
  ESP_LOGI(TAG_WIFI, "WiFi target: SSID='%s'", WIFI_SSID);

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  wifi_init_sta();

  /* RPC 初始化 */
  esprpc_init();

#if CONFIG_HTTPD_WS_SUPPORT
  /* WebSocket 传输：初始化、注册、启动 */
  esprpc_transport_ws_init();
  esprpc_transport_t *ws = esprpc_transport_ws_get();
  if (ws)
  {
    esprpc_transport_add(ws);
    ws->start(ws->ctx, transport_recv_to_rpc, NULL);
  }
  /* HTTP 服务器在 WiFi 获取 IP 后启动（见 on_wifi_ip_event） */
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &on_wifi_event, NULL));
#endif

#if CONFIG_ESPRPC_ENABLE_BLE
  /* BLE 传输：初始化、注册 */
  esprpc_transport_ble_init();
  esprpc_transport_t *ble = esprpc_transport_ble_get();
  if (ble)
  {
    esprpc_transport_add(ble);
    ble->start(ble->ctx, transport_recv_to_rpc, NULL);
  }
#endif

#if CONFIG_ESPRPC_ENABLE_SERIAL
  /* 串口传输：初始化传输层，应用侧初始化 USB Serial/JTAG、读任务并注册发送回调 */
  esprpc_transport_serial_init();
  esp_err_t ser_ret = serial_usb_jtag_init();
  if (ser_ret == ESP_OK)
  {
    esprpc_serial_set_tx_cb(serial_usb_jtag_tx_cb, nullptr);
  }
  esprpc_transport_t *serial = esprpc_transport_serial_get();
  if (serial)
  {
    esprpc_transport_add(serial);
    serial->start(serial->ctx, transport_recv_to_rpc, NULL);
  }
#endif

  /* 注册 UserService（实现由 generator 生成占位） */
  esprpc_register_service_ex("UserService", &user_service_impl_instance,
                             UserService_dispatch);

  ESP_LOGI(TAG, "RPC ready - WebSocket at ws://<ip>:80/ws when WiFi connected"
#if CONFIG_ESPRPC_ENABLE_BLE
                ", BLE advertising"
#endif
#if CONFIG_ESPRPC_ENABLE_SERIAL
                ", Serial (external)"
#endif
  );
}
