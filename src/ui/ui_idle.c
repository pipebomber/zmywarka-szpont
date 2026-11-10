#include <string.h>
#include "esp_timer.h"
#include "ui_internal.h"

static bool s_prog_swiped = false;
static bool s_prog_long = false;
static lv_point_t s_prog_press_pt = {0};

static bool s_start_hold_fired = false;
static bool s_start_left_bounds = false;
static bool s_swiped_down = false;
static lv_point_t s_start_press_pt = {0};
static lv_timer_t *s_start_hint_timer = NULL;

const char *ui_get_selected_prog_name(void)
{
    switch (ui.selected_prog) {
        case MIDEA_PROG_1H:        return ui.extra_dry ? "1 GODZINA + SUSZ." : "1 GODZINA";
        case MIDEA_PROG_ECO:       return "ECO 50°C";
        case MIDEA_PROG_INTENSIVE: return "INTENSYWNY";
        case MIDEA_PROG_30MIN:
        default:                   return "30 MINUT";
    }
}

static void style_prog_card(const ui_palette_t *p, bool selected, lv_obj_t *card,
                            lv_obj_t *icon_box, lv_obj_t *icon_img, lv_obj_t *dot_sel, lv_obj_t *lbl_title)
{
    lv_obj_remove_style(card, &st_bento_selected, 0);
    if (selected) {
        lv_obj_add_style(card, &st_bento_selected, 0);
        lv_obj_set_style_bg_color(icon_box, lv_color_hex(p->sel_icon_bg), 0);
        lv_obj_set_style_img_recolor(icon_img, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_color(dot_sel, lv_color_hex(p->sel_dot), 0);
        lv_obj_set_style_border_color(dot_sel, p->is_light ? lv_color_hex(p->sel_border) : lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(dot_sel, 2, 0);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(p->sel_title), 0);
    } else {
        lv_obj_set_style_bg_color(icon_box, lv_color_hex(p->unsel_icon_bg), 0);
        lv_obj_set_style_img_recolor(icon_img, lv_color_hex(p->muted), 0);
        lv_obj_set_style_bg_color(dot_sel, lv_color_hex(p->tile), 0);
        lv_obj_set_style_border_color(dot_sel, lv_color_hex(p->border_light), 0);
        lv_obj_set_style_border_width(dot_sel, 2, 0);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(p->unsel_title), 0);
    }
}

void ui_idle_update_visuals(void)
{
    if (!ui.btn_prog_1h || !ui.btn_prog_30 || !ui.btn_big_start) return;

    const ui_palette_t *p = ui_theme_get();
    midea_program_t top_prog = ui.slot_top_eco ? MIDEA_PROG_ECO : MIDEA_PROG_1H;
    midea_program_t bot_prog = ui.slot_bot_int ? MIDEA_PROG_INTENSIVE : MIDEA_PROG_30MIN;

    style_prog_card(p, (ui.selected_prog == top_prog), ui.btn_prog_1h, ui.icon_1h_box, ui.icon_1h_img, ui.dot_1h_sel, ui.lbl_1h_title);
    lv_label_set_text(ui.lbl_1h_title, ui.slot_top_eco ? "ECO 50°C" : (ui.extra_dry ? "1H + SUSZ." : "1 GODZINA"));

    style_prog_card(p, (ui.selected_prog == bot_prog), ui.btn_prog_30, ui.icon_30_box, ui.icon_30_img, ui.dot_30_sel, ui.lbl_30_title);
    lv_label_set_text(ui.lbl_30_title, ui.slot_bot_int ? "INTENSYWNY" : "30 MINUT");

    lv_obj_remove_style(ui.btn_big_start, &st_btn_hero_cobalt, 0);
    lv_obj_remove_style(ui.btn_big_start, &st_btn_hero_amber, 0);
    if (!ui.door_closed) {
        lv_obj_add_style(ui.btn_big_start, &st_btn_hero_amber, 0);
        lv_img_set_src(ui.icon_start_img, &ic_warn);
        lv_obj_align(ui.icon_start_img, LV_ALIGN_CENTER, 0, -54);
        lv_label_set_text(ui.lbl_start_main, "ZAMKNIJ");
        lv_obj_align(ui.lbl_start_main, LV_ALIGN_CENTER, 0, -8);
        lv_obj_clear_flag(ui.lbl_start_word, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui.lbl_start_word, "DRZWICZKI");
        lv_label_set_text(ui.lbl_start_sub, "Drzwi są otwarte");
        lv_obj_set_style_text_color(ui.lbl_start_sub, lv_color_hex(0xFEF3C7), 0);
    } else {
        lv_obj_add_style(ui.btn_big_start, &st_btn_hero_cobalt, 0);
        lv_img_set_src(ui.icon_start_img, &ic_play);
        lv_obj_align(ui.icon_start_img, LV_ALIGN_CENTER, 0, -40);
        lv_label_set_text(ui.lbl_start_main, "START");
        lv_obj_align(ui.lbl_start_main, LV_ALIGN_CENTER, 0, 8);
        lv_obj_add_flag(ui.lbl_start_word, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui.lbl_start_sub, ui_get_selected_prog_name());
        lv_obj_set_style_text_color(ui.lbl_start_sub, lv_color_hex(p->start_sub), 0);
    }
}

