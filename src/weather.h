#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define WEATHER_MAX_DAYS 7

typedef struct {
    uint8_t wmo_code;
    int16_t t_max;
    int16_t t_min;
    uint8_t wday; // 0=Nd, 1=Pon, 2=Wt, 3=Sr, 4=Czw, 5=Pt, 6=Sob
} weather_day_t;

typedef struct {
    bool valid;
    int16_t temp_c;
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
