/**
 * Zmywarka ESP - 2026 Bento Mosaic UI Controller
 */

#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "midea_api.h"
#include "weather.h"

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_START_1H,
    UI_ACTION_START_1H_DRY,
    UI_ACTION_START_30MIN,
    UI_ACTION_START_ECO,
    UI_ACTION_START_INTENSIVE,
    UI_ACTION_CANCEL,
    UI_ACTION_RETRY,
    UI_ACTION_DISCOVER_IP,
    UI_ACTION_SLEEP_NOW,
    UI_ACTION_SCHED_30M,
    UI_ACTION_SCHED_1H,
    UI_ACTION_SCHED_2H,
    UI_ACTION_SCHED_3H,
    UI_ACTION_SCHED_CANCEL,
    UI_ACTION_OTA_OPEN,
    UI_ACTION_OTA_CLOSE
} ui_action_t;

typedef void (*ui_action_cb_t)(ui_action_t action);

void ui_init(ui_action_cb_t action_cb);
void ui_show_loading(const char *headline, const char *detail);
void ui_prepare_wake_screen(void);
bool ui_is_on_main_screen(void);
void ui_show_main(void);
void ui_update_status(const midea_status_t *status, bool wifi_ok, int rssi);
void ui_update_clock(void);
void ui_set_busy(bool busy, const char *msg);
void ui_dismiss_finish_view(void);
bool ui_is_finish_dismissed(void);
void ui_get_selected_options(midea_program_t *out_prog, bool *out_extra_dry);
void ui_set_selected_options(midea_program_t prog, bool extra_dry);
void ui_set_schedule_countdown(int remaining_sec);
void ui_set_weather(const weather_data_t *w);
void ui_set_ota_url(const char *ip_str);
void ui_set_ota_progress(int pct, bool finished, bool ok);

#endif // UI_H
