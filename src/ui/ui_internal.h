#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "midea_api.h"
#include "ui.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "screen_off.h"
#include "config.h"

typedef struct {
    ui_action_cb_t action_cb;
    midea_program_t selected_prog;
    bool extra_dry;
    bool door_closed;
    bool dismiss_finish;
    bool had_online_status;
    bool wifi_reconnecting;

    int last_pct;
    int last_left_min;
    int last_clock_key;
    bool last_door_closed;
    bool peek_active;
    lv_timer_t *peek_timer;
    int finish_hour;
    int finish_min;
    int sched_rem_sec;

    bool diag_active;
    lv_timer_t *diag_timer;
    int diag_rssi;
    uint32_t diag_latency_ms;
    char diag_ip[16];
    bool cached_rinse_lack;
    bool cached_salt_lack;
    weather_data_t weather;

    bool slot_top_eco;
    bool slot_bot_int;
    int64_t delay_opened_us;

    uint8_t saved_theme_idx;
    uint8_t preview_theme_idx;

    lv_obj_t *scr_loading;
    lv_obj_t *scr_main;

    lv_obj_t *load_card;
    lv_obj_t *load_brand;
    lv_obj_t *load_spinner;
    lv_obj_t *lbl_load_title;
    lv_obj_t *lbl_load_detail;

    lv_obj_t *pane_idle;
    lv_obj_t *pane_work;
    lv_obj_t *pane_finish;
    lv_obj_t *pane_offline;
    lv_obj_t *active_pane;

    lv_obj_t *tile_status;
    lv_obj_t *dot_idle_status;
    lv_obj_t *lbl_idle_clock;
    lv_obj_t *icon_weather;
    lv_obj_t *lbl_weather;
    lv_obj_t *lbl_idle_supply;
    lv_obj_t *tile_sleep;
    lv_obj_t *m_ic;
    lv_obj_t *lbl_sleep;

    lv_obj_t *btn_prog_1h;
    lv_obj_t *icon_1h_box;
    lv_obj_t *icon_1h_img;
    lv_obj_t *dot_1h_sel;
    lv_obj_t *lbl_1h_title;

    lv_obj_t *btn_prog_30;
    lv_obj_t *icon_30_box;
    lv_obj_t *icon_30_img;
    lv_obj_t *dot_30_sel;
    lv_obj_t *lbl_30_title;

    lv_obj_t *btn_big_start;
    lv_obj_t *obj_start_fill;
    lv_obj_t *icon_start_img;
    lv_obj_t *lbl_start_main;
    lv_obj_t *lbl_start_word;
    lv_obj_t *lbl_start_sub;

    lv_obj_t *tile_dial;
    lv_obj_t *dot_work;
    lv_obj_t *lbl_work_prog;
    lv_obj_t *btn_w_sleep;
    lv_obj_t *w_moon_ic;
    lv_obj_t *lbl_ws;
    lv_obj_t *arc_orbit;
    lv_obj_t *arc_work;
    lv_obj_t *lbl_work_time;
    lv_obj_t *lbl_work_unit;
    lv_obj_t *lbl_work_temp;
    lv_obj_t *lbl_work_peek_cap;
    lv_obj_t *lbl_work_peek_eta;
    lv_obj_t *lbl_work_peek_stage;
    lv_obj_t *lbl_work_door_warn;
    lv_obj_t *btn_stop;
    lv_obj_t *obj_stop_fill;
    lv_obj_t *lbl_stop;

    lv_obj_t *fin_card;
    lv_obj_t *chk_icon;
    lv_obj_t *lbl_fin_t;
    lv_obj_t *lbl_fin_s;
    lv_obj_t *lbl_finish_warn;
    lv_obj_t *btn_fin_sleep;
    lv_obj_t *fin_moon_ic;
    lv_obj_t *lbl_fs;

    lv_obj_t *err_card;
    lv_obj_t *err_ic;
    lv_obj_t *lbl_err_title;
    lv_obj_t *lbl_err_desc;
    lv_obj_t *btn_discover;

    lv_obj_t *overlay_busy;
    lv_obj_t *busy_spin;
    lv_obj_t *lbl_busy_msg;

    lv_obj_t *overlay_theme;
    lv_obj_t *th_dlg;
    lv_obj_t *lbl_theme_name;
    lv_obj_t *btn_th_prev;
    lv_obj_t *lbl_th_prev;
    lv_obj_t *btn_th_next;
    lv_obj_t *lbl_th_next;
    lv_obj_t *th_prev_bg;
    lv_obj_t *th_prev_top;
    lv_obj_t *th_prev_dot;
    lv_obj_t *th_prev_start;
    lv_obj_t *th_prev_prog;
    lv_obj_t *th_prev_arc;
    lv_obj_t *btn_th_ota;
    lv_obj_t *lbl_th_ota;
    lv_obj_t *btn_th_cancel;
    lv_obj_t *lbl_th_cancel;
    lv_obj_t *btn_th_bri;
    lv_obj_t *lbl_th_bri;

    lv_obj_t *overlay_delay;
    lv_obj_t *dl_dlg;
    lv_obj_t *lbl_delay_title;
    lv_obj_t *lbl_delay_sub;
    lv_obj_t *btn_dl_opts[4];
    lv_obj_t *lbl_dl_opts[4];
    lv_obj_t *btn_dl_back;
    lv_obj_t *lbl_dl_back;

    lv_obj_t *overlay_ota;
    lv_obj_t *ota_dlg;
    lv_obj_t *lbl_ota_title;
    lv_obj_t *lbl_ota_url;
    lv_obj_t *bar_ota_prog;
    lv_obj_t *lbl_ota_desc;
    lv_obj_t *btn_ota_close;
    lv_obj_t *lbl_ota_close;

    lv_obj_t *overlay_weather;
    lv_obj_t *w_dlg;
    lv_obj_t *lbl_w_city;
    lv_obj_t *lbl_w_empty;
    lv_obj_t *btn_w_close;
    lv_obj_t *lbl_w_close;
    int8_t w_sel_day;
    lv_obj_t *w_week_box;
    lv_obj_t *lbl_w_summary;
    lv_obj_t *w_col[WEATHER_MAX_DAYS];
    lv_obj_t *lbl_w_day[WEATHER_MAX_DAYS];
    lv_obj_t *ic_w_day[WEATHER_MAX_DAYS];
    lv_obj_t *lbl_w_hi[WEATHER_MAX_DAYS];
    lv_obj_t *lbl_w_lo[WEATHER_MAX_DAYS];
    lv_obj_t *lbl_w_pop[WEATHER_MAX_DAYS];
    lv_obj_t *w_day_box;
    lv_obj_t *btn_w_back;
    lv_obj_t *lbl_w_back;
    lv_obj_t *w_det_card;
    lv_obj_t *ic_w_det;
    lv_obj_t *lbl_w_det_temp;
    lv_obj_t *lbl_w_det_sun;
    lv_obj_t *lbl_w_det_meta;
    lv_obj_t *w_hour_row;
    lv_obj_t *w_hour_card[WEATHER_MAX_HOURS];
    lv_obj_t *lbl_wh_time[WEATHER_MAX_HOURS];
    lv_obj_t *ic_wh[WEATHER_MAX_HOURS];
    lv_obj_t *lbl_wh_temp[WEATHER_MAX_HOURS];
    lv_obj_t *lbl_wh_pop[WEATHER_MAX_HOURS];
} ui_ctx_t;