void ui_pick_weather_icon(const ui_palette_t *p, uint8_t c, bool day,
                          const lv_img_dsc_t **out_ic, lv_color_t *out_col)
{
    lv_color_t c_sun  = lv_color_hex(0xF59E0B);
    lv_color_t c_cool = p->is_light ? lv_color_hex(0x0284C7) : lv_color_hex(0x38BDF8);
    lv_color_t c_sub  = lv_color_hex(p->text_sub);

    if (c >= 95) {
        *out_ic = &ic_w_storm;
        *out_col = c_sun;
    } else if ((c >= 71 && c <= 77) || c == 85 || c == 86) {
        *out_ic = &ic_w_snow;
        *out_col = p->is_light ? lv_color_hex(0x0284C7) : lv_color_hex(0xBAE6FD);
    } else if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) {
        *out_ic = &ic_w_rain;
        *out_col = c_cool;
    } else if (c == 45 || c == 48) {
        *out_ic = &ic_w_fog;
        *out_col = c_sub;
    } else if (c == 3) {
        *out_ic = &ic_w_cloud;
        *out_col = c_sub;
    } else if (c == 1 || c == 2) {
        *out_ic = day ? &ic_w_partly_day : &ic_w_partly_night;
        *out_col = day ? c_sun : c_cool;
    } else {
        *out_ic = day ? &ic_w_sun : &ic_w_moon;
        *out_col = day ? c_sun : c_cool;
    }
}

void ui_idle_refresh_weather(void)
{
    if (!ui.icon_weather || !ui.lbl_weather) return;

    if (!ui.weather.valid || ui.diag_active) {
        lv_obj_add_flag(ui.icon_weather, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_weather, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const ui_palette_t *p = ui_theme_get();
    const lv_img_dsc_t *ic = &ic_w_sun;
    lv_color_t col = lv_color_hex(0xF59E0B);
    ui_pick_weather_icon(p, ui.weather.wmo_code, ui.weather.is_day, &ic, &col);

    lv_img_set_src(ui.icon_weather, ic);
    lv_obj_set_style_img_recolor(ui.icon_weather, col, 0);
    lv_label_set_text_fmt(ui.lbl_weather, "%d°", (int)ui.weather.temp_c);
    lv_obj_set_style_text_color(ui.lbl_weather, lv_color_hex(p->text), 0);

    lv_obj_clear_flag(ui.icon_weather, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui.lbl_weather, LV_OBJ_FLAG_HIDDEN);
}

void ui_idle_refresh_supply_banner(void)
{
    if (!ui.lbl_idle_supply) return;

    bool shift_for_weather = (ui.weather.valid && !ui.diag_active);
    lv_obj_align(ui.lbl_idle_supply, LV_ALIGN_LEFT_MID, shift_for_weather ? 170 : 96, 0);

    if (ui.diag_active) {
        if (ui.wifi_reconnecting || ui.diag_rssi <= -100) {
            lv_label_set_text(ui.lbl_idle_supply, "Wi-Fi: rozłączono");
            lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_AMBER, 0);
        } else {
            const char *dot = strrchr(ui.diag_ip, '.');
            const char *tail = dot ? (dot + 1) : ui.diag_ip;
            lv_label_set_text_fmt(ui.lbl_idle_supply, "%d dBm · %lums · .%s",
                                  ui.diag_rssi, (unsigned long)ui.diag_latency_ms, tail);
            lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_CYAN, 0);
        }
        return;
    }

    if (ui.sched_rem_sec > 0) {
        int mins = (ui.sched_rem_sec + 59) / 60;
        int h = mins / 60;
        int m = mins % 60;
        if (h > 0) lv_label_set_text_fmt(ui.lbl_idle_supply, "Start za %dh %02dm", h, m);
        else lv_label_set_text_fmt(ui.lbl_idle_supply, "Start za %d min", m);
        lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_CYAN, 0);
    } else if (ui.wifi_reconnecting) {
        lv_label_set_text(ui.lbl_idle_supply, "Ponowne Wi-Fi...");
        lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_AMBER, 0);
    } else if (ui.cached_rinse_lack && ui.cached_salt_lack) {
        lv_label_set_text(ui.lbl_idle_supply, "Brak płynu i soli!");
        lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_AMBER, 0);
    } else if (ui.cached_rinse_lack) {
        lv_label_set_text(ui.lbl_idle_supply, "Dolej nabłyszczacz!");
        lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_AMBER, 0);
    } else if (ui.cached_salt_lack) {
        lv_label_set_text(ui.lbl_idle_supply, "Dosyp sól!");
        lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_AMBER, 0);
    } else if (!ui.door_closed) {
        lv_label_set_text(ui.lbl_idle_supply, "Zamknij drzwi");
        lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_AMBER, 0);
    } else {
        lv_label_set_text(ui.lbl_idle_supply, "");
    }
}

