#include "weather.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "WEATHER";

#define WEATHER_DEFAULT_CITY    "Wrocław"
#define WEATHER_RETRY_FAIL_US   (120LL * 1000000LL)
#define WEATHER_BUF_SIZE        2560

static weather_data_t s_data = {
    .city = WEATHER_DEFAULT_CITY,
    .lat = WEATHER_LAT_STR,
    .lon = WEATHER_LON_STR,
};
static weather_update_cb_t s_cb = NULL;
static volatile bool s_fetching = false;
static int64_t s_last_attempt_us = 0;
static bool s_last_attempt_ok = false;

static uint8_t ymd_weekday(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

static void load_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return;

    size_t sz = sizeof(s_data.city);
    if (nvs_get_str(nvs, "w_city", s_data.city, &sz) != ESP_OK || s_data.city[0] == '\0') {
        snprintf(s_data.city, sizeof(s_data.city), "%s", WEATHER_DEFAULT_CITY);
    }
    sz = sizeof(s_data.lat);
    if (nvs_get_str(nvs, "w_lat", s_data.lat, &sz) != ESP_OK || s_data.lat[0] == '\0') {
        snprintf(s_data.lat, sizeof(s_data.lat), "%s", WEATHER_LAT_STR);
    }
    sz = sizeof(s_data.lon);
    if (nvs_get_str(nvs, "w_lon", s_data.lon, &sz) != ESP_OK || s_data.lon[0] == '\0') {
        snprintf(s_data.lon, sizeof(s_data.lon), "%s", WEATHER_LON_STR);
    }

    int64_t ep = 0;
    int16_t tc = 0;
    uint8_t code = 0;
    uint8_t day = 1;
    char flat[16] = {0};
    size_t fl_sz = sizeof(flat);
    if (nvs_get_str(nvs, "w_flat", flat, &fl_sz) == ESP_OK &&
        strcmp(flat, s_data.lat) == 0 &&
        nvs_get_i64(nvs, "w_ep", &ep) == ESP_OK &&
        nvs_get_i16(nvs, "w_tc", &tc) == ESP_OK &&
        nvs_get_u8(nvs, "w_code", &code) == ESP_OK &&
        nvs_get_u8(nvs, "w_day", &day) == ESP_OK) {
        s_data.valid = true;
        s_data.fetched_at = ep;
        s_data.temp_c = tc;
        s_data.wmo_code = code;
        s_data.is_day = (day != 0);

        uint8_t dcnt = 0;
        size_t blob_sz = sizeof(s_data.days);
        if (nvs_get_u8(nvs, "w_dcnt", &dcnt) == ESP_OK &&
            dcnt > 0 && dcnt <= WEATHER_MAX_DAYS &&
            nvs_get_blob(nvs, "w_days", s_data.days, &blob_sz) == ESP_OK &&
            blob_sz == sizeof(s_data.days)) {
            s_data.day_count = dcnt;
        }
        ESP_LOGI(TAG, "Pogoda z NVS (%s): %d C, wmo=%u, dni=%u",
                 s_data.city, (int)tc, code, s_data.day_count);
    }
    nvs_close(nvs);
}

static void save_weather_nvs(const weather_data_t *d)
{
    if (!d || !d->valid || d->fetched_at < 1700000000LL) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_str(nvs, "w_flat", d->lat);
    nvs_set_i64(nvs, "w_ep", d->fetched_at);
    nvs_set_i16(nvs, "w_tc", d->temp_c);
    nvs_set_u8(nvs, "w_code", d->wmo_code);
    nvs_set_u8(nvs, "w_day", d->is_day ? 1 : 0);
    nvs_set_u8(nvs, "w_dcnt", d->day_count);
    nvs_set_blob(nvs, "w_days", d->days, sizeof(d->days));
    nvs_commit(nvs);
    nvs_close(nvs);
}

