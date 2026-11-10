/**
 * Wi-Fi Manager for Szpont Washer ESP
 * Connects directly using secrets.h (HUAWEI-2.4G-Q3d4) with RTC Fast Connect
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    APP_WIFI_CONNECTED = 0,
    APP_WIFI_DISCONNECTED
} app_wifi_event_t;

typedef void (*app_wifi_cb_t)(app_wifi_event_t event);

esp_err_t wifi_manager_init(app_wifi_cb_t event_cb);
void wifi_manager_start(void);
void wifi_manager_stop(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_get_ip(char *ip_str, size_t max_len);
int wifi_manager_get_rssi(void);

#endif // WIFI_MANAGER_H