static void on_prog_slot(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        s_prog_swiped = false;
        s_prog_long = false;
        lv_indev_t *indev = lv_indev_get_act();
        if (indev) lv_indev_get_point(indev, &s_prog_press_pt);
    } else if (code == LV_EVENT_PRESSING) {
        if (s_prog_swiped || s_prog_long) return;
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t pt = {0};
        lv_indev_get_point(indev, &pt);
        int dy = (int)pt.y - (int)s_prog_press_pt.y;
        int dx = (int)pt.x - (int)s_prog_press_pt.x;
        if (dy < 0) dy = -dy;
        if (dx < 0) dx = -dx;
        if (dy >= 22 && dy > dx) {
            s_prog_swiped = true;
            if (slot == 0) {
                ui.slot_top_eco = !ui.slot_top_eco;
                ui.selected_prog = ui.slot_top_eco ? MIDEA_PROG_ECO : MIDEA_PROG_1H;
            } else {
                ui.slot_bot_int = !ui.slot_bot_int;
                ui.selected_prog = ui.slot_bot_int ? MIDEA_PROG_INTENSIVE : MIDEA_PROG_30MIN;
            }
            ui_idle_update_visuals();
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (s_prog_swiped) return;
        if (slot == 0 && !ui.slot_top_eco) {
            s_prog_long = true;
            ui.selected_prog = MIDEA_PROG_1H;
            ui.extra_dry = !ui.extra_dry;
            ui_idle_update_visuals();
        }
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        if (s_prog_swiped || s_prog_long) return;
        if (slot == 0) {
            ui.selected_prog = ui.slot_top_eco ? MIDEA_PROG_ECO : MIDEA_PROG_1H;
        } else {
            ui.selected_prog = ui.slot_bot_int ? MIDEA_PROG_INTENSIVE : MIDEA_PROG_30MIN;
        }
        ui_idle_update_visuals();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_prog_swiped = false;
        s_prog_long = false;
    }
}

static void trigger_selected_start(void)
{
    if (!ui.action_cb) return;
    switch (ui.selected_prog) {
        case MIDEA_PROG_ECO:       ui.action_cb(UI_ACTION_START_ECO); break;
        case MIDEA_PROG_INTENSIVE: ui.action_cb(UI_ACTION_START_INTENSIVE); break;
        case MIDEA_PROG_1H:        ui.action_cb(ui.extra_dry ? UI_ACTION_START_1H_DRY : UI_ACTION_START_1H); break;
        case MIDEA_PROG_30MIN:
        default:                   ui.action_cb(UI_ACTION_START_30MIN); break;
    }
}

static void on_start_hint_timeout(lv_timer_t *t)
{
    if (t) lv_timer_pause(t);
    ui_idle_update_visuals();
}

static void on_start_fill_done(lv_anim_t *a)
{
    (void)a;
    if (ui.obj_start_fill) lv_obj_set_width(ui.obj_start_fill, 0);
    if (!ui.door_closed || s_swiped_down || s_start_left_bounds ||
        !ui.btn_big_start || !lv_obj_has_state(ui.btn_big_start, LV_STATE_PRESSED)) {
        return;
    }
    s_start_hold_fired = true;
    if (s_start_hint_timer) lv_timer_pause(s_start_hint_timer);
    ui_idle_update_visuals();
    trigger_selected_start();
}

static void on_idle_gesture(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
        s_swiped_down = true;
        ui_cancel_hold_fill_anim(ui.obj_start_fill, false);
        ui_open_delay_picker();
    }
}

