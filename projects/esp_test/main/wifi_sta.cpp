/**
 * @file wifi_sta.cpp
 * @brief WiFi STA 事件处理：获取 IP + 状态更新
 */

#include <cstring>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "esp_http_server.h"
#include "esprpc_transport.h"
#include "wifi_sta.hpp"

static const char *TAG = "wifi_sta";
static volatile bool s_wifi_connected = false;

/** true = 自建 httpd 并注册 /health，再让 esp-rpc 只注册 /ws；false = esp-rpc 内部创建 httpd */
static const bool use_own_httpd = true;

static httpd_handle_t s_httpd = nullptr;

static esp_err_t health_handler(httpd_req_t *req) {
  const char *body = "{\"status\":\"ok\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, body, strlen(body));
  return ESP_OK;
}

static void start_own_httpd_and_ws(void) {
  if (s_httpd) return;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 8;
  cfg.lru_purge_enable = true;
  if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    return;
  }
  httpd_uri_t health_uri = {
    .uri = "/health",
    .method = HTTP_GET,
    .handler = health_handler,
    .user_ctx = nullptr,
  };
  if (httpd_register_uri_handler(s_httpd, &health_uri) != ESP_OK) {
    ESP_LOGE(TAG, "register /health failed");
    httpd_stop(s_httpd);
    s_httpd = nullptr;
    return;
  }
  esp_err_t ret = esprpc_transport_ws_start_server(s_httpd, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "esprpc_transport_ws_start_server failed: %s", esp_err_to_name(ret));
    httpd_stop(s_httpd);
    s_httpd = nullptr;
  } else {
    ESP_LOGI(TAG, "HTTP server (own): /health + /rpc");
  }
}

namespace {

/* 将 WiFi 断开原因码转为可读字符串 */
const char *wifi_reason_str(uint8_t reason) {
  switch (reason) {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_LEAVE";
    case 4:   return "ASSOC_EXPIRE";
    case 5:   return "ASSOC_TOOMANY";
    case 6:   return "NOT_AUTHED";
    case 7:   return "NOT_ASSOCED";
    case 8:   return "ASSOC_LEAVE";
    case 9:   return "ASSOC_NOT_AUTHED";
    case 14:  return "MIC_FAILURE";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
    case 16:  return "GROUP_KEY_UPDATE_TIMEOUT";
    case 23:  return "802_1X_AUTH_FAILED";
    case 24:  return "CIPHER_SUITE_REJECTED";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default:  return "UNKNOWN";
  }
}

}  // namespace

/* WiFi 事件：获取 IP + 状态更新，合并为一个 handler 用 switch 判断 */
void on_wifi_event(void *arg, esp_event_base_t event_base,
                   int32_t event_id, void *event_data) {
  (void)arg;

  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_CONNECTED: {
        auto *e = static_cast<wifi_event_sta_connected_t *>(event_data);
        s_wifi_connected = true;
        ESP_LOGI(TAG, "Connected to AP ssid=%s, channel=%d, authmode=%d",
                 reinterpret_cast<const char *>(e->ssid), e->channel, e->authmode);
        break;
      }
      case WIFI_EVENT_STA_DISCONNECTED: {
        auto *e = static_cast<wifi_event_sta_disconnected_t *>(event_data);
        s_wifi_connected = false;
        ESP_LOGW(TAG, "Disconnected: reason=%d (%s), ssid=%.32s",
                 e->reason, wifi_reason_str(e->reason),
                 reinterpret_cast<const char *>(e->ssid));
        if (e->reason == 201) {
          ESP_LOGW(TAG, "NO_AP_FOUND: 请检查 SSID 是否正确、路由器是否开启、信号是否足够");
        } else if (e->reason == 202) {
          ESP_LOGW(TAG, "AUTH_FAIL: 密码可能错误，或加密方式不匹配");
        } else if (e->reason == 204) {
          ESP_LOGW(TAG, "HANDSHAKE_TIMEOUT: 密码错误或路由器加密设置不兼容");
        }
        break;
      }
      default:
        break;
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
      case IP_EVENT_STA_GOT_IP: {
        auto *ev = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR " mask:" IPSTR " gw:" IPSTR,
                 IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.netmask),
                 IP2STR(&ev->ip_info.gw));
        if (use_own_httpd) {
          //使用自建的httpd
          start_own_httpd_and_ws();
        } else {
          //使用esp-rpc自动创建的httpd
          esp_err_t ret = esprpc_transport_ws_start_server(NULL, NULL);
          if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WebSocket server: %s", esp_err_to_name(ret));
          }
        }
        break;
      }
      default:
        break;
    }
  }
}