static bool parse_open_meteo(const char *json_str, weather_data_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *temp = cur ? cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m") : NULL;
    cJSON *code = cur ? cJSON_GetObjectItemCaseSensitive(cur, "weather_code") : NULL;
    cJSON *day = cur ? cJSON_GetObjectItemCaseSensitive(cur, "is_day") : NULL;

    bool ok = cJSON_IsNumber(temp) && cJSON_IsNumber(code) && cJSON_IsNumber(day);
    if (ok) {
        out->valid = true;
        out->temp_c = (int16_t)llround(temp->valuedouble);
        out->wmo_code = (uint8_t)code->valueint;
        out->is_day = (day->valueint != 0);
        time_t now = 0;
        time(&now);
        out->fetched_at = (int64_t)now;
        out->day_count = 0;

        cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
        cJSON *d_time = daily ? cJSON_GetObjectItemCaseSensitive(daily, "time") : NULL;
        cJSON *d_code = daily ? cJSON_GetObjectItemCaseSensitive(daily, "weather_code") : NULL;
        cJSON *d_max = daily ? cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max") : NULL;
        cJSON *d_min = daily ? cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min") : NULL;

        if (cJSON_IsArray(d_time) && cJSON_IsArray(d_code) && cJSON_IsArray(d_max) && cJSON_IsArray(d_min)) {
            int n = cJSON_GetArraySize(d_time);
            if (n > WEATHER_MAX_DAYS) n = WEATHER_MAX_DAYS;
            for (int i = 0; i < n; i++) {
                cJSON *it_t = cJSON_GetArrayItem(d_time, i);
                cJSON *it_c = cJSON_GetArrayItem(d_code, i);
                cJSON *it_hi = cJSON_GetArrayItem(d_max, i);
                cJSON *it_lo = cJSON_GetArrayItem(d_min, i);
                if (!cJSON_IsString(it_t) || !cJSON_IsNumber(it_c) || !cJSON_IsNumber(it_hi) || !cJSON_IsNumber(it_lo)) {
                    break;
                }
                int yy = 2026, mm = 1, dd = 1;
                if (sscanf(it_t->valuestring, "%d-%d-%d", &yy, &mm, &dd) == 3) {
                    out->days[i].wday = ymd_weekday(yy, mm, dd);
                } else {
                    out->days[i].wday = (uint8_t)(i % 7);
                }
                out->days[i].wmo_code = (uint8_t)it_c->valueint;
                out->days[i].t_max = (int16_t)llround(it_hi->valuedouble);
                out->days[i].t_min = (int16_t)llround(it_lo->valuedouble);
                out->day_count++;
            }
        }
    }

    cJSON_Delete(root);
    return ok;
}

static void fetch_task(void *pv)
{
    (void)pv;
    bool ok = false;
    char *buf = malloc(WEATHER_BUF_SIZE);
    char lat_snap[16];
    char lon_snap[16];
    snprintf(lat_snap, sizeof(lat_snap), "%s", s_data.lat);
    snprintf(lon_snap, sizeof(lon_snap), "%s", s_data.lon);

    char url[256];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,is_day,weather_code"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=7&timezone=auto",
             lat_snap, lon_snap);

    if (buf) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = 8000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client) {
            if (esp_http_client_open(client, 0) == ESP_OK) {
                esp_http_client_fetch_headers(client);
                int status = esp_http_client_get_status_code(client);
                int total = 0;
                while (total < WEATHER_BUF_SIZE - 1) {
                    int n = esp_http_client_read(client, buf + total, WEATHER_BUF_SIZE - 1 - total);
                    if (n <= 0) break;
                    total += n;
                }
                buf[total] = '\0';

                if (status == 200 && total > 0) {
                    weather_data_t next = s_data;
                    if (parse_open_meteo(buf, &next)) {
                        // zachowaj aktywne miasto jesli uzytkownik nie zmienil go w trakcie
                        if (strcmp(lat_snap, s_data.lat) == 0 && strcmp(lon_snap, s_data.lon) == 0) {
                            snprintf(next.city, sizeof(next.city), "%s", s_data.city);
                            snprintf(next.lat, sizeof(next.lat), "%s", s_data.lat);
                            snprintf(next.lon, sizeof(next.lon), "%s", s_data.lon);
                            s_data = next;
                            save_weather_nvs(&s_data);
                            ok = true;
                            ESP_LOGI(TAG, "%s: %d C, wmo=%u, dzien=%d, dni=%u",
                                     s_data.city, (int)s_data.temp_c, s_data.wmo_code,
                                     s_data.is_day ? 1 : 0, s_data.day_count);
                            if (s_cb) s_cb(&s_data);
                        }
                    } else {
                        ESP_LOGW(TAG, "Blad parsowania JSON Open-Meteo");
                    }
                } else {
                    ESP_LOGW(TAG, "Open-Meteo HTTP %d (bytes=%d)", status, total);
                }
            } else {
                ESP_LOGW(TAG, "Nie mozna polaczyc z Open-Meteo");
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }
        free(buf);
    }

    s_last_attempt_us = esp_timer_get_time();
    s_last_attempt_ok = ok;
    s_fetching = false;
    vTaskDelete(NULL);
}

