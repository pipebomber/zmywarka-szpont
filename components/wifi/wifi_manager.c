/* Wi-Fi STA z szybkim laczeniem z pamieci RTC */

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "wifi_manager.h"
#include "config.h"

static const char *TAG = "WIFI_MGR";

static RTC_DATA_ATTR bool s_rtc_fast_valid = false;
static RTC_DATA_ATTR uint8_t s_rtc_channel = 0;
static RTC_DATA_ATTR uint8_t s_rtc_bssid[6] = {0};

static app_wifi_cb_t s_user_cb = NULL;
static bool s_is_connected = false;
static bool s_should_stay_connected = false;
static int s_retry_count = 0;
static char s_current_ip[16] = {0};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *conn = (wifi_event_sta_connected_t *)event_data;
                s_rtc_channel = conn->channel;
                memcpy(s_rtc_bssid, conn->bssid, 6);
                s_rtc_fast_valid = true;
                ESP_LOGI(TAG, "Associated to AP (ch=%u)", s_rtc_channel);
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED:
                s_is_connected = false;
                s_current_ip[0] = '\0';
                if (!s_should_stay_connected) break;

                if (s_rtc_fast_valid && s_retry_count == 0) {
                    s_rtc_fast_valid = false;
                    wifi_config_t cfg;
                    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
                        cfg.sta.bssid_set = false;
                        cfg.sta.channel = 0;
                        esp_wifi_set_config(WIFI_IF_STA, &cfg);
                    }
                }
                s_retry_count++;
                if (s_retry_count >= 3 && s_user_cb) {
                    s_user_cb(APP_WIFI_DISCONNECTED);
                }
                esp_wifi_connect();
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_current_ip, sizeof(s_current_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_is_connected = true;
        ESP_LOGI(TAG, "Got IP: %s", s_current_ip);
        if (s_user_cb) s_user_cb(APP_WIFI_CONNECTED);
    }
}

esp_err_t wifi_manager_init(app_wifi_cb_t event_cb)
{
    s_user_cb = event_cb;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    return ESP_OK;
}

void wifi_manager_start(void)
{
    wifi_config_t wifi_config = {0};
#ifdef WIFI_SSID
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
#endif
#ifdef WIFI_PASSWORD
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
#endif

    if (s_rtc_fast_valid && s_rtc_channel > 0) {
        wifi_config.sta.channel = s_rtc_channel;
        memcpy(wifi_config.sta.bssid, s_rtc_bssid, 6);
        wifi_config.sta.bssid_set = true;
        ESP_LOGI(TAG, "Fast-connecting to %s (ch=%u)", (char *)wifi_config.sta.ssid, s_rtc_channel);
    } else {
        ESP_LOGI(TAG, "Connecting to SSID: %s", (char *)wifi_config.sta.ssid);
    }

    s_should_stay_connected = true;
    s_retry_count = 0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    // wylaczamy oszczedzanie modemu, zeby lokalne TCP chodzilo bez opoznien
    esp_wifi_set_ps(WIFI_PS_NONE);
}

void wifi_manager_stop(void)
{
    s_should_stay_connected = false;
    s_is_connected = false;
    esp_wifi_disconnect();
    esp_wifi_stop();
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}

bool wifi_manager_get_ip(char *ip_str, size_t max_len)
{
    if (!s_is_connected || s_current_ip[0] == '\0') return false;
    snprintf(ip_str, max_len, "%s", s_current_ip);
    return true;
}

int wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (s_is_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return -100;
}
