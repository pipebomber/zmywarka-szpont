#include <time.h>
#include "esp_timer.h"
#include "ui_internal.h"

void ui_refresh_theme_preview(void)
{
    if (!ui.th_prev_bg) return;
    const ui_palette_t *p = ui_theme_by_idx(ui.preview_theme_idx);
    if (ui.lbl_theme_name) {
        lv_label_set_text_fmt(ui.lbl_theme_name, "%u/%u · %s",
                              (unsigned)(ui.preview_theme_idx + 1),
                              (unsigned)ui_theme_count(), p->name);
    }
    lv_obj_set_style_bg_color(ui.th_prev_bg, lv_color_hex(p->bg), 0);
    lv_obj_set_style_border_color(ui.th_prev_bg, lv_color_hex(p->border_light), 0);
    if (ui.th_prev_top) {
        lv_obj_set_style_bg_color(ui.th_prev_top, lv_color_hex(p->tile), 0);
        lv_obj_set_style_border_color(ui.th_prev_top, lv_color_hex(p->border), 0);
    }
    if (ui.th_prev_dot) lv_obj_set_style_bg_color(ui.th_prev_dot, lv_color_hex(p->mint), 0);
    if (ui.th_prev_start) {
        lv_obj_set_style_bg_color(ui.th_prev_start, lv_color_hex(p->start_top), 0);
        lv_obj_set_style_bg_grad_color(ui.th_prev_start, lv_color_hex(p->start_bot), 0);
        lv_obj_set_style_border_color(ui.th_prev_start, lv_color_hex(p->start_border), 0);
    }
    if (ui.th_prev_prog) {
        lv_obj_set_style_bg_color(ui.th_prev_prog, lv_color_hex(p->sel_bg), 0);
        lv_obj_set_style_border_color(ui.th_prev_prog, lv_color_hex(p->sel_border), 0);
    }
    if (ui.th_prev_arc) {
        lv_obj_set_style_arc_color(ui.th_prev_arc, lv_color_hex(p->sleep_bg), LV_PART_MAIN);
        lv_obj_set_style_arc_color(ui.th_prev_arc, lv_color_hex(p->accent), LV_PART_INDICATOR);
    }
}

static void refresh_bri_btn_label(void)
{
    if (!ui.lbl_th_bri) return;
    uint8_t m = screen_off_get_bri_mode();
    if (m == SCREEN_BRI_100) lv_label_set_text(ui.lbl_th_bri, "Jasność: 100%");
    else if (m == SCREEN_BRI_50) lv_label_set_text(ui.lbl_th_bri, "Jasność: 50%");
    else lv_label_set_text(ui.lbl_th_bri, "Jasność: Auto");
}

static void on_bri_cycle(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    screen_off_cycle_bri_mode();
    refresh_bri_btn_label();
}

void ui_open_theme_menu(void)
{
    ui.diag_active = false;
    if (ui.diag_timer) lv_timer_pause(ui.diag_timer);
    ui_idle_refresh_supply_banner();
    if (!ui.overlay_theme) return;
    ui.saved_theme_idx = ui_theme_get_idx();
    ui.preview_theme_idx = ui.saved_theme_idx;
    ui_refresh_theme_preview();
    refresh_bri_btn_label();
    lv_obj_clear_flag(ui.overlay_theme, LV_OBJ_FLAG_HIDDEN);
}

static void on_theme_step(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    uint8_t n = ui_theme_count();
    ui.preview_theme_idx = (uint8_t)((ui.preview_theme_idx + n + delta) % n);
    ui_theme_set(ui.preview_theme_idx);
    ui_apply_theme_colors();
}

static void on_theme_save(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_theme_save(ui.preview_theme_idx);
    ui_apply_theme_colors();
    if (ui.overlay_theme) lv_obj_add_flag(ui.overlay_theme, LV_OBJ_FLAG_HIDDEN);
}

static void on_theme_cancel(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_theme_set(ui.saved_theme_idx);
    ui_apply_theme_colors();
    if (ui.overlay_theme) lv_obj_add_flag(ui.overlay_theme, LV_OBJ_FLAG_HIDDEN);
}

static void on_theme_ota(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_theme_set(ui.saved_theme_idx);
    ui_apply_theme_colors();
    if (ui.overlay_theme) lv_obj_add_flag(ui.overlay_theme, LV_OBJ_FLAG_HIDDEN);
    if (ui.bar_ota_prog) {
        lv_obj_add_flag(ui.bar_ota_prog, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(ui.bar_ota_prog, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(ui.bar_ota_prog, TH_CYAN, LV_PART_INDICATOR);
    }
    if (ui.btn_ota_close) lv_obj_clear_state(ui.btn_ota_close, LV_STATE_DISABLED);
    if (ui.lbl_ota_desc) {
        lv_label_set_text(ui.lbl_ota_desc, "Otwórz ten adres w przeglądarce\ni wyślij plik firmware.bin");
    }
    if (ui.overlay_ota) lv_obj_clear_flag(ui.overlay_ota, LV_OBJ_FLAG_HIDDEN);
    if (ui.action_cb) ui.action_cb(UI_ACTION_OTA_OPEN);
}

static void on_ota_close(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui.overlay_ota) lv_obj_add_flag(ui.overlay_ota, LV_OBJ_FLAG_HIDDEN);
    if (ui.action_cb) ui.action_cb(UI_ACTION_OTA_CLOSE);
}

void ui_open_delay_picker(void)
{
    if (!ui.door_closed || ui.wifi_reconnecting || !ui.overlay_delay) return;
    if (!lv_obj_has_flag(ui.overlay_delay, LV_OBJ_FLAG_HIDDEN)) return;
    ui_idle_update_visuals();
    ui.delay_opened_us = esp_timer_get_time();
    if (ui.lbl_delay_sub) lv_label_set_text_fmt(ui.lbl_delay_sub, "Program: %s", ui_get_selected_prog_name());
    if (ui.lbl_dl_back) lv_label_set_text(ui.lbl_dl_back, (ui.sched_rem_sec > 0) ? "Anuluj plan" : "Wróć");
    lv_obj_clear_flag(ui.overlay_delay, LV_OBJ_FLAG_HIDDEN);
}

static void on_weather_close(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (lv_event_get_target(e) != ui.overlay_weather && lv_event_get_target(e) != ui.btn_w_close) {
        return;
    }
    if (ui.overlay_weather) lv_obj_add_flag(ui.overlay_weather, LV_OBJ_FLAG_HIDDEN);
}

static void on_weather_back(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui.w_sel_day = -1;
    ui_refresh_weather_forecast();
}

static void on_weather_day_tap(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < ui.weather.day_count) {
        ui.w_sel_day = (int8_t)idx;
        ui_refresh_weather_forecast();
    }
}

static int current_local_hour(void)
{
    time_t now = 0;
    struct tm t = {0};
    time(&now);
    localtime_r(&now, &t);
    if (t.tm_year < (2024 - 1900)) return -1;
    return t.tm_hour;
}