static void on_big_start(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_start_hold_fired = false;
        s_start_left_bounds = false;
        s_swiped_down = false;
        lv_indev_t *indev = lv_indev_get_act();
        if (indev) lv_indev_get_point(indev, &s_start_press_pt);
        if (!ui.door_closed) return;
        ui_start_hold_fill_anim(ui.obj_start_fill, 264, 130, 750, on_start_fill_done);
    } else if (code == LV_EVENT_PRESSING) {
        if (!ui.door_closed || s_start_hold_fired || s_swiped_down) return;
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t pt = {0};
        lv_indev_get_point(indev, &pt);
        int dy = (int)pt.y - (int)s_start_press_pt.y; // dodatnie przy przesunieciu w dol
        int dx = (int)pt.x - (int)s_start_press_pt.x;
        if (dx < 0) dx = -dx;
        if (dy >= 36 && dy > dx) {
            s_swiped_down = true;
            ui_cancel_hold_fill_anim(ui.obj_start_fill, false);
            ui_open_delay_picker();
            return;
        }
        if (!s_start_left_bounds && !ui_point_in_obj(ui.btn_big_start, &pt)) {
            s_start_left_bounds = true;
            ui_cancel_hold_fill_anim(ui.obj_start_fill, true);
        }
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        if (!ui.door_closed || s_start_hold_fired || s_swiped_down || s_start_left_bounds) return;
        if (ui.lbl_start_sub) lv_label_set_text(ui.lbl_start_sub, "W dół: Plan · Trzymaj: Start");
        if (!s_start_hint_timer) {
            s_start_hint_timer = lv_timer_create(on_start_hint_timeout, 1400, NULL);
        } else {
            lv_timer_reset(s_start_hint_timer);
            lv_timer_resume(s_start_hint_timer);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui_cancel_hold_fill_anim(ui.obj_start_fill, !s_start_hold_fired && !s_swiped_down && !s_start_left_bounds);
        s_start_hold_fired = false;
        s_start_left_bounds = false;
        s_swiped_down = false;
    }
}

static void on_diag_timeout(lv_timer_t *t)
{
    if (t) lv_timer_pause(t);
    ui.diag_active = false;
    ui_idle_refresh_weather();
    ui_idle_refresh_supply_banner();
}

static void on_clock_area(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_LONG_PRESSED) {
        ui_open_theme_menu();
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        ui.diag_active = !ui.diag_active;
        if (ui.diag_active) {
            if (!ui.diag_timer) {
                ui.diag_timer = lv_timer_create(on_diag_timeout, 4000, NULL);
            } else {
                lv_timer_reset(ui.diag_timer);
                lv_timer_resume(ui.diag_timer);
            }
        } else if (ui.diag_timer) {
            lv_timer_pause(ui.diag_timer);
        }
        ui_idle_refresh_weather();
        ui_idle_refresh_supply_banner();
    }
}

static void on_weather_tap(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui.weather.valid && !ui.diag_active) {
        ui_open_weather_forecast();
    }
}

static void on_retry(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui.action_cb) ui.action_cb(UI_ACTION_RETRY);
}

static void on_discover(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui.action_cb) ui.action_cb(UI_ACTION_DISCOVER_IP);
}

static void on_finish_new(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui.dismiss_finish = true;
    screen_off_set_finish_hold(false);
    ui_show_only_pane(ui.pane_idle);
}