void weather_init(weather_update_cb_t cb)
{
    s_cb = cb;
    load_nvs();
}

void weather_get_cached(weather_data_t *out)
{
    if (out) *out = s_data;
}

void weather_get_city(char *city, size_t city_sz, char *lat, size_t lat_sz, char *lon, size_t lon_sz)
{
    if (city && city_sz > 0) snprintf(city, city_sz, "%s", s_data.city);
    if (lat && lat_sz > 0) snprintf(lat, lat_sz, "%s", s_data.lat);
    if (lon && lon_sz > 0) snprintf(lon, lon_sz, "%s", s_data.lon);
}

esp_err_t weather_set_city(const char *city, const char *lat, const char *lon)
{
    if (!city || !lat || !lon || !city[0] || !lat[0] || !lon[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    char *endp = NULL;
    double d_lat = strtod(lat, &endp);
    if (endp == lat || d_lat < -90.0 || d_lat > 90.0) return ESP_ERR_INVALID_ARG;
    double d_lon = strtod(lon, &endp);
    if (endp == lon || d_lon < -180.0 || d_lon > 180.0) return ESP_ERR_INVALID_ARG;

    snprintf(s_data.city, sizeof(s_data.city), "%s", city);
    snprintf(s_data.lat, sizeof(s_data.lat), "%s", lat);
    snprintf(s_data.lon, sizeof(s_data.lon), "%s", lon);
    s_data.fetched_at = 0;
    s_last_attempt_us = 0;
    s_last_attempt_ok = false;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "w_city", s_data.city);
        nvs_set_str(nvs, "w_lat", s_data.lat);
        nvs_set_str(nvs, "w_lon", s_data.lon);
        nvs_erase_key(nvs, "w_ep");
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "Zmieniono miasto pogody: %s (%s, %s)", s_data.city, s_data.lat, s_data.lon);
    if (s_cb) s_cb(&s_data);
    weather_poll_if_due();
    return ESP_OK;
}

void weather_poll_if_due(void)
{
    if (s_fetching) return;

    int64_t now_us = esp_timer_get_time();
    if (s_last_attempt_us > 0) {
        int64_t retry_us = s_last_attempt_ok
            ? ((int64_t)WEATHER_POLL_INTERVAL_S * 1000000LL)
            : WEATHER_RETRY_FAIL_US;
        if ((now_us - s_last_attempt_us) < retry_us) return;
    }

    time_t now_ep = 0;
    time(&now_ep);
    if (s_data.valid && s_data.day_count > 0 && s_data.fetched_at >= 1700000000LL) {
        if (now_ep < 1700000000LL) {
            if (now_us < 10000000LL) return;
        } else if ((now_ep - s_data.fetched_at) < WEATHER_POLL_INTERVAL_S) {
            return;
        }
    }

    s_fetching = true;
    s_last_attempt_us = now_us;
    if (xTaskCreate(fetch_task, "weather_http", 6144, NULL, 3, NULL) != pdPASS) {
        s_fetching = false;
        s_last_attempt_ok = false;
    }
}
