#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define WEATHER_MAX_DAYS  7
#define WEATHER_MAX_HOURS 24

typedef struct {
    int8_t temp_c;
    uint8_t wmo_code;
    uint8_t pop_pct;
} weather_hour_t;

typedef struct {
    uint8_t wmo_code;
    int16_t t_max;
    int16_t t_min;
    uint8_t wday;       // 0=Nd, 1=Pon, 2=Wt, 3=Sr, 4=Czw, 5=Pt, 6=Sob
    uint8_t pop_max;    // szansa opadu 0..100
    uint16_t rain_mm10; // opad mm * 10
    uint8_t wind_max;   // max wiatr km/h
    uint8_t sr_h;
    uint8_t sr_m;
    uint8_t ss_h;
    uint8_t ss_m;
    uint8_t hour_count;
    weather_hour_t hours[WEATHER_MAX_HOURS];
} weather_day_t;

typedef struct {
    bool valid;
    int16_t temp_c;
    int16_t feels_c;
    uint8_t humidity;
    uint16_t pressure_hpa;
    uint8_t wind_kmh;
    uint8_t wmo_code;
    bool is_day;
    int64_t fetched_at;
    char city[32];
    char lat[16];
    char lon[16];
    uint8_t day_count;
    weather_day_t days[WEATHER_MAX_DAYS];
} weather_data_t;

typedef void (*weather_update_cb_t)(const weather_data_t *data);

void weather_init(weather_update_cb_t cb);
void weather_get_cached(weather_data_t *out);
void weather_get_city(char *city, size_t city_sz, char *lat, size_t lat_sz, char *lon, size_t lon_sz);
esp_err_t weather_set_city(const char *city, const char *lat, const char *lon);
void weather_poll_if_due(void);

#endif // WEATHER_H