static void refresh_weather_day_detail(const ui_palette_t *p, const weather_day_t *d,
                                       int day_idx, const char *day_name)
{
    if (ui.btn_w_back) lv_obj_clear_flag(ui.btn_w_back, LV_OBJ_FLAG_HIDDEN);
    if (ui.lbl_w_city) {
        lv_obj_set_width(ui.lbl_w_city, 244);
        lv_obj_align(ui.lbl_w_city, LV_ALIGN_TOP_LEFT, 92, 4);
        lv_label_set_text_fmt(ui.lbl_w_city, "%s · %s", day_name, ui_wmo_desc(d->wmo_code, true));
    }

    if (ui.w_det_card) {
        lv_obj_set_style_bg_color(ui.w_det_card, lv_color_hex(p->sleep_bg), 0);
        lv_obj_set_style_border_color(ui.w_det_card, lv_color_hex(p->border_light), 0);
    }

    const lv_img_dsc_t *ic = &ic_w_sun;
    lv_color_t col = lv_color_hex(0xF59E0B);
    ui_pick_weather_icon(p, d->wmo_code, true, &ic, &col);
    if (ui.ic_w_det) {
        lv_img_set_src(ui.ic_w_det, ic);
        lv_obj_set_style_img_recolor(ui.ic_w_det, col, 0);
    }

    if (ui.lbl_w_det_temp) {
        lv_obj_set_style_text_color(ui.lbl_w_det_temp, lv_color_hex(p->text), 0);
        if (day_idx == 0) {
            lv_label_set_text_fmt(ui.lbl_w_det_temp, "%d° / %d°  (odcz. %d°)",
                                  (int)d->t_max, (int)d->t_min, (int)ui.weather.feels_c);
        } else {
            lv_label_set_text_fmt(ui.lbl_w_det_temp, "%d° / %d°", (int)d->t_max, (int)d->t_min);
        }
    }

    if (ui.lbl_w_det_sun) {
        lv_obj_set_style_text_color(ui.lbl_w_det_sun, lv_color_hex(p->text_sub), 0);
        lv_label_set_text_fmt(ui.lbl_w_det_sun, "%02u:%02u - %02u:%02u",
                              d->sr_h, d->sr_m, d->ss_h, d->ss_m);
    }

    if (ui.lbl_w_det_meta) {
        lv_obj_set_style_text_color(ui.lbl_w_det_meta, lv_color_hex(p->text_sub), 0);
        if (day_idx == 0 && ui.weather.pressure_hpa > 0) {
            lv_label_set_text_fmt(ui.lbl_w_det_meta, "Opad: %u%% (%u.%u mm) · Wiatr: %u km/h · %u%% · %u hPa",
                                  d->pop_max, (unsigned)(d->rain_mm10 / 10), (unsigned)(d->rain_mm10 % 10),
                                  ui.weather.wind_kmh, ui.weather.humidity, (unsigned)ui.weather.pressure_hpa);
        } else {
            lv_label_set_text_fmt(ui.lbl_w_det_meta, "Opad: %u%% (%u.%u mm) · Max wiatr: %u km/h",
                                  d->pop_max, (unsigned)(d->rain_mm10 / 10), (unsigned)(d->rain_mm10 % 10),
                                  d->wind_max);
        }
    }

    int cur_h = (day_idx == 0) ? current_local_hour() : -1;
    int sr_min = d->sr_h * 60 + d->sr_m;
    int ss_min = d->ss_h * 60 + d->ss_m;
    lv_color_t c_rain = p->is_light ? lv_color_hex(0x0284C7) : lv_color_hex(0x38BDF8);

    for (int h = 0; h < WEATHER_MAX_HOURS; h++) {
        if (!ui.w_hour_card[h]) continue;
        if (h >= d->hour_count) {
            lv_obj_add_flag(ui.w_hour_card[h], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(ui.w_hour_card[h], LV_OBJ_FLAG_HIDDEN);

        bool is_now = (day_idx == 0 && cur_h >= 0 && h == cur_h);
        lv_obj_set_style_bg_color(ui.w_hour_card[h], lv_color_hex(is_now ? p->sel_bg : p->sleep_bg), 0);
        lv_obj_set_style_border_color(ui.w_hour_card[h], lv_color_hex(is_now ? p->accent : p->border_light), 0);

        const weather_hour_t *hr = &d->hours[h];
        lv_label_set_text_fmt(ui.lbl_wh_time[h], "%02d:00", h);
        lv_obj_set_style_text_color(ui.lbl_wh_time[h], lv_color_hex(is_now ? p->accent : p->text_sub), 0);

        int h_min = h * 60 + 30;
        bool h_day = (h_min >= sr_min && h_min < ss_min);
        const lv_img_dsc_t *hic = &ic_w_sun;
        lv_color_t hcol = lv_color_hex(0xF59E0B);
        ui_pick_weather_icon(p, hr->wmo_code, h_day, &hic, &hcol);
        lv_img_set_src(ui.ic_wh[h], hic);
        lv_obj_set_style_img_recolor(ui.ic_wh[h], hcol, 0);

        lv_label_set_text_fmt(ui.lbl_wh_temp[h], "%d°", (int)hr->temp_c);
        lv_obj_set_style_text_color(ui.lbl_wh_temp[h], lv_color_hex(p->text), 0);

        lv_label_set_text_fmt(ui.lbl_wh_pop[h], "%u%%", hr->pop_pct);
        lv_obj_set_style_text_color(ui.lbl_wh_pop[h], hr->pop_pct >= 20 ? c_rain : lv_color_hex(p->muted), 0);
    }

    if (ui.w_hour_row) {
        int focus_h = (day_idx == 0 && cur_h >= 0) ? cur_h : 6;
        if (focus_h > WEATHER_MAX_HOURS - 6) focus_h = WEATHER_MAX_HOURS - 6;
        if (focus_h < 0) focus_h = 0;
        lv_obj_update_layout(ui.w_hour_row);
        lv_obj_scroll_to_x(ui.w_hour_row, focus_h * 65, LV_ANIM_OFF);
    }
}

void ui_refresh_weather_forecast(void)
{
    if (!ui.w_dlg) return;

    const ui_palette_t *p = ui_theme_get();
    static const char *WDAYS[7] = {"Nd", "Pon", "Wt", "Śr", "Czw", "Pt", "Sob"};
    lv_color_t c_rain = p->is_light ? lv_color_hex(0x0284C7) : lv_color_hex(0x38BDF8);

    if (ui.w_sel_day >= ui.weather.day_count) {
        ui.w_sel_day = -1;
    }

    if (ui.lbl_w_empty) {
        if (ui.weather.day_count == 0) {
            lv_obj_clear_flag(ui.lbl_w_empty, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(ui.lbl_w_empty, lv_color_hex(p->text_sub), 0);
        } else {
            lv_obj_add_flag(ui.lbl_w_empty, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (ui.w_sel_day >= 0 && ui.w_sel_day < ui.weather.day_count) {
        if (ui.w_week_box) lv_obj_add_flag(ui.w_week_box, LV_OBJ_FLAG_HIDDEN);
        if (ui.w_day_box) lv_obj_clear_flag(ui.w_day_box, LV_OBJ_FLAG_HIDDEN);
        const weather_day_t *d = &ui.weather.days[ui.w_sel_day];
        const char *dname = (ui.w_sel_day == 0) ? "Dziś" : WDAYS[d->wday % 7];
        refresh_weather_day_detail(p, d, ui.w_sel_day, dname);
        return;
    }

    if (ui.btn_w_back) lv_obj_add_flag(ui.btn_w_back, LV_OBJ_FLAG_HIDDEN);
    if (ui.w_day_box) lv_obj_add_flag(ui.w_day_box, LV_OBJ_FLAG_HIDDEN);
    if (ui.w_week_box) lv_obj_clear_flag(ui.w_week_box, LV_OBJ_FLAG_HIDDEN);

    if (ui.lbl_w_city) {
        const char *city = ui.weather.city[0] ? ui.weather.city : "Wrocław";
        lv_obj_set_width(ui.lbl_w_city, 332);
        lv_obj_align(ui.lbl_w_city, LV_ALIGN_TOP_LEFT, 0, 4);
        lv_label_set_text_fmt(ui.lbl_w_city, "Pogoda · %s", city);
    }

    if (ui.lbl_w_summary) {
        lv_obj_set_style_text_color(ui.lbl_w_summary, lv_color_hex(p->text_sub), 0);
        if (ui.weather.pressure_hpa > 0) {
            lv_label_set_text_fmt(ui.lbl_w_summary, "%s · Odcz. %d° · Wilg. %u%% · Wiatr %u km/h · %u hPa",
                                  ui_wmo_desc(ui.weather.wmo_code, ui.weather.is_day),
                                  (int)ui.weather.feels_c, ui.weather.humidity,
                                  ui.weather.wind_kmh, (unsigned)ui.weather.pressure_hpa);
        } else {
            lv_label_set_text(ui.lbl_w_summary, ui_wmo_desc(ui.weather.wmo_code, ui.weather.is_day));
        }
    }

    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        if (!ui.w_col[i]) continue;
        if (i >= ui.weather.day_count) {
            lv_obj_add_flag(ui.w_col[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(ui.w_col[i], LV_OBJ_FLAG_HIDDEN);

        bool is_today = (i == 0);
        lv_obj_set_style_bg_color(ui.w_col[i], lv_color_hex(is_today ? p->sel_bg : p->sleep_bg), 0);
        lv_obj_set_style_border_color(ui.w_col[i], lv_color_hex(is_today ? p->accent : p->border_light), 0);

        const weather_day_t *d = &ui.weather.days[i];
        const char *dname = is_today ? "Dziś" : WDAYS[d->wday % 7];
        lv_label_set_text(ui.lbl_w_day[i], dname);
        lv_obj_set_style_text_color(ui.lbl_w_day[i], lv_color_hex(is_today ? p->accent : p->text_sub), 0);

        const lv_img_dsc_t *ic = &ic_w_sun;
        lv_color_t col = lv_color_hex(0xF59E0B);
        ui_pick_weather_icon(p, d->wmo_code, true, &ic, &col);
        lv_img_set_src(ui.ic_w_day[i], ic);
        lv_obj_set_style_img_recolor(ui.ic_w_day[i], col, 0);

        lv_label_set_text_fmt(ui.lbl_w_hi[i], "%d°", (int)d->t_max);
        lv_obj_set_style_text_color(ui.lbl_w_hi[i], lv_color_hex(p->text), 0);
        lv_label_set_text_fmt(ui.lbl_w_lo[i], "%d°", (int)d->t_min);
        lv_obj_set_style_text_color(ui.lbl_w_lo[i], lv_color_hex(p->text_sub), 0);
        if (ui.lbl_w_pop[i]) {
            lv_label_set_text_fmt(ui.lbl_w_pop[i], "%u%%", d->pop_max);
            lv_obj_set_style_text_color(ui.lbl_w_pop[i], d->pop_max >= 20 ? c_rain : lv_color_hex(p->muted), 0);
        }
    }
}

void ui_open_weather_forecast(void)
{
    if (!ui.overlay_weather || !ui.weather.valid) return;
    ui.w_sel_day = -1;
    ui_refresh_weather_forecast();
    lv_obj_clear_flag(ui.overlay_weather, LV_OBJ_FLAG_HIDDEN);
}

static void on_delay_pick(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if ((esp_timer_get_time() - ui.delay_opened_us) < 350000LL) return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (ui.overlay_delay) lv_obj_add_flag(ui.overlay_delay, LV_OBJ_FLAG_HIDDEN);

    static const ui_action_t DL_ACTIONS[4] = {
        UI_ACTION_SCHED_30M,
        UI_ACTION_SCHED_1H,
        UI_ACTION_SCHED_2H,
        UI_ACTION_SCHED_3H,
    };
    if (idx >= 0 && idx < 4 && ui.action_cb) {
        ui.action_cb(DL_ACTIONS[idx]);
    }
}

static void on_delay_back(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui.overlay_delay) lv_obj_add_flag(ui.overlay_delay, LV_OBJ_FLAG_HIDDEN);
    if (ui.sched_rem_sec > 0 && ui.action_cb) {
        ui.action_cb(UI_ACTION_SCHED_CANCEL);
    }
}

void ui_create_loading_screen(void)
{
    ui.scr_loading = lv_obj_create(NULL);
    lv_obj_add_style(ui.scr_loading, &st_screen, 0);
    lv_obj_clear_flag(ui.scr_loading, LV_OBJ_FLAG_SCROLLABLE);

    ui.load_card = lv_obj_create(ui.scr_loading);
    lv_obj_set_size(ui.load_card, 380, 220);
    lv_obj_add_style(ui.load_card, &st_bento_tile, 0);
    lv_obj_clear_flag(ui.load_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ui.load_card);

    ui.load_brand = lv_label_create(ui.load_card);
    lv_label_set_text(ui.load_brand, "ZMYWARKA");
    lv_obj_set_style_text_font(ui.load_brand, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.load_brand, TH_CYAN, 0);
    lv_obj_align(ui.load_brand, LV_ALIGN_TOP_MID, 0, 4);

    ui.load_spinner = lv_spinner_create(ui.load_card, 1800, 75);
    lv_obj_set_size(ui.load_spinner, 60, 60);
    lv_obj_set_style_arc_color(ui.load_spinner, lv_color_hex(ui_theme_get()->sleep_bg), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui.load_spinner, TH_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ui.load_spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ui.load_spinner, 6, LV_PART_INDICATOR);
    lv_obj_align(ui.load_spinner, LV_ALIGN_CENTER, 0, -6);

    ui.lbl_load_title = lv_label_create(ui.load_card);
    lv_label_set_text(ui.lbl_load_title, "Łączenie z siecią Wi-Fi...");
    lv_obj_set_style_text_font(ui.lbl_load_title, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_load_title, TH_TEXT, 0);
    lv_obj_align(ui.lbl_load_title, LV_ALIGN_BOTTOM_MID, 0, -32);

    ui.lbl_load_detail = lv_label_create(ui.load_card);
    lv_label_set_text(ui.lbl_load_detail, "Proszę chwilkę poczekać");
    lv_obj_set_style_text_font(ui.lbl_load_detail, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_load_detail, TH_TEXT_SUB, 0);
    lv_obj_align(ui.lbl_load_detail, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void ui_create_overlays(lv_obj_t *scr)
{
    const ui_palette_t *p = ui_theme_get();

    // nakladka wysylania komendy
    ui.overlay_busy = ui_make_overlay_backdrop(scr);

    ui.busy_spin = lv_spinner_create(ui.overlay_busy, 1500, 75);
    lv_obj_set_size(ui.busy_spin, 74, 74);
    lv_obj_set_style_arc_color(ui.busy_spin, lv_color_hex(p->sleep_bg), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui.busy_spin, TH_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ui.busy_spin, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ui.busy_spin, 7, LV_PART_INDICATOR);
    lv_obj_align(ui.busy_spin, LV_ALIGN_CENTER, 0, -24);

    ui.lbl_busy_msg = lv_label_create(ui.overlay_busy);
    lv_label_set_text(ui.lbl_busy_msg, "Wysyłanie polecenia...");
    lv_obj_set_style_text_font(ui.lbl_busy_msg, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_busy_msg, TH_TEXT, 0);
    lv_obj_align(ui.lbl_busy_msg, LV_ALIGN_CENTER, 0, 44);

    // nakladka wyboru motywu, jasnosci i OTA
    ui.overlay_theme = ui_make_overlay_backdrop(scr);

    ui.th_dlg = lv_obj_create(ui.overlay_theme);
    lv_obj_set_size(ui.th_dlg, 448, 286);
    lv_obj_add_style(ui.th_dlg, &st_bento_tile, 0);
    lv_obj_set_style_border_color(ui.th_dlg, TH_CYAN, 0);
    lv_obj_set_style_border_width(ui.th_dlg, 2, 0);
    lv_obj_clear_flag(ui.th_dlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ui.th_dlg);

    ui.lbl_theme_name = lv_label_create(ui.th_dlg);
    lv_obj_set_width(ui.lbl_theme_name, 272);
    lv_label_set_long_mode(ui.lbl_theme_name, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui.lbl_theme_name, "1/9 · Domyślny");
    lv_obj_set_style_text_font(ui.lbl_theme_name, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_theme_name, TH_TEXT, 0);
    lv_obj_align(ui.lbl_theme_name, LV_ALIGN_TOP_LEFT, 4, 8);

    ui.btn_th_bri = lv_btn_create(ui.th_dlg);
    lv_obj_set_size(ui.btn_th_bri, 134, 34);
    lv_obj_add_style(ui.btn_th_bri, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_th_bri, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_th_bri, 10, 0);
    lv_obj_set_style_shadow_width(ui.btn_th_bri, 0, 0);
    lv_obj_align(ui.btn_th_bri, LV_ALIGN_TOP_RIGHT, -2, 0);
    lv_obj_add_event_cb(ui.btn_th_bri, on_bri_cycle, LV_EVENT_CLICKED, NULL);

    ui.lbl_th_bri = lv_label_create(ui.btn_th_bri);
    lv_label_set_text(ui.lbl_th_bri, "Jasność: Auto");
    lv_obj_set_style_text_font(ui.lbl_th_bri, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_th_bri, TH_CYAN, 0);
    lv_obj_center(ui.lbl_th_bri);
    refresh_bri_btn_label();

    ui.btn_th_prev = lv_btn_create(ui.th_dlg);
    lv_obj_set_size(ui.btn_th_prev, 56, 124);
    lv_obj_add_style(ui.btn_th_prev, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_th_prev, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_th_prev, 14, 0);
    lv_obj_set_style_shadow_width(ui.btn_th_prev, 0, 0);
    lv_obj_align(ui.btn_th_prev, LV_ALIGN_LEFT_MID, 2, -6);
    lv_obj_add_event_cb(ui.btn_th_prev, on_theme_step, LV_EVENT_CLICKED, (void *)(intptr_t)(-1));

    ui.lbl_th_prev = lv_label_create(ui.btn_th_prev);
    lv_label_set_text(ui.lbl_th_prev, "<");
    lv_obj_set_style_text_font(ui.lbl_th_prev, &pl_font_30, 0);
    lv_obj_set_style_text_color(ui.lbl_th_prev, TH_TEXT, 0);
    lv_obj_center(ui.lbl_th_prev);

    ui.btn_th_next = lv_btn_create(ui.th_dlg);
    lv_obj_set_size(ui.btn_th_next, 56, 124);
    lv_obj_add_style(ui.btn_th_next, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_th_next, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_th_next, 14, 0);
    lv_obj_set_style_shadow_width(ui.btn_th_next, 0, 0);
    lv_obj_align(ui.btn_th_next, LV_ALIGN_RIGHT_MID, -2, -6);
    lv_obj_add_event_cb(ui.btn_th_next, on_theme_step, LV_EVENT_CLICKED, (void *)(intptr_t)(+1));

    ui.lbl_th_next = lv_label_create(ui.btn_th_next);
    lv_label_set_text(ui.lbl_th_next, ">");
    lv_obj_set_style_text_font(ui.lbl_th_next, &pl_font_30, 0);
    lv_obj_set_style_text_color(ui.lbl_th_next, TH_TEXT, 0);
    lv_obj_center(ui.lbl_th_next);

    ui.th_prev_bg = lv_obj_create(ui.th_dlg);
    lv_obj_set_size(ui.th_prev_bg, 290, 144);
    lv_obj_set_style_radius(ui.th_prev_bg, 14, 0);
    lv_obj_set_style_border_width(ui.th_prev_bg, 2, 0);
    lv_obj_set_style_pad_all(ui.th_prev_bg, 8, 0);
    lv_obj_clear_flag(ui.th_prev_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ui.th_prev_bg, LV_ALIGN_CENTER, 0, -6);

    ui.th_prev_top = lv_obj_create(ui.th_prev_bg);
    lv_obj_set_size(ui.th_prev_top, 270, 22);
    lv_obj_set_style_radius(ui.th_prev_top, 8, 0);
    lv_obj_set_style_border_width(ui.th_prev_top, 1, 0);
    lv_obj_set_style_pad_all(ui.th_prev_top, 4, 0);
    lv_obj_clear_flag(ui.th_prev_top, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ui.th_prev_top, LV_ALIGN_TOP_MID, 0, 0);

    ui.th_prev_dot = lv_obj_create(ui.th_prev_top);
    lv_obj_set_size(ui.th_prev_dot, 8, 8);
    lv_obj_set_style_radius(ui.th_prev_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui.th_prev_dot, 0, 0);
    lv_obj_align(ui.th_prev_dot, LV_ALIGN_LEFT_MID, 2, 0);

    ui.th_prev_start = lv_obj_create(ui.th_prev_bg);
    lv_obj_set_size(ui.th_prev_start, 144, 92);
    lv_obj_set_style_radius(ui.th_prev_start, 12, 0);
    lv_obj_set_style_bg_grad_dir(ui.th_prev_start, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(ui.th_prev_start, 2, 0);
    lv_obj_clear_flag(ui.th_prev_start, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(ui.th_prev_start, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *tps = lv_label_create(ui.th_prev_start);
    lv_label_set_text(tps, "START");
    lv_obj_set_style_text_font(tps, &pl_font_22, 0);
    lv_obj_set_style_text_color(tps, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(tps);

    ui.th_prev_prog = lv_obj_create(ui.th_prev_bg);
    lv_obj_set_size(ui.th_prev_prog, 118, 42);
    lv_obj_set_style_radius(ui.th_prev_prog, 10, 0);
    lv_obj_set_style_border_width(ui.th_prev_prog, 2, 0);
    lv_obj_clear_flag(ui.th_prev_prog, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ui.th_prev_prog, 152, 28);

    ui.th_prev_arc = lv_arc_create(ui.th_prev_bg);
    lv_obj_set_size(ui.th_prev_arc, 44, 44);
    lv_arc_set_bg_angles(ui.th_prev_arc, 0, 360);
    lv_arc_set_range(ui.th_prev_arc, 0, 100);
    lv_arc_set_value(ui.th_prev_arc, 65);
    lv_arc_set_rotation(ui.th_prev_arc, 270);
    lv_obj_clear_flag(ui.th_prev_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(ui.th_prev_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(ui.th_prev_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ui.th_prev_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_pos(ui.th_prev_arc, 190, 76);

    ui.btn_th_ota = lv_btn_create(ui.th_dlg);
    lv_obj_set_size(ui.btn_th_ota, 126, 48);
    lv_obj_add_style(ui.btn_th_ota, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_th_ota, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_th_ota, 14, 0);
    lv_obj_set_style_shadow_width(ui.btn_th_ota, 0, 0);
    lv_obj_align(ui.btn_th_ota, LV_ALIGN_BOTTOM_LEFT, 2, 0);
    lv_obj_add_event_cb(ui.btn_th_ota, on_theme_ota, LV_EVENT_CLICKED, NULL);

    ui.lbl_th_ota = lv_label_create(ui.btn_th_ota);
    lv_label_set_text(ui.lbl_th_ota, "Wi-Fi OTA");
    lv_obj_set_style_text_font(ui.lbl_th_ota, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_th_ota, TH_CYAN, 0);
    lv_obj_center(ui.lbl_th_ota);

    ui.btn_th_cancel = lv_btn_create(ui.th_dlg);
    lv_obj_set_size(ui.btn_th_cancel, 126, 48);
    lv_obj_add_style(ui.btn_th_cancel, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_th_cancel, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_th_cancel, 14, 0);
    lv_obj_set_style_shadow_width(ui.btn_th_cancel, 0, 0);
    lv_obj_align(ui.btn_th_cancel, LV_ALIGN_BOTTOM_MID, -8, 0);
    lv_obj_add_event_cb(ui.btn_th_cancel, on_theme_cancel, LV_EVENT_CLICKED, NULL);

    ui.lbl_th_cancel = lv_label_create(ui.btn_th_cancel);
    lv_label_set_text(ui.lbl_th_cancel, "ANULUJ");
    lv_obj_set_style_text_font(ui.lbl_th_cancel, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_th_cancel, TH_TEXT, 0);
    lv_obj_center(ui.lbl_th_cancel);

    lv_obj_t *btn_th_save = lv_btn_create(ui.th_dlg);
    lv_obj_set_size(btn_th_save, 152, 48);
    lv_obj_add_style(btn_th_save, &st_btn_hero_cobalt, 0);
    lv_obj_add_style(btn_th_save, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_th_save, 14, 0);
    lv_obj_align(btn_th_save, LV_ALIGN_BOTTOM_RIGHT, -2, 0);
    lv_obj_add_event_cb(btn_th_save, on_theme_save, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_th_save = lv_label_create(btn_th_save);
    lv_label_set_text(lbl_th_save, "ZAPISZ");
    lv_obj_set_style_text_font(lbl_th_save, &pl_font_22, 0);
    lv_obj_set_style_text_color(lbl_th_save, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_th_save);

    ui_refresh_theme_preview();

    // nakladka opoznionego startu
    ui.overlay_delay = ui_make_overlay_backdrop(scr);

    ui.dl_dlg = lv_obj_create(ui.overlay_delay);
    lv_obj_set_size(ui.dl_dlg, 434, 244);
    lv_obj_add_style(ui.dl_dlg, &st_bento_tile, 0);
    lv_obj_set_style_border_color(ui.dl_dlg, TH_CYAN, 0);
    lv_obj_set_style_border_width(ui.dl_dlg, 2, 0);
    lv_obj_clear_flag(ui.dl_dlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ui.dl_dlg);

    ui.lbl_delay_title = lv_label_create(ui.dl_dlg);
    lv_label_set_text(ui.lbl_delay_title, "Zaplanuj start");
    lv_obj_set_style_text_font(ui.lbl_delay_title, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_delay_title, TH_TEXT, 0);
    lv_obj_align(ui.lbl_delay_title, LV_ALIGN_TOP_MID, 0, 4);

    ui.lbl_delay_sub = lv_label_create(ui.dl_dlg);
    lv_label_set_text(ui.lbl_delay_sub, "Program: 30 MINUT");
    lv_obj_set_style_text_font(ui.lbl_delay_sub, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_delay_sub, TH_TEXT_SUB, 0);
    lv_obj_align(ui.lbl_delay_sub, LV_ALIGN_TOP_MID, 0, 36);

    static const char *DL_NAMES[4] = {"+30m", "+1h", "+2h", "+3h"};
    for (int i = 0; i < 4; i++) {
        ui.btn_dl_opts[i] = lv_btn_create(ui.dl_dlg);
        lv_obj_set_size(ui.btn_dl_opts[i], 94, 74);
        lv_obj_set_style_bg_color(ui.btn_dl_opts[i], lv_color_hex(p->sleep_bg), 0);
        lv_obj_set_style_border_color(ui.btn_dl_opts[i], TH_BORDER_LIGHT, 0);
        lv_obj_set_style_border_width(ui.btn_dl_opts[i], 1, 0);
        lv_obj_set_style_shadow_width(ui.btn_dl_opts[i], 0, 0);
        lv_obj_add_style(ui.btn_dl_opts[i], &st_btn_pressed, LV_STATE_PRESSED);
        lv_obj_set_style_radius(ui.btn_dl_opts[i], 14, 0);
        lv_obj_set_pos(ui.btn_dl_opts[i], i * 104, 72);
        lv_obj_add_event_cb(ui.btn_dl_opts[i], on_delay_pick, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        ui.lbl_dl_opts[i] = lv_label_create(ui.btn_dl_opts[i]);
        lv_label_set_text(ui.lbl_dl_opts[i], DL_NAMES[i]);
        lv_obj_set_style_text_font(ui.lbl_dl_opts[i], &pl_font_22, 0);
        lv_obj_set_style_text_color(ui.lbl_dl_opts[i], TH_TEXT, 0);
        lv_obj_center(ui.lbl_dl_opts[i]);
    }

    ui.btn_dl_back = lv_btn_create(ui.dl_dlg);
    lv_obj_set_size(ui.btn_dl_back, 220, 48);
    lv_obj_add_style(ui.btn_dl_back, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_dl_back, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_dl_back, 14, 0);
    lv_obj_set_style_shadow_width(ui.btn_dl_back, 0, 0);
    lv_obj_align(ui.btn_dl_back, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(ui.btn_dl_back, on_delay_back, LV_EVENT_CLICKED, NULL);

    ui.lbl_dl_back = lv_label_create(ui.btn_dl_back);
    lv_label_set_text(ui.lbl_dl_back, "Wróć");
    lv_obj_set_style_text_font(ui.lbl_dl_back, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_dl_back, TH_TEXT, 0);
    lv_obj_center(ui.lbl_dl_back);

    // nakladka serwera OTA
    ui.overlay_ota = ui_make_overlay_backdrop(scr);

    ui.ota_dlg = lv_obj_create(ui.overlay_ota);
    lv_obj_set_size(ui.ota_dlg, 436, 244);
    lv_obj_add_style(ui.ota_dlg, &st_bento_tile, 0);
    lv_obj_set_style_border_color(ui.ota_dlg, TH_CYAN, 0);
    lv_obj_set_style_border_width(ui.ota_dlg, 2, 0);
    lv_obj_clear_flag(ui.ota_dlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ui.ota_dlg);

    ui.lbl_ota_title = lv_label_create(ui.ota_dlg);
    lv_label_set_text(ui.lbl_ota_title, "Aktualizacja Wi-Fi (OTA) · v2");
    lv_obj_set_style_text_font(ui.lbl_ota_title, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_ota_title, TH_TEXT, 0);
    lv_obj_align(ui.lbl_ota_title, LV_ALIGN_TOP_MID, 0, 2);

    ui.lbl_ota_url = lv_label_create(ui.ota_dlg);
    lv_label_set_text(ui.lbl_ota_url, "http://zmywarka.local/\n192.168.18.88");
    lv_obj_set_style_text_align(ui.lbl_ota_url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ui.lbl_ota_url, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_ota_url, TH_CYAN, 0);
    lv_obj_align(ui.lbl_ota_url, LV_ALIGN_TOP_MID, 0, 36);

    ui.bar_ota_prog = lv_bar_create(ui.ota_dlg);
    lv_obj_set_size(ui.bar_ota_prog, 340, 16);
    lv_bar_set_range(ui.bar_ota_prog, 0, 100);
    lv_bar_set_value(ui.bar_ota_prog, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(ui.bar_ota_prog, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(ui.bar_ota_prog, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui.bar_ota_prog, lv_color_hex(p->sleep_bg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.bar_ota_prog, TH_CYAN, LV_PART_INDICATOR);
    lv_obj_align(ui.bar_ota_prog, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_add_flag(ui.bar_ota_prog, LV_OBJ_FLAG_HIDDEN);

    ui.lbl_ota_desc = lv_label_create(ui.ota_dlg);
    lv_label_set_text(ui.lbl_ota_desc, "Otwórz ten adres w przeglądarce\ni wyślij plik firmware.bin");
    lv_obj_set_style_text_align(ui.lbl_ota_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ui.lbl_ota_desc, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_ota_desc, TH_TEXT_SUB, 0);
    lv_obj_align(ui.lbl_ota_desc, LV_ALIGN_TOP_MID, 0, 118);

    ui.btn_ota_close = lv_btn_create(ui.ota_dlg);
    lv_obj_set_size(ui.btn_ota_close, 200, 52);
    lv_obj_add_style(ui.btn_ota_close, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_ota_close, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_ota_close, 14, 0);
    lv_obj_set_style_shadow_width(ui.btn_ota_close, 0, 0);
    lv_obj_align(ui.btn_ota_close, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_event_cb(ui.btn_ota_close, on_ota_close, LV_EVENT_CLICKED, NULL);

    ui.lbl_ota_close = lv_label_create(ui.btn_ota_close);
    lv_label_set_text(ui.lbl_ota_close, "ZAMKNIJ");
    lv_obj_set_style_text_font(ui.lbl_ota_close, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_ota_close, TH_TEXT, 0);
    lv_obj_center(ui.lbl_ota_close);

    // nakladka prognozy 7-dniowej + szczegolow dnia i godzin
    ui.overlay_weather = ui_make_overlay_backdrop(scr);
    lv_obj_add_flag(ui.overlay_weather, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ui.overlay_weather, on_weather_close, LV_EVENT_CLICKED, NULL);

    ui.w_dlg = lv_obj_create(ui.overlay_weather);
    lv_obj_set_size(ui.w_dlg, 456, 280);
    lv_obj_add_style(ui.w_dlg, &st_bento_tile, 0);
    lv_obj_set_style_pad_all(ui.w_dlg, 12, 0);
    lv_obj_set_style_border_color(ui.w_dlg, TH_CYAN, 0);
    lv_obj_set_style_border_width(ui.w_dlg, 2, 0);
    lv_obj_clear_flag(ui.w_dlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ui.w_dlg);

    ui.lbl_w_empty = lv_label_create(ui.w_dlg);
    lv_label_set_text(ui.lbl_w_empty, "Pobieranie prognozy 7-dniowej...");
    lv_obj_set_style_text_font(ui.lbl_w_empty, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_w_empty, TH_TEXT_SUB, 0);
    lv_obj_align(ui.lbl_w_empty, LV_ALIGN_CENTER, 0, 18);
    lv_obj_add_flag(ui.lbl_w_empty, LV_OBJ_FLAG_HIDDEN);

    ui.btn_w_back = lv_btn_create(ui.w_dlg);
    lv_obj_set_size(ui.btn_w_back, 82, 34);
    lv_obj_add_style(ui.btn_w_back, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_w_back, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_w_back, 10, 0);
    lv_obj_set_style_border_color(ui.btn_w_back, TH_BORDER_LIGHT, 0);
    lv_obj_set_style_border_width(ui.btn_w_back, 1, 0);
    lv_obj_set_style_pad_all(ui.btn_w_back, 0, 0);
    lv_obj_set_style_shadow_width(ui.btn_w_back, 0, 0);
    lv_obj_align(ui.btn_w_back, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(ui.btn_w_back, on_weather_back, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(ui.btn_w_back, LV_OBJ_FLAG_HIDDEN);

    ui.lbl_w_back = lv_label_create(ui.btn_w_back);
    lv_label_set_text(ui.lbl_w_back, "< 7 dni");
    lv_obj_set_style_text_font(ui.lbl_w_back, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_w_back, TH_TEXT, 0);
    lv_obj_center(ui.lbl_w_back);

    ui.lbl_w_city = lv_label_create(ui.w_dlg);
    lv_obj_set_width(ui.lbl_w_city, 332);
    lv_label_set_long_mode(ui.lbl_w_city, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui.lbl_w_city, "Pogoda · Wrocław");
    lv_obj_set_style_text_font(ui.lbl_w_city, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_w_city, TH_TEXT, 0);
    lv_obj_align(ui.lbl_w_city, LV_ALIGN_TOP_LEFT, 0, 4);

    ui.btn_w_close = lv_btn_create(ui.w_dlg);
    lv_obj_set_size(ui.btn_w_close, 88, 34);
    lv_obj_add_style(ui.btn_w_close, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.btn_w_close, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_w_close, 10, 0);
    lv_obj_set_style_border_color(ui.btn_w_close, TH_BORDER_LIGHT, 0);
    lv_obj_set_style_border_width(ui.btn_w_close, 1, 0);
    lv_obj_set_style_pad_all(ui.btn_w_close, 0, 0);
    lv_obj_set_style_shadow_width(ui.btn_w_close, 0, 0);
    lv_obj_align(ui.btn_w_close, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(ui.btn_w_close, on_weather_close, LV_EVENT_CLICKED, NULL);

    ui.lbl_w_close = lv_label_create(ui.btn_w_close);
    lv_label_set_text(ui.lbl_w_close, "Zamknij");
    lv_obj_set_style_text_font(ui.lbl_w_close, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_w_close, TH_TEXT, 0);
    lv_obj_center(ui.lbl_w_close);

    // widok 7 dni
    ui.w_week_box = lv_obj_create(ui.w_dlg);
    lv_obj_set_size(ui.w_week_box, 432, 218);
    lv_obj_set_pos(ui.w_week_box, 0, 38);
    lv_obj_clear_flag(ui.w_week_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui.w_week_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.w_week_box, 0, 0);
    lv_obj_set_style_pad_all(ui.w_week_box, 0, 0);

    ui.lbl_w_summary = lv_label_create(ui.w_week_box);
    lv_obj_set_width(ui.lbl_w_summary, 432);
    lv_label_set_long_mode(ui.lbl_w_summary, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui.lbl_w_summary, "");
    lv_obj_set_style_text_font(ui.lbl_w_summary, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_w_summary, TH_TEXT_SUB, 0);
    lv_obj_set_pos(ui.lbl_w_summary, 0, 0);

    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        lv_obj_t *col = lv_obj_create(ui.w_week_box);
        ui.w_col[i] = col;
        lv_obj_set_size(col, 58, 188);
        lv_obj_set_pos(col, i * 62, 28);
        lv_obj_add_style(col, &st_btn_pressed, LV_STATE_PRESSED);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(col, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(col, 0, 0);
        lv_obj_set_style_radius(col, 14, 0);
        lv_obj_set_style_border_width(col, 1, 0);
        lv_obj_set_style_bg_color(col, lv_color_hex(p->sleep_bg), 0);
        lv_obj_set_style_border_color(col, TH_BORDER_LIGHT, 0);
        lv_obj_add_event_cb(col, on_weather_day_tap, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        ui.lbl_w_day[i] = lv_label_create(col);
        lv_label_set_text(ui.lbl_w_day[i], "-");
        lv_obj_set_style_text_font(ui.lbl_w_day[i], &pl_font_16, 0);
        lv_obj_set_style_text_color(ui.lbl_w_day[i], TH_TEXT_SUB, 0);
        lv_obj_align(ui.lbl_w_day[i], LV_ALIGN_TOP_MID, 0, 10);

        ui.ic_w_day[i] = ui_theme_create_icon(col, &ic_w_sun, TH_AMBER);
        lv_obj_align(ui.ic_w_day[i], LV_ALIGN_CENTER, 0, -24);

        ui.lbl_w_hi[i] = lv_label_create(col);
        lv_label_set_text(ui.lbl_w_hi[i], "--");
        lv_obj_set_style_text_font(ui.lbl_w_hi[i], &pl_font_22, 0);
        lv_obj_set_style_text_color(ui.lbl_w_hi[i], TH_TEXT, 0);
        lv_obj_align(ui.lbl_w_hi[i], LV_ALIGN_BOTTOM_MID, 0, -58);

        ui.lbl_w_lo[i] = lv_label_create(col);
        lv_label_set_text(ui.lbl_w_lo[i], "--");
        lv_obj_set_style_text_font(ui.lbl_w_lo[i], &pl_font_16, 0);
        lv_obj_set_style_text_color(ui.lbl_w_lo[i], TH_TEXT_SUB, 0);
        lv_obj_align(ui.lbl_w_lo[i], LV_ALIGN_BOTTOM_MID, 0, -34);

        ui.lbl_w_pop[i] = lv_label_create(col);
        lv_label_set_text(ui.lbl_w_pop[i], "0%");
        lv_obj_set_style_text_font(ui.lbl_w_pop[i], &pl_font_16, 0);
        lv_obj_set_style_text_color(ui.lbl_w_pop[i], TH_CYAN, 0);
        lv_obj_align(ui.lbl_w_pop[i], LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    // widok szczegolow dnia + 24h pasek godzinowy
    ui.w_day_box = lv_obj_create(ui.w_dlg);
    lv_obj_set_size(ui.w_day_box, 432, 216);
    lv_obj_set_pos(ui.w_day_box, 0, 40);
    lv_obj_clear_flag(ui.w_day_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui.w_day_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.w_day_box, 0, 0);
    lv_obj_set_style_pad_all(ui.w_day_box, 0, 0);
    lv_obj_add_flag(ui.w_day_box, LV_OBJ_FLAG_HIDDEN);

    ui.w_det_card = lv_obj_create(ui.w_day_box);
    lv_obj_set_size(ui.w_det_card, 432, 66);
    lv_obj_set_pos(ui.w_det_card, 0, 0);
    lv_obj_clear_flag(ui.w_det_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(ui.w_det_card, 0, 0);
    lv_obj_set_style_radius(ui.w_det_card, 14, 0);
    lv_obj_set_style_border_width(ui.w_det_card, 1, 0);
    lv_obj_set_style_bg_color(ui.w_det_card, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_border_color(ui.w_det_card, TH_BORDER_LIGHT, 0);

    ui.ic_w_det = ui_theme_create_icon(ui.w_det_card, &ic_w_sun, TH_AMBER);
    lv_obj_align(ui.ic_w_det, LV_ALIGN_TOP_LEFT, 12, 9);

    ui.lbl_w_det_temp = lv_label_create(ui.w_det_card);
    lv_label_set_text(ui.lbl_w_det_temp, "--");
    lv_obj_set_style_text_font(ui.lbl_w_det_temp, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_w_det_temp, TH_TEXT, 0);
    lv_obj_align(ui.lbl_w_det_temp, LV_ALIGN_TOP_LEFT, 42, 7);

    ui.lbl_w_det_sun = lv_label_create(ui.w_det_card);
    lv_label_set_text(ui.lbl_w_det_sun, "");
    lv_obj_set_style_text_font(ui.lbl_w_det_sun, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_w_det_sun, TH_TEXT_SUB, 0);
    lv_obj_align(ui.lbl_w_det_sun, LV_ALIGN_TOP_RIGHT, -12, 10);

    ui.lbl_w_det_meta = lv_label_create(ui.w_det_card);
    lv_obj_set_width(ui.lbl_w_det_meta, 408);
    lv_label_set_long_mode(ui.lbl_w_det_meta, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui.lbl_w_det_meta, "");
    lv_obj_set_style_text_font(ui.lbl_w_det_meta, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_w_det_meta, TH_TEXT_SUB, 0);
    lv_obj_align(ui.lbl_w_det_meta, LV_ALIGN_BOTTOM_LEFT, 12, -10);

    ui.w_hour_row = lv_obj_create(ui.w_day_box);
    lv_obj_set_size(ui.w_hour_row, 432, 142);
    lv_obj_set_pos(ui.w_hour_row, 0, 74);
    lv_obj_set_scroll_dir(ui.w_hour_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(ui.w_hour_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui.w_hour_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.w_hour_row, 0, 0);
    lv_obj_set_style_pad_all(ui.w_hour_row, 0, 0);

    for (int h = 0; h < WEATHER_MAX_HOURS; h++) {
        lv_obj_t *hc = lv_obj_create(ui.w_hour_row);
        ui.w_hour_card[h] = hc;
        lv_obj_set_size(hc, 60, 138);
        lv_obj_set_pos(hc, h * 65, 2);
        lv_obj_clear_flag(hc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(hc, 0, 0);
        lv_obj_set_style_radius(hc, 14, 0);
        lv_obj_set_style_border_width(hc, 1, 0);
        lv_obj_set_style_bg_color(hc, lv_color_hex(p->sleep_bg), 0);
        lv_obj_set_style_border_color(hc, TH_BORDER_LIGHT, 0);

        ui.lbl_wh_time[h] = lv_label_create(hc);
        lv_label_set_text(ui.lbl_wh_time[h], "00:00");
        lv_obj_set_style_text_font(ui.lbl_wh_time[h], &pl_font_16, 0);
        lv_obj_set_style_text_color(ui.lbl_wh_time[h], TH_TEXT_SUB, 0);
        lv_obj_align(ui.lbl_wh_time[h], LV_ALIGN_TOP_MID, 0, 10);

        ui.ic_wh[h] = ui_theme_create_icon(hc, &ic_w_sun, TH_AMBER);
        lv_obj_align(ui.ic_wh[h], LV_ALIGN_CENTER, 0, -10);

        ui.lbl_wh_temp[h] = lv_label_create(hc);
        lv_label_set_text(ui.lbl_wh_temp[h], "--");
        lv_obj_set_style_text_font(ui.lbl_wh_temp[h], &pl_font_22, 0);
        lv_obj_set_style_text_color(ui.lbl_wh_temp[h], TH_TEXT, 0);
        lv_obj_align(ui.lbl_wh_temp[h], LV_ALIGN_BOTTOM_MID, 0, -34);

        ui.lbl_wh_pop[h] = lv_label_create(hc);
        lv_label_set_text(ui.lbl_wh_pop[h], "0%");
        lv_obj_set_style_text_font(ui.lbl_wh_pop[h], &pl_font_16, 0);
        lv_obj_set_style_text_color(ui.lbl_wh_pop[h], TH_CYAN, 0);
        lv_obj_align(ui.lbl_wh_pop[h], LV_ALIGN_BOTTOM_MID, 0, -10);
    }
}

void ui_overlays_apply_theme(const ui_palette_t *p)
{
    lv_color_t c_text = lv_color_hex(p->text);
    lv_color_t c_sub = lv_color_hex(p->text_sub);
    lv_color_t c_sleep = lv_color_hex(p->sleep_bg);
    lv_color_t c_accent = lv_color_hex(p->accent);

    if (ui.load_brand) lv_obj_set_style_text_color(ui.load_brand, c_accent, 0);
    if (ui.load_spinner) {
        lv_obj_set_style_arc_color(ui.load_spinner, c_sleep, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ui.load_spinner, c_accent, LV_PART_INDICATOR);
    }
    if (ui.lbl_load_title) lv_obj_set_style_text_color(ui.lbl_load_title, c_text, 0);
    if (ui.lbl_load_detail) lv_obj_set_style_text_color(ui.lbl_load_detail, c_sub, 0);

    if (ui.overlay_busy) lv_obj_set_style_bg_color(ui.overlay_busy, lv_color_hex(p->bg), 0);
    if (ui.busy_spin) {
        lv_obj_set_style_arc_color(ui.busy_spin, c_sleep, LV_PART_MAIN);
        lv_obj_set_style_arc_color(ui.busy_spin, c_accent, LV_PART_INDICATOR);
    }
    if (ui.lbl_busy_msg) lv_obj_set_style_text_color(ui.lbl_busy_msg, c_text, 0);

    if (ui.overlay_theme) lv_obj_set_style_bg_color(ui.overlay_theme, lv_color_hex(p->bg), 0);
    if (ui.th_dlg) lv_obj_set_style_border_color(ui.th_dlg, c_accent, 0);
    if (ui.lbl_theme_name) lv_obj_set_style_text_color(ui.lbl_theme_name, c_text, 0);
    if (ui.btn_th_prev) lv_obj_set_style_bg_color(ui.btn_th_prev, c_sleep, 0);
    if (ui.lbl_th_prev) lv_obj_set_style_text_color(ui.lbl_th_prev, c_text, 0);
    if (ui.btn_th_next) lv_obj_set_style_bg_color(ui.btn_th_next, c_sleep, 0);
    if (ui.lbl_th_next) lv_obj_set_style_text_color(ui.lbl_th_next, c_text, 0);
    if (ui.btn_th_ota) lv_obj_set_style_bg_color(ui.btn_th_ota, c_sleep, 0);
    if (ui.lbl_th_ota) lv_obj_set_style_text_color(ui.lbl_th_ota, c_accent, 0);
    if (ui.btn_th_cancel) lv_obj_set_style_bg_color(ui.btn_th_cancel, c_sleep, 0);
    if (ui.lbl_th_cancel) lv_obj_set_style_text_color(ui.lbl_th_cancel, c_text, 0);
    if (ui.btn_th_bri) lv_obj_set_style_bg_color(ui.btn_th_bri, c_sleep, 0);
    if (ui.lbl_th_bri) lv_obj_set_style_text_color(ui.lbl_th_bri, c_accent, 0);

    if (ui.overlay_delay) lv_obj_set_style_bg_color(ui.overlay_delay, lv_color_hex(p->bg), 0);
    if (ui.dl_dlg) lv_obj_set_style_border_color(ui.dl_dlg, c_accent, 0);
    if (ui.lbl_delay_title) lv_obj_set_style_text_color(ui.lbl_delay_title, c_text, 0);
    if (ui.lbl_delay_sub) lv_obj_set_style_text_color(ui.lbl_delay_sub, c_sub, 0);
    for (int i = 0; i < 4; i++) {
        if (ui.btn_dl_opts[i]) lv_obj_set_style_bg_color(ui.btn_dl_opts[i], c_sleep, 0);
        if (ui.lbl_dl_opts[i]) lv_obj_set_style_text_color(ui.lbl_dl_opts[i], c_text, 0);
    }
    if (ui.btn_dl_back) lv_obj_set_style_bg_color(ui.btn_dl_back, c_sleep, 0);
    if (ui.lbl_dl_back) lv_obj_set_style_text_color(ui.lbl_dl_back, c_text, 0);

    if (ui.overlay_ota) lv_obj_set_style_bg_color(ui.overlay_ota, lv_color_hex(p->bg), 0);
    if (ui.ota_dlg) lv_obj_set_style_border_color(ui.ota_dlg, c_accent, 0);
    if (ui.lbl_ota_title) lv_obj_set_style_text_color(ui.lbl_ota_title, c_text, 0);
    if (ui.lbl_ota_url) lv_obj_set_style_text_color(ui.lbl_ota_url, c_accent, 0);
    if (ui.bar_ota_prog) {
        lv_obj_set_style_bg_color(ui.bar_ota_prog, c_sleep, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui.bar_ota_prog, c_accent, LV_PART_INDICATOR);
    }
    if (ui.lbl_ota_desc) lv_obj_set_style_text_color(ui.lbl_ota_desc, c_sub, 0);
    if (ui.btn_ota_close) lv_obj_set_style_bg_color(ui.btn_ota_close, c_sleep, 0);
    if (ui.lbl_ota_close) lv_obj_set_style_text_color(ui.lbl_ota_close, c_text, 0);

    if (ui.overlay_weather) lv_obj_set_style_bg_color(ui.overlay_weather, lv_color_hex(p->bg), 0);
    if (ui.w_dlg) lv_obj_set_style_border_color(ui.w_dlg, c_accent, 0);
    if (ui.lbl_w_city) lv_obj_set_style_text_color(ui.lbl_w_city, c_text, 0);
    if (ui.btn_w_close) {
        lv_obj_set_style_bg_color(ui.btn_w_close, c_sleep, 0);
        lv_obj_set_style_border_color(ui.btn_w_close, lv_color_hex(p->border_light), 0);
    }
    if (ui.lbl_w_close) lv_obj_set_style_text_color(ui.lbl_w_close, c_text, 0);
    if (ui.btn_w_back) {
        lv_obj_set_style_bg_color(ui.btn_w_back, c_sleep, 0);
        lv_obj_set_style_border_color(ui.btn_w_back, lv_color_hex(p->border_light), 0);
    }
    if (ui.lbl_w_back) lv_obj_set_style_text_color(ui.lbl_w_back, c_text, 0);
    ui_refresh_weather_forecast();

    ui_refresh_theme_preview();
}

void ui_set_busy(bool busy, const char *msg)
{
    if (!ui.overlay_busy) return;
    if (busy) {
        if (msg) lv_label_set_text(ui.lbl_busy_msg, msg);
        lv_obj_clear_flag(ui.overlay_busy, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.overlay_busy, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_set_ota_url(const char *ip_str)
{
    if (ui.lbl_ota_url && ip_str && ip_str[0]) {
        lv_label_set_text_fmt(ui.lbl_ota_url, "http://zmywarka.local/\n%s", ip_str);
    }
}

void ui_set_ota_progress(int pct, bool finished, bool ok)
{
    if (!ui.overlay_ota || !ui.bar_ota_prog || !ui.lbl_ota_desc) return;
    if (ui.scr_main && lv_scr_act() != ui.scr_main) {
        lv_scr_load(ui.scr_main);
    }
    lv_obj_clear_flag(ui.overlay_ota, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui.bar_ota_prog, LV_OBJ_FLAG_HIDDEN);

    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(ui.bar_ota_prog, pct, LV_ANIM_OFF);

    if (!finished) {
        if (ui.btn_ota_close) lv_obj_add_state(ui.btn_ota_close, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(ui.bar_ota_prog, TH_CYAN, LV_PART_INDICATOR);
        lv_label_set_text_fmt(ui.lbl_ota_desc, "Wgrywanie OTA: %d%%\nNie odłączaj zasilania...", pct);
    } else if (ok) {
        if (ui.btn_ota_close) lv_obj_add_state(ui.btn_ota_close, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(ui.bar_ota_prog, TH_MINT, LV_PART_INDICATOR);
        lv_bar_set_value(ui.bar_ota_prog, 100, LV_ANIM_OFF);
        lv_label_set_text(ui.lbl_ota_desc, "Gotowe (100%)!\nRestartowanie ESP32...");
    } else {
        if (ui.btn_ota_close) lv_obj_clear_state(ui.btn_ota_close, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(ui.bar_ota_prog, TH_RED, LV_PART_INDICATOR);
        lv_label_set_text(ui.lbl_ota_desc, "Błąd zapisu OTA!\nSpróbuj ponownie.");
    }
}
