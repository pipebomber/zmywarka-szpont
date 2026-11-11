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
#define WEATHER_BUF_SIZE        12288

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

static void parse_hhmm(const char *iso, uint8_t *out_h, uint8_t *out_m)
{
    int hh = 0, mm = 0;
    const char *tp = iso ? strchr(iso, 'T') : NULL;
    if (tp && sscanf(tp + 1, "%d:%d", &hh, &mm) == 2) {
        *out_h = (uint8_t)((hh >= 0 && hh < 24) ? hh : 0);
        *out_m = (uint8_t)((mm >= 0 && mm < 60) ? mm : 0);
    }
}

static uint8_t clamp_u8(double v, int max_v)
{
    long r = lround(v);
    if (r < 0) return 0;
    if (r > max_v) return (uint8_t)max_v;
    return (uint8_t)r;
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
    int16_t fc = 0;
    uint8_t code = 0;
    uint8_t day = 1;
    uint8_t hum = 0;
    uint8_t wind = 0;
    uint16_t pres = 0;
    char flat[16] = {0};
    size_t fl_sz = sizeof(flat);

    if (nvs_get_str(nvs, "w_flat", flat, &fl_sz) == ESP_OK &&
        strcmp(flat, s_data.lat) == 0 &&
        nvs_get_i64(nvs, "w_ep", &ep) == ESP_OK &&
        nvs_get_i16(nvs, "w_tc", &tc) == ESP_OK &&
        nvs_get_i16(nvs, "w_fc", &fc) == ESP_OK &&
        nvs_get_u8(nvs, "w_code", &code) == ESP_OK &&
        nvs_get_u8(nvs, "w_day", &day) == ESP_OK) {
        nvs_get_u8(nvs, "w_hum", &hum);
        nvs_get_u8(nvs, "w_wind", &wind);
        nvs_get_u16(nvs, "w_pres", &pres);

        s_data.valid = true;
        s_data.fetched_at = ep;
        s_data.temp_c = tc;
        s_data.feels_c = fc;
        s_data.humidity = hum;
        s_data.wind_kmh = wind;
        s_data.pressure_hpa = pres;
        s_data.wmo_code = code;
        s_data.is_day = (day != 0);

        uint8_t dcnt = 0;
        size_t blob_sz = sizeof(s_data.days);
        if (nvs_get_u8(nvs, "w_dcnt", &dcnt) == ESP_OK &&
            dcnt > 0 && dcnt <= WEATHER_MAX_DAYS &&
            nvs_get_blob(nvs, "w_days2", s_data.days, &blob_sz) == ESP_OK &&
            blob_sz == sizeof(s_data.days)) {
            s_data.day_count = dcnt;
        }
        ESP_LOGI(TAG, "Pogoda z NVS (%s): %d C, dni=%u",
                 s_data.city, (int)tc, s_data.day_count);
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
    nvs_set_i16(nvs, "w_fc", d->feels_c);
    nvs_set_u8(nvs, "w_hum", d->humidity);
    nvs_set_u8(nvs, "w_wind", d->wind_kmh);
    nvs_set_u16(nvs, "w_pres", d->pressure_hpa);
    nvs_set_u8(nvs, "w_code", d->wmo_code);
    nvs_set_u8(nvs, "w_day", d->is_day ? 1 : 0);
    nvs_set_u8(nvs, "w_dcnt", d->day_count);
    nvs_set_blob(nvs, "w_days2", d->days, sizeof(d->days));
    nvs_commit(nvs);
    nvs_close(nvs);
}

static bool parse_open_meteo(const char *json_str, weather_data_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return false;

    cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *temp = cur ? cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m") : NULL;
    cJSON *feels = cur ? cJSON_GetObjectItemCaseSensitive(cur, "apparent_temperature") : NULL;
    cJSON *hum = cur ? cJSON_GetObjectItemCaseSensitive(cur, "relative_humidity_2m") : NULL;
    cJSON *pres = cur ? cJSON_GetObjectItemCaseSensitive(cur, "surface_pressure") : NULL;
    cJSON *wind = cur ? cJSON_GetObjectItemCaseSensitive(cur, "wind_speed_10m") : NULL;
    cJSON *code = cur ? cJSON_GetObjectItemCaseSensitive(cur, "weather_code") : NULL;
    cJSON *day = cur ? cJSON_GetObjectItemCaseSensitive(cur, "is_day") : NULL;

    bool ok = cJSON_IsNumber(temp) && cJSON_IsNumber(code) && cJSON_IsNumber(day);
    if (ok) {
        out->valid = true;
        out->temp_c = (int16_t)llround(temp->valuedouble);
        out->feels_c = cJSON_IsNumber(feels) ? (int16_t)llround(feels->valuedouble) : out->temp_c;
        out->humidity = cJSON_IsNumber(hum) ? clamp_u8(hum->valuedouble, 100) : 0;
        out->wind_kmh = cJSON_IsNumber(wind) ? clamp_u8(wind->valuedouble, 250) : 0;
        long p_hpa = cJSON_IsNumber(pres) ? lround(pres->valuedouble) : 0;
        out->pressure_hpa = (uint16_t)((p_hpa >= 0 && p_hpa < 2000) ? p_hpa : 0);
        out->wmo_code = (uint8_t)code->valueint;
        out->is_day = (day->valueint != 0);
        time_t now = 0;
        time(&now);
        out->fetched_at = (int64_t)now;
        out->day_count = 0;
        memset(out->days, 0, sizeof(out->days));

        cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
        cJSON *d_time = daily ? cJSON_GetObjectItemCaseSensitive(daily, "time") : NULL;
        cJSON *d_code = daily ? cJSON_GetObjectItemCaseSensitive(daily, "weather_code") : NULL;
        cJSON *d_max = daily ? cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max") : NULL;
        cJSON *d_min = daily ? cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min") : NULL;
        cJSON *d_pop = daily ? cJSON_GetObjectItemCaseSensitive(daily, "precipitation_probability_max") : NULL;
        cJSON *d_sum = daily ? cJSON_GetObjectItemCaseSensitive(daily, "precipitation_sum") : NULL;
        cJSON *d_wnd = daily ? cJSON_GetObjectItemCaseSensitive(daily, "wind_speed_10m_max") : NULL;
        cJSON *d_sr = daily ? cJSON_GetObjectItemCaseSensitive(daily, "sunrise") : NULL;
        cJSON *d_ss = daily ? cJSON_GetObjectItemCaseSensitive(daily, "sunset") : NULL;

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
                weather_day_t *wd = &out->days[i];
                int yy = 2026, mm = 1, dd = 1;
                if (sscanf(it_t->valuestring, "%d-%d-%d", &yy, &mm, &dd) == 3) {
                    wd->wday = ymd_weekday(yy, mm, dd);
                } else {
                    wd->wday = (uint8_t)(i % 7);
                }
                wd->wmo_code = (uint8_t)it_c->valueint;
                wd->t_max = (int16_t)llround(it_hi->valuedouble);
                wd->t_min = (int16_t)llround(it_lo->valuedouble);

                cJSON *it_p = d_pop ? cJSON_GetArrayItem(d_pop, i) : NULL;
                cJSON *it_s = d_sum ? cJSON_GetArrayItem(d_sum, i) : NULL;
                cJSON *it_w = d_wnd ? cJSON_GetArrayItem(d_wnd, i) : NULL;
                cJSON *it_sr = d_sr ? cJSON_GetArrayItem(d_sr, i) : NULL;
                cJSON *it_ss = d_ss ? cJSON_GetArrayItem(d_ss, i) : NULL;

                wd->pop_max = cJSON_IsNumber(it_p) ? clamp_u8(it_p->valuedouble, 100) : 0;
                long rm = cJSON_IsNumber(it_s) ? lround(it_s->valuedouble * 10.0) : 0;
                wd->rain_mm10 = (uint16_t)((rm > 0 && rm < 65000) ? rm : 0);
                wd->wind_max = cJSON_IsNumber(it_w) ? clamp_u8(it_w->valuedouble, 250) : 0;
                wd->sr_h = 6; wd->sr_m = 30;
                wd->ss_h = 17; wd->ss_m = 30;
                if (cJSON_IsString(it_sr)) parse_hhmm(it_sr->valuestring, &wd->sr_h, &wd->sr_m);
                if (cJSON_IsString(it_ss)) parse_hhmm(it_ss->valuestring, &wd->ss_h, &wd->ss_m);

                out->day_count++;
            }
        }

        cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
        cJSON *h_temp = hourly ? cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m") : NULL;
        cJSON *h_code = hourly ? cJSON_GetObjectItemCaseSensitive(hourly, "weather_code") : NULL;
        cJSON *h_pop = hourly ? cJSON_GetObjectItemCaseSensitive(hourly, "precipitation_probability") : NULL;

        if (cJSON_IsArray(h_temp) && cJSON_IsArray(h_code)) {
            int hn = cJSON_GetArraySize(h_temp);
            for (int di = 0; di < out->day_count; di++) {
                for (int hi = 0; hi < WEATHER_MAX_HOURS; hi++) {
                    int idx = di * WEATHER_MAX_HOURS + hi;
                    if (idx >= hn) break;
                    cJSON *ht = cJSON_GetArrayItem(h_temp, idx);
                    cJSON *hc = cJSON_GetArrayItem(h_code, idx);
                    cJSON *hp = h_pop ? cJSON_GetArrayItem(h_pop, idx) : NULL;
                    if (!cJSON_IsNumber(ht) || !cJSON_IsNumber(hc)) break;
                    long tv = lround(ht->valuedouble);
                    if (tv < -99) tv = -99;
                    if (tv > 99) tv = 99;
                    out->days[di].hours[hi].temp_c = (int8_t)tv;
                    out->days[di].hours[hi].wmo_code = (uint8_t)hc->valueint;
                    out->days[di].hours[hi].pop_pct = cJSON_IsNumber(hp) ? clamp_u8(hp->valuedouble, 100) : 0;
                    out->days[di].hour_count++;
                }
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

    char url[512];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,surface_pressure,wind_speed_10m,is_day,weather_code"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum,wind_speed_10m_max,sunrise,sunset"
             "&hourly=temperature_2m,weather_code,precipitation_probability"
             "&forecast_days=7&timezone=auto",
             lat_snap, lon_snap);

    if (buf) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = 8000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (client) {
            esp_http_client_set_header(client, "Accept-Encoding", "identity");
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
                        if (strcmp(lat_snap, s_data.lat) == 0 && strcmp(lon_snap, s_data.lon) == 0) {
                            snprintf(next.city, sizeof(next.city), "%s", s_data.city);
                            snprintf(next.lat, sizeof(next.lat), "%s", s_data.lat);
                            snprintf(next.lon, sizeof(next.lon), "%s", s_data.lon);
                            s_data = next;
                            save_weather_nvs(&s_data);
                            ok = true;
                            ESP_LOGI(TAG, "%s: %d C, odcz=%d, dni=%u, godz[0]=%u",
                                     s_data.city, (int)s_data.temp_c, (int)s_data.feels_c,
                                     s_data.day_count, s_data.day_count ? s_data.days[0].hour_count : 0);
                            if (s_cb) s_cb(&s_data);
                        }
                    } else {
                        ESP_LOGW(TAG, "Blad parsowania JSON Open-Meteo (bytes=%d)", total);
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
    bool have_full = s_data.valid && s_data.day_count > 0 && s_data.days[0].hour_count > 0;
    if (have_full && s_data.fetched_at >= 1700000000LL) {
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
