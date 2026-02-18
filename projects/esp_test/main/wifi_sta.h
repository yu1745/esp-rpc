/**
 * @file wifi_sta.h
 * @brief WiFi STA 事件处理
 */

#ifndef WIFI_STA_H
#define WIFI_STA_H

#include "esp_event.h"

void on_wifi_event(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data);

#endif /* WIFI_STA_H */