extern ui_ctx_t ui;

// wspolne helpery w ui.c
bool ui_is_pane_visible(lv_obj_t *pane);
void ui_start_hold_fill_anim(lv_obj_t *fill_obj, int32_t target_w, uint32_t delay_ms,
                             uint32_t dur_ms, lv_anim_ready_cb_t done_cb);
void ui_cancel_hold_fill_anim(lv_obj_t *fill_obj, bool retract);
bool ui_point_in_obj(lv_obj_t *obj, const lv_point_t *pt);
lv_obj_t *ui_make_bento_container(lv_obj_t *parent, bool hidden);
lv_obj_t *ui_make_overlay_backdrop(lv_obj_t *parent);
void ui_show_only_pane(lv_obj_t *active);
void ui_apply_theme_colors(void);
void ui_on_sleep_now(lv_event_t *e);

// ui_idle.c
void ui_create_idle_finish_offline_panes(lv_obj_t *scr);
const char *ui_get_selected_prog_name(void);
void ui_idle_update_visuals(void);
void ui_idle_refresh_supply_banner(void);
void ui_idle_refresh_weather(void);
void ui_pick_weather_icon(const ui_palette_t *p, uint8_t code, bool day,
                          const lv_img_dsc_t **out_ic, lv_color_t *out_col);
const char *ui_wmo_desc(uint8_t code, bool day);
void ui_idle_apply_theme(const ui_palette_t *p);

// ui_work.c
void ui_create_work_pane(lv_obj_t *scr);
void ui_work_update_center_view(void);
void ui_work_apply_theme(const ui_palette_t *p);
void ui_work_update_status(const midea_status_t *status);

// ui_overlays.c
void ui_create_loading_screen(void);
void ui_create_overlays(lv_obj_t *scr);
void ui_open_theme_menu(void);
void ui_open_delay_picker(void);
void ui_open_weather_forecast(void);
void ui_refresh_weather_forecast(void);
void ui_refresh_theme_preview(void);
void ui_overlays_apply_theme(const ui_palette_t *p);

#endif // UI_INTERNAL_H
