#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_BRI_AUTO = 0, // Day 100%, Evening 60%, Night 35%
    SCREEN_BRI_100  = 1, // Always 100% when awake
    SCREEN_BRI_50   = 2  // Always 50% when awake
} screen_bri_mode_t;

typedef void (*screen_power_cb_t)(bool sleeping);

esp_err_t screen_off_init(uint32_t timeout_ms);
void screen_off_deinit(void);
void screen_off_activity(void);
void screen_off_force_off(void);
void screen_off_set_keep_awake(bool keep_awake);
void screen_off_set_finish_hold(bool finish_active);
uint8_t screen_off_get_bri_mode(void);
uint8_t screen_off_cycle_bri_mode(void);
bool screen_off_is_off(void);
bool screen_off_is_flush_paused(void);
void screen_off_set_sleep_prerender(bool active);
bool screen_off_has_pending_wake_backlight(void);
void screen_off_complete_wake_backlight(void);
void screen_off_set_callback(screen_power_cb_t cb);

#ifdef __cplusplus
}
#endif
