/**
 * @file wifi_sta.hpp
 * @brief WiFi STA 事件处理
 */

#ifndef WIFI_STA_HPP
#define WIFI_STA_HPP

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

void on_wifi_event(void *arg, esp_event_base_t event_base,
                  int32_t event_id, void *event_data);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_STA_HPP */