void ui_create_idle_finish_offline_panes(lv_obj_t *scr)
{
    const ui_palette_t *p = ui_theme_get();

    // ekran gotowosci
    ui.pane_idle = ui_make_bento_container(scr, false);
    lv_obj_add_event_cb(ui.pane_idle, on_idle_gesture, LV_EVENT_GESTURE, NULL);
    ui.active_pane = ui.pane_idle;

    ui.tile_status = lv_obj_create(ui.pane_idle);
    lv_obj_set_size(ui.tile_status, 460, 56);
    lv_obj_add_style(ui.tile_status, &st_bento_tile, 0);
    lv_obj_set_style_radius(ui.tile_status, 18, 0);
    lv_obj_set_style_pad_all(ui.tile_status, 8, 0);
    lv_obj_set_style_pad_hor(ui.tile_status, 14, 0);
    lv_obj_clear_flag(ui.tile_status, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui.tile_status, 10, 10);

    ui.dot_idle_status = lv_obj_create(ui.tile_status);
    lv_obj_set_size(ui.dot_idle_status, 14, 14);
    lv_obj_set_style_radius(ui.dot_idle_status, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui.dot_idle_status, 0, 0);
    lv_obj_set_style_bg_color(ui.dot_idle_status, TH_MINT, 0);
    lv_obj_align(ui.dot_idle_status, LV_ALIGN_LEFT_MID, 0, 0);

    ui.lbl_idle_clock = lv_label_create(ui.tile_status);
    lv_label_set_text(ui.lbl_idle_clock, "--:--");
    lv_obj_set_style_text_font(ui.lbl_idle_clock, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_idle_clock, TH_TEXT, 0);
    lv_obj_align(ui.lbl_idle_clock, LV_ALIGN_LEFT_MID, 22, 0);

    // klik w zegar: diagnostyka na 4s; przytrzymanie: motywy i OTA
    lv_obj_t *btn_clock_hold = lv_obj_create(ui.tile_status);
    lv_obj_set_size(btn_clock_hold, 98, 44);
    lv_obj_align(btn_clock_hold, LV_ALIGN_LEFT_MID, -6, 0);
    lv_obj_clear_flag(btn_clock_hold, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn_clock_hold, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(btn_clock_hold, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_clock_hold, 0, 0);
    lv_obj_add_event_cb(btn_clock_hold, on_clock_area, LV_EVENT_ALL, NULL);

    ui.icon_weather = ui_theme_create_icon(ui.tile_status, &ic_w_sun, TH_AMBER);
    lv_obj_align(ui.icon_weather, LV_ALIGN_LEFT_MID, 92, 0);
    lv_obj_add_flag(ui.icon_weather, LV_OBJ_FLAG_HIDDEN);

    ui.lbl_weather = lv_label_create(ui.tile_status);
    lv_label_set_text(ui.lbl_weather, "");
    lv_obj_set_style_text_font(ui.lbl_weather, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_weather, TH_TEXT, 0);
    lv_obj_align(ui.lbl_weather, LV_ALIGN_LEFT_MID, 118, 0);
    lv_obj_add_flag(ui.lbl_weather, LV_OBJ_FLAG_HIDDEN);

    // klik w ikone/temperature pogody -> 7-dniowa prognoza
    lv_obj_t *btn_weather_area = lv_obj_create(ui.tile_status);
    lv_obj_set_size(btn_weather_area, 76, 44);
    lv_obj_align(btn_weather_area, LV_ALIGN_LEFT_MID, 90, 0);
    lv_obj_clear_flag(btn_weather_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn_weather_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(btn_weather_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_weather_area, 0, 0);
    lv_obj_add_event_cb(btn_weather_area, on_weather_tap, LV_EVENT_CLICKED, NULL);

    ui.lbl_idle_supply = lv_label_create(ui.tile_status);
    lv_label_set_text(ui.lbl_idle_supply, "");
    lv_obj_set_style_text_font(ui.lbl_idle_supply, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_AMBER, 0);
    lv_obj_align(ui.lbl_idle_supply, LV_ALIGN_LEFT_MID, 96, 0);

    ui.tile_sleep = lv_btn_create(ui.tile_status);
    lv_obj_set_size(ui.tile_sleep, 96, 38);
    lv_obj_add_style(ui.tile_sleep, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.tile_sleep, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.tile_sleep, 12, 0);
    lv_obj_set_style_border_color(ui.tile_sleep, TH_BORDER_LIGHT, 0);
    lv_obj_set_style_border_width(ui.tile_sleep, 1, 0);
    lv_obj_set_style_pad_all(ui.tile_sleep, 0, 0);
    lv_obj_set_style_shadow_width(ui.tile_sleep, 0, 0);
    lv_obj_align(ui.tile_sleep, LV_ALIGN_RIGHT_MID, 2, 0);
    lv_obj_set_ext_click_area(ui.tile_sleep, 8);
    lv_obj_add_event_cb(ui.tile_sleep, ui_on_sleep_now, LV_EVENT_CLICKED, NULL);

    ui.m_ic = ui_theme_create_icon(ui.tile_sleep, &ic_moon, TH_TEXT_SUB);
    lv_obj_align(ui.m_ic, LV_ALIGN_LEFT_MID, 12, 0);

    ui.lbl_sleep = lv_label_create(ui.tile_sleep);
    lv_label_set_text(ui.lbl_sleep, "Uśpij");
    lv_obj_set_style_text_font(ui.lbl_sleep, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_sleep, TH_TEXT, 0);
    lv_obj_align(ui.lbl_sleep, LV_ALIGN_LEFT_MID, 38, 0);

    ui.btn_big_start = lv_btn_create(ui.pane_idle);
    lv_obj_set_size(ui.btn_big_start, 264, 234);
    lv_obj_add_style(ui.btn_big_start, &st_btn_hero_cobalt, 0);
    lv_obj_add_style(ui.btn_big_start, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(ui.btn_big_start, 0, 0);
    lv_obj_set_style_clip_corner(ui.btn_big_start, true, 0);
    lv_obj_set_pos(ui.btn_big_start, 10, 76);
    lv_obj_add_event_cb(ui.btn_big_start, on_big_start, LV_EVENT_ALL, NULL);

    ui.obj_start_fill = lv_obj_create(ui.btn_big_start);
    lv_obj_set_pos(ui.obj_start_fill, 0, 0);
    lv_obj_set_size(ui.obj_start_fill, 0, 234);
    lv_obj_clear_flag(ui.obj_start_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ui.obj_start_fill, 0, 0);
    lv_obj_set_style_border_width(ui.obj_start_fill, 0, 0);
    lv_obj_set_style_pad_all(ui.obj_start_fill, 0, 0);
    lv_obj_set_style_bg_color(ui.obj_start_fill, lv_color_hex(p->mint), 0);
    lv_obj_set_style_bg_opa(ui.obj_start_fill, LV_OPA_60, 0);

    ui.icon_start_img = ui_theme_create_icon(ui.btn_big_start, &ic_play, lv_color_hex(0xFFFFFF));
    lv_obj_align(ui.icon_start_img, LV_ALIGN_CENTER, 0, -54);

    ui.lbl_start_main = lv_label_create(ui.btn_big_start);
    lv_obj_set_style_text_font(ui.lbl_start_main, &pl_font_30, 0);
    lv_obj_set_style_text_color(ui.lbl_start_main, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui.lbl_start_main, LV_ALIGN_CENTER, 0, -8);

    ui.lbl_start_word = lv_label_create(ui.btn_big_start);
    lv_obj_set_style_text_font(ui.lbl_start_word, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_start_word, lv_color_hex(0xF8FAFC), 0);
    lv_obj_align(ui.lbl_start_word, LV_ALIGN_CENTER, 0, 26);

    ui.lbl_start_sub = lv_label_create(ui.btn_big_start);
    lv_obj_set_style_text_font(ui.lbl_start_sub, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_start_sub, lv_color_hex(p->start_sub), 0);
    lv_obj_align(ui.lbl_start_sub, LV_ALIGN_BOTTOM_MID, 0, -22);

    ui.btn_prog_1h = lv_obj_create(ui.pane_idle);
    lv_obj_set_size(ui.btn_prog_1h, 186, 112);
    lv_obj_add_style(ui.btn_prog_1h, &st_bento_tile, 0);
    lv_obj_add_style(ui.btn_prog_1h, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_clear_flag(ui.btn_prog_1h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(ui.btn_prog_1h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ui.btn_prog_1h, 284, 76);
    lv_obj_add_event_cb(ui.btn_prog_1h, on_prog_slot, LV_EVENT_ALL, (void *)(intptr_t)0);

    ui.icon_1h_box = lv_obj_create(ui.btn_prog_1h);
    lv_obj_set_size(ui.icon_1h_box, 38, 38);
    lv_obj_clear_flag(ui.icon_1h_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ui.icon_1h_box, 12, 0);
    lv_obj_set_style_border_width(ui.icon_1h_box, 0, 0);
    lv_obj_align(ui.icon_1h_box, LV_ALIGN_TOP_LEFT, 0, 0);
    ui.icon_1h_img = ui_theme_create_icon(ui.icon_1h_box, &ic_clock, TH_MUTED);
    lv_obj_center(ui.icon_1h_img);

    ui.dot_1h_sel = lv_obj_create(ui.btn_prog_1h);
    lv_obj_set_size(ui.dot_1h_sel, 16, 16);
    lv_obj_set_style_radius(ui.dot_1h_sel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui.dot_1h_sel, 2, 0);
    lv_obj_set_style_pad_all(ui.dot_1h_sel, 0, 0);
    lv_obj_clear_flag(ui.dot_1h_sel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui.dot_1h_sel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(ui.dot_1h_sel, LV_ALIGN_TOP_RIGHT, 0, 2);

    ui.lbl_1h_title = lv_label_create(ui.btn_prog_1h);
    lv_label_set_text(ui.lbl_1h_title, "1 GODZINA");
    lv_obj_set_style_text_font(ui.lbl_1h_title, &pl_font_22, 0);
    lv_obj_align(ui.lbl_1h_title, LV_ALIGN_BOTTOM_LEFT, 0, -6);

    ui.btn_prog_30 = lv_obj_create(ui.pane_idle);
    lv_obj_set_size(ui.btn_prog_30, 186, 112);
    lv_obj_add_style(ui.btn_prog_30, &st_bento_tile, 0);
    lv_obj_add_style(ui.btn_prog_30, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_clear_flag(ui.btn_prog_30, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(ui.btn_prog_30, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ui.btn_prog_30, 284, 198);
    lv_obj_add_event_cb(ui.btn_prog_30, on_prog_slot, LV_EVENT_ALL, (void *)(intptr_t)1);

    ui.icon_30_box = lv_obj_create(ui.btn_prog_30);
    lv_obj_set_size(ui.icon_30_box, 38, 38);
    lv_obj_clear_flag(ui.icon_30_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ui.icon_30_box, 12, 0);
    lv_obj_set_style_border_width(ui.icon_30_box, 0, 0);
    lv_obj_align(ui.icon_30_box, LV_ALIGN_TOP_LEFT, 0, 0);
    ui.icon_30_img = ui_theme_create_icon(ui.icon_30_box, &ic_bolt, TH_CYAN);
    lv_obj_center(ui.icon_30_img);

    ui.dot_30_sel = lv_obj_create(ui.btn_prog_30);
    lv_obj_set_size(ui.dot_30_sel, 16, 16);
    lv_obj_set_style_radius(ui.dot_30_sel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui.dot_30_sel, 2, 0);
    lv_obj_set_style_pad_all(ui.dot_30_sel, 0, 0);
    lv_obj_clear_flag(ui.dot_30_sel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui.dot_30_sel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(ui.dot_30_sel, LV_ALIGN_TOP_RIGHT, 0, 2);

    ui.lbl_30_title = lv_label_create(ui.btn_prog_30);
    lv_label_set_text(ui.lbl_30_title, "30 MINUT");
    lv_obj_set_style_text_font(ui.lbl_30_title, &pl_font_22, 0);
    lv_obj_align(ui.lbl_30_title, LV_ALIGN_BOTTOM_LEFT, 0, -6);

    ui_idle_update_visuals();

    // ekran konca cyklu
    ui.pane_finish = ui_make_bento_container(scr, true);

    ui.fin_card = lv_obj_create(ui.pane_finish);
    lv_obj_set_size(ui.fin_card, 460, 216);
    lv_obj_add_style(ui.fin_card, &st_bento_tile, 0);
    lv_obj_add_style(ui.fin_card, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui.fin_card, TH_MINT_DARK, 0);
    lv_obj_set_style_border_color(ui.fin_card, TH_MINT, 0);
    lv_obj_set_style_border_width(ui.fin_card, 2, 0);
    lv_obj_clear_flag(ui.fin_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ui.fin_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ui.fin_card, 10, 10);
    lv_obj_add_event_cb(ui.fin_card, on_finish_new, LV_EVENT_CLICKED, NULL);

    ui.chk_icon = ui_theme_create_icon(ui.fin_card, &ic_check, TH_MINT);
    lv_obj_align(ui.chk_icon, LV_ALIGN_TOP_MID, 0, 14);

    ui.lbl_fin_t = lv_label_create(ui.fin_card);
    lv_label_set_text(ui.lbl_fin_t, "ZMYWANIE ZAKOŃCZONE!");
    lv_obj_set_style_text_font(ui.lbl_fin_t, &pl_font_30, 0);
    lv_obj_set_style_text_color(ui.lbl_fin_t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui.lbl_fin_t, LV_ALIGN_CENTER, 0, 4);

    ui.lbl_fin_s = lv_label_create(ui.fin_card);
    lv_label_set_text(ui.lbl_fin_s, "Naczynia są czyste. Uchyl drzwiczki.");
    lv_obj_set_style_text_font(ui.lbl_fin_s, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_fin_s, lv_color_hex(0xA7F3D0), 0);
    lv_obj_align(ui.lbl_fin_s, LV_ALIGN_CENTER, 0, 46);

    ui.lbl_finish_warn = lv_label_create(ui.fin_card);
    lv_label_set_text(ui.lbl_finish_warn, "");
    lv_obj_set_style_text_font(ui.lbl_finish_warn, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_finish_warn, TH_AMBER, 0);
    lv_obj_align(ui.lbl_finish_warn, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_flag(ui.lbl_finish_warn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn_new = lv_btn_create(ui.pane_finish);
    lv_obj_set_size(btn_new, 286, 74);
    lv_obj_add_style(btn_new, &st_btn_hero_cobalt, 0);
    lv_obj_add_style(btn_new, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_pos(btn_new, 10, 236);
    lv_obj_add_event_cb(btn_new, on_finish_new, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_new = lv_label_create(btn_new);
    lv_label_set_text(lbl_new, "NOWE ZMYWANIE");
    lv_obj_set_style_text_font(lbl_new, &pl_font_22, 0);
    lv_obj_set_style_text_color(lbl_new, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_new);

    ui.btn_fin_sleep = lv_btn_create(ui.pane_finish);
    lv_obj_set_size(ui.btn_fin_sleep, 164, 74);
    lv_obj_add_style(ui.btn_fin_sleep, &st_bento_tile, 0);
    lv_obj_add_style(ui.btn_fin_sleep, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_pos(ui.btn_fin_sleep, 306, 236);
    lv_obj_add_event_cb(ui.btn_fin_sleep, ui_on_sleep_now, LV_EVENT_CLICKED, NULL);

    ui.fin_moon_ic = ui_theme_create_icon(ui.btn_fin_sleep, &ic_moon, TH_TEXT_SUB);
    lv_obj_align(ui.fin_moon_ic, LV_ALIGN_LEFT_MID, 12, 0);

    ui.lbl_fs = lv_label_create(ui.btn_fin_sleep);
    lv_label_set_text(ui.lbl_fs, "Uśpij ekran");
    lv_obj_set_style_text_font(ui.lbl_fs, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_fs, TH_TEXT, 0);
    lv_obj_align(ui.lbl_fs, LV_ALIGN_LEFT_MID, 40, 0);

    // ekran bledu / braku zasiegu
    ui.pane_offline = ui_make_bento_container(scr, true);

    ui.err_card = lv_obj_create(ui.pane_offline);
    lv_obj_set_size(ui.err_card, 460, 216);
    lv_obj_add_style(ui.err_card, &st_bento_tile, 0);
    lv_obj_set_style_bg_color(ui.err_card, TH_RED_DARK, 0);
    lv_obj_set_style_border_color(ui.err_card, TH_RED, 0);
    lv_obj_set_style_border_width(ui.err_card, 2, 0);
    lv_obj_clear_flag(ui.err_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui.err_card, 10, 10);

    ui.err_ic = ui_theme_create_icon(ui.err_card, &ic_warn, TH_RED);
    lv_obj_align(ui.err_ic, LV_ALIGN_TOP_MID, 0, 14);

    ui.lbl_err_title = lv_label_create(ui.err_card);
    lv_label_set_text(ui.lbl_err_title, "BRAK POŁĄCZENIA");
    lv_obj_set_style_text_font(ui.lbl_err_title, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_err_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui.lbl_err_title, LV_ALIGN_CENTER, 0, -4);

    ui.lbl_err_desc = lv_label_create(ui.err_card);
    lv_obj_set_width(ui.lbl_err_desc, 420);
    lv_label_set_long_mode(ui.lbl_err_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(ui.lbl_err_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ui.lbl_err_desc, "Sprawdź, czy zmywarka ma zasilanie.");
    lv_obj_set_style_text_font(ui.lbl_err_desc, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_err_desc, lv_color_hex(0xFECACA), 0);
    lv_obj_align(ui.lbl_err_desc, LV_ALIGN_CENTER, 0, 42);

    lv_obj_t *btn_retry = lv_btn_create(ui.pane_offline);
    lv_obj_set_size(btn_retry, 225, 74);
    lv_obj_add_style(btn_retry, &st_btn_hero_cobalt, 0);
    lv_obj_add_style(btn_retry, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_pos(btn_retry, 10, 236);
    lv_obj_add_event_cb(btn_retry, on_retry, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_retry = lv_label_create(btn_retry);
    lv_label_set_text(lbl_retry, "ODŚWIEŻ");
    lv_obj_set_style_text_font(lbl_retry, &pl_font_22, 0);
    lv_obj_set_style_text_color(lbl_retry, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_retry);

    ui.btn_discover = lv_btn_create(ui.pane_offline);
    lv_obj_set_size(ui.btn_discover, 225, 74);
    lv_obj_add_style(ui.btn_discover, &st_btn_hero_amber, 0);
    lv_obj_add_style(ui.btn_discover, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_pos(ui.btn_discover, 245, 236);
    lv_obj_add_event_cb(ui.btn_discover, on_discover, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_disc = lv_label_create(ui.btn_discover);
    lv_label_set_text(lbl_disc, "SZUKAJ W SIECI");
    lv_obj_set_style_text_font(lbl_disc, &pl_font_22, 0);
    lv_obj_set_style_text_color(lbl_disc, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_disc);
}

void ui_idle_apply_theme(const ui_palette_t *p)
{
    lv_color_t c_text = lv_color_hex(p->text);
    lv_color_t c_sub = lv_color_hex(p->text_sub);
    lv_color_t c_sleep = lv_color_hex(p->sleep_bg);

    if (ui.lbl_idle_clock) lv_obj_set_style_text_color(ui.lbl_idle_clock, c_text, 0);
    if (ui.tile_sleep) {
        lv_obj_set_style_bg_color(ui.tile_sleep, c_sleep, 0);
        lv_obj_set_style_border_color(ui.tile_sleep, lv_color_hex(p->border_light), 0);
    }
    if (ui.m_ic) lv_obj_set_style_img_recolor(ui.m_ic, c_sub, 0);
    if (ui.lbl_sleep) lv_obj_set_style_text_color(ui.lbl_sleep, c_text, 0);
    if (ui.obj_start_fill) lv_obj_set_style_bg_color(ui.obj_start_fill, lv_color_hex(p->mint), 0);
    ui_idle_refresh_weather();
    ui_idle_update_visuals();

    if (ui.fin_card) {
        lv_obj_set_style_bg_color(ui.fin_card, lv_color_hex(p->mint_dark), 0);
        lv_obj_set_style_border_color(ui.fin_card, lv_color_hex(p->mint), 0);
    }
    if (ui.chk_icon) lv_obj_set_style_img_recolor(ui.chk_icon, lv_color_hex(p->mint), 0);
    if (ui.btn_fin_sleep) lv_obj_set_style_border_color(ui.btn_fin_sleep, lv_color_hex(p->border_light), 0);
    if (ui.fin_moon_ic) lv_obj_set_style_img_recolor(ui.fin_moon_ic, c_sub, 0);
    if (ui.lbl_fs) lv_obj_set_style_text_color(ui.lbl_fs, c_text, 0);

    if (ui.err_card) {
        lv_obj_set_style_bg_color(ui.err_card, lv_color_hex(p->red_dark), 0);
        lv_obj_set_style_border_color(ui.err_card, lv_color_hex(p->red), 0);
    }
    if (ui.err_ic) lv_obj_set_style_img_recolor(ui.err_ic, lv_color_hex(p->red), 0);
}
