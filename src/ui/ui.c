/* Szpont Zmywarka - glowny koordynator UI (480x320) */

#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "ui_internal.h"

static const char *TAG = "UI";

ui_ctx_t ui = {
    .selected_prog = MIDEA_PROG_1H,
    .door_closed = true,
    .last_pct = -1,
    .last_left_min = -1,
    .last_clock_key = -1,
    .last_door_closed = true,
    .w_sel_day = -1,
    .finish_hour = -1,
    .finish_min = -1,
    .diag_rssi = -100,
    .diag_ip = DISHWASHER_IP,
};

bool ui_is_pane_visible(lv_obj_t *pane)
{
    return ui.scr_main && pane && (lv_scr_act() == ui.scr_main) && !lv_obj_has_flag(pane, LV_OBJ_FLAG_HIDDEN);
}

static void anim_idle_dot_opa_cb(void *var, int32_t v)
{
    if (!ui_is_pane_visible(ui.pane_idle)) return;
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_work_dot_opa_cb(void *var, int32_t v)
{
    if (!ui_is_pane_visible(ui.pane_work)) return;
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_start_breath_cb(void *var, int32_t v)
{
    (void)var;
    if (ui.wifi_reconnecting || !ui_is_pane_visible(ui.pane_idle) || !ui.btn_big_start || !ui.icon_start_img) return;
    lv_obj_set_style_border_opa(ui.btn_big_start, (lv_opa_t)v, 0);
    int32_t icon_opa = 180 + ((v - 110) * 75) / 145;
    if (icon_opa > 255) icon_opa = 255;
    lv_obj_set_style_img_opa(ui.icon_start_img, (lv_opa_t)icon_opa, 0);
}

static void anim_fin_breath_cb(void *var, int32_t v)
{
    if (!ui_is_pane_visible(ui.pane_finish)) return;
    lv_obj_set_style_border_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void anim_arc_rot_cb(void *var, int32_t v)
{
    if (!ui_is_pane_visible(ui.pane_work)) return;
    lv_arc_set_rotation((lv_obj_t *)var, (uint16_t)(v % 360));
}

static void anim_pane_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void start_loop_anim(void *obj, lv_anim_exec_xcb_t cb, int32_t v0, int32_t v1,
                            uint32_t dur_ms, bool playback)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, v0, v1);
    lv_anim_set_time(&a, dur_ms);
    lv_anim_set_path_cb(&a, playback ? lv_anim_path_ease_in_out : lv_anim_path_linear);
    if (playback) lv_anim_set_playback_time(&a, dur_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void anim_fill_w_cb(void *var, int32_t v)
{
    lv_obj_set_width((lv_obj_t *)var, (lv_coord_t)v);
}

void ui_start_hold_fill_anim(lv_obj_t *fill_obj, int32_t target_w, uint32_t delay_ms,
                             uint32_t dur_ms, lv_anim_ready_cb_t done_cb)
{
    if (!fill_obj) return;
    lv_anim_del(fill_obj, anim_fill_w_cb);
    lv_obj_set_width(fill_obj, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, fill_obj);
    lv_anim_set_exec_cb(&a, anim_fill_w_cb);
    lv_anim_set_values(&a, 0, target_w);
    lv_anim_set_delay(&a, delay_ms);
    lv_anim_set_time(&a, dur_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    if (done_cb) lv_anim_set_ready_cb(&a, done_cb);
    lv_anim_start(&a);
}

void ui_cancel_hold_fill_anim(lv_obj_t *fill_obj, bool retract)
{
    if (!fill_obj) return;
    lv_anim_del(fill_obj, anim_fill_w_cb);
    int32_t cur_w = lv_obj_get_width(fill_obj);
    if (retract && cur_w > 0) {
        lv_anim_t r;
        lv_anim_init(&r);
        lv_anim_set_var(&r, fill_obj);
        lv_anim_set_exec_cb(&r, anim_fill_w_cb);
        lv_anim_set_values(&r, cur_w, 0);
        lv_anim_set_time(&r, 140);
        lv_anim_set_path_cb(&r, lv_anim_path_ease_out);
        lv_anim_start(&r);
    } else {
        lv_obj_set_width(fill_obj, 0);
    }
}

bool ui_point_in_obj(lv_obj_t *obj, const lv_point_t *pt)
{
    if (!obj || !pt) return false;
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return (pt->x >= area.x1 && pt->x <= area.x2 && pt->y >= area.y1 && pt->y <= area.y2);
}

void ui_on_sleep_now(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui.action_cb) ui.action_cb(UI_ACTION_SLEEP_NOW);
}

lv_obj_t *ui_make_bento_container(lv_obj_t *parent, bool hidden)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, 480, 320);
    lv_obj_align(p, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    if (hidden) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

lv_obj_t *ui_make_overlay_backdrop(lv_obj_t *parent)
{
    lv_obj_t *ov = lv_obj_create(parent);
    lv_obj_set_size(ov, 480, 320);
    lv_obj_align(ov, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ov, TH_BG, 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_90, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
    return ov;
}

void ui_apply_theme_colors(void)
{
    const ui_palette_t *p = ui_theme_get();
    if (ui.scr_loading) lv_obj_refresh_style(ui.scr_loading, LV_PART_ANY, LV_STYLE_PROP_ANY);
    if (ui.scr_main) lv_obj_refresh_style(ui.scr_main, LV_PART_ANY, LV_STYLE_PROP_ANY);

    ui_idle_apply_theme(p);
    ui_work_apply_theme(p);
    ui_overlays_apply_theme(p);
}

void ui_show_only_pane(lv_obj_t *active)
{
    bool changed = (active != ui.active_pane);
    ui.active_pane = active;
    lv_obj_t *panes[4] = {ui.pane_idle, ui.pane_work, ui.pane_finish, ui.pane_offline};
    for (int i = 0; i < 4; i++) {
        if (!panes[i]) continue;
        bool is_hidden = lv_obj_has_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
        if (panes[i] == active) {
            if (is_hidden) {
                lv_obj_clear_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
                if (changed) {
                    lv_anim_t a;
                    lv_anim_init(&a);
                    lv_anim_set_var(&a, panes[i]);
                    lv_anim_set_exec_cb(&a, anim_pane_opa_cb);
                    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
                    lv_anim_set_time(&a, 200);
                    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                    lv_anim_start(&a);
                }
            }
        } else {
            if (!is_hidden) lv_obj_add_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void create_main_screen(void)
{
    ui.scr_main = lv_obj_create(NULL);
    lv_obj_add_style(ui.scr_main, &st_screen, 0);
    lv_obj_clear_flag(ui.scr_main, LV_OBJ_FLAG_SCROLLABLE);

    ui_create_idle_finish_offline_panes(ui.scr_main);
    ui_create_work_pane(ui.scr_main);
    ui_create_overlays(ui.scr_main);

    // animacje w tle (pauzowane gdy dany ekran jest ukryty)
    start_loop_anim(ui.dot_idle_status, anim_idle_dot_opa_cb, 110, 255, 1400, true);
    start_loop_anim(ui.btn_big_start, anim_start_breath_cb, 110, 255, 2200, true);
    start_loop_anim(ui.dot_work, anim_work_dot_opa_cb, 100, 255, 1300, true);
    start_loop_anim(ui.fin_card, anim_fin_breath_cb, 120, 255, 1800, true);
    start_loop_anim(ui.arc_orbit, anim_arc_rot_cb, 270, 630, 6500, false);
}

void ui_init(ui_action_cb_t action_cb)
{
    ui.action_cb = action_cb;
    ui_theme_init();
    ui_create_loading_screen();
    create_main_screen();
    ESP_LOGI(TAG, "UI ready");
}

void ui_show_loading(const char *headline, const char *detail)
{
    if (!ui.scr_loading) return;
    if (headline) lv_label_set_text(ui.lbl_load_title, headline);
    if (detail) lv_label_set_text(ui.lbl_load_detail, detail);
    if (lv_scr_act() != ui.scr_loading) {
        lv_scr_load(ui.scr_loading);
    }
}

static void set_wifi_reconnecting_ui(bool reconnecting)
{
    ui.wifi_reconnecting = reconnecting;
    lv_obj_t *btns[4] = {ui.btn_big_start, ui.btn_prog_1h, ui.btn_prog_30, ui.btn_stop};
    for (int i = 0; i < 4; i++) {
        if (!btns[i]) continue;
        if (reconnecting) {
            lv_obj_add_state(btns[i], LV_STATE_DISABLED);
            lv_obj_set_style_opa(btns[i], LV_OPA_40, 0);
        } else {
            lv_obj_clear_state(btns[i], LV_STATE_DISABLED);
            lv_obj_set_style_opa(btns[i], LV_OPA_COVER, 0);
        }
    }

    if (reconnecting) {
        ui_cancel_hold_fill_anim(ui.obj_start_fill, false);
        ui_cancel_hold_fill_anim(ui.obj_stop_fill, false);
        if (ui.dot_idle_status) lv_obj_set_style_bg_color(ui.dot_idle_status, TH_AMBER, 0);
        if (ui.dot_work) lv_obj_set_style_bg_color(ui.dot_work, TH_AMBER, 0);
        if (ui.lbl_idle_supply && ui.sched_rem_sec <= 0) {
            lv_label_set_text(ui.lbl_idle_supply, "Ponowne Wi-Fi...");
            lv_obj_set_style_text_color(ui.lbl_idle_supply, TH_AMBER, 0);
        }
    }
}

void ui_prepare_wake_screen(void)
{
    if (ui.overlay_busy) lv_obj_add_flag(ui.overlay_busy, LV_OBJ_FLAG_HIDDEN);
    if (ui.overlay_delay) lv_obj_add_flag(ui.overlay_delay, LV_OBJ_FLAG_HIDDEN);
    if (ui.overlay_theme) lv_obj_add_flag(ui.overlay_theme, LV_OBJ_FLAG_HIDDEN);
    if (ui.overlay_ota) lv_obj_add_flag(ui.overlay_ota, LV_OBJ_FLAG_HIDDEN);
    if (ui.overlay_weather) lv_obj_add_flag(ui.overlay_weather, LV_OBJ_FLAG_HIDDEN);

    // jesli ekran konca byl juz wyswietlony przed uspieciem, po wybudzeniu nie pokazujemy go ponownie
    if (ui.active_pane == ui.pane_finish) {
        ui.dismiss_finish = true;
        screen_off_set_finish_hold(false);
        ui_show_only_pane(ui.pane_idle);
    }

    // po pierwszym polaczeniu zostajemy na glownym ekranie z zoltym statusem Wi-Fi
    if (ui.had_online_status && ui.scr_main) {
        if (lv_scr_act() != ui.scr_main) {
            lv_scr_load(ui.scr_main);
        }
        ui_update_clock();
        set_wifi_reconnecting_ui(true);
        return;
    }

    if (ui.scr_loading && lv_scr_act() != ui.scr_loading) {
        char detail[48] = "Szybkie łączenie z domową siecią";
#ifdef WIFI_SSID
        snprintf(detail, sizeof(detail), "Sieć: %s", WIFI_SSID);
#endif
        ui_show_loading("Łączenie z Wi-Fi...", detail);
    }
}

bool ui_is_on_main_screen(void)
{
    return (ui.scr_main && lv_scr_act() == ui.scr_main);
}

void ui_show_main(void)
{
    if (ui.scr_main && lv_scr_act() != ui.scr_main) {
        lv_scr_load_anim(ui.scr_main, LV_SCR_LOAD_ANIM_FADE_IN, 160, 0, false);
    }
}

void ui_dismiss_finish_view(void)
{
    ui.dismiss_finish = false;
}

bool ui_is_finish_dismissed(void)
{
    return ui.dismiss_finish;
}

void ui_get_selected_options(midea_program_t *out_prog, bool *out_extra_dry)
{
    if (out_prog) *out_prog = ui.selected_prog;
    if (out_extra_dry) *out_extra_dry = ui.extra_dry;
}

void ui_set_selected_options(midea_program_t prog, bool extra_dry)
{
    ui.selected_prog = prog;
    ui.extra_dry = (prog == MIDEA_PROG_1H) ? extra_dry : false;
    ui.slot_top_eco = (prog == MIDEA_PROG_ECO);
    ui.slot_bot_int = (prog == MIDEA_PROG_INTENSIVE);
    ui_idle_update_visuals();
}

void ui_set_schedule_countdown(int remaining_sec)
{
    ui.sched_rem_sec = remaining_sec;
    ui_idle_refresh_supply_banner();
}

void ui_set_weather(const weather_data_t *w)
{
    if (w) {
        ui.weather = *w;
    } else {
        memset(&ui.weather, 0, sizeof(ui.weather));
    }
    ui_idle_refresh_weather();
    ui_idle_refresh_supply_banner();
    ui_refresh_weather_forecast();
}

void ui_update_clock(void)
{
    time_t now = 0;
    struct tm t = {0};
    time(&now);
    localtime_r(&now, &t);
    if (t.tm_year < (2024 - 1900)) return;

    int key = t.tm_hour * 60 + t.tm_min;
    if (key != ui.last_clock_key) {
        ui.last_clock_key = key;
        if (ui.lbl_idle_clock) lv_label_set_text_fmt(ui.lbl_idle_clock, "%02d:%02d", t.tm_hour, t.tm_min);
    }
}

void ui_update_status(const midea_status_t *status, bool wifi_ok, int rssi)
{
    if (!ui.scr_main) return;
    ui.diag_rssi = rssi;
    if (status && status->host_ip[0]) {
        snprintf(ui.diag_ip, sizeof(ui.diag_ip), "%s", status->host_ip);
        ui.diag_latency_ms = status->latency_ms;
    }
    ui_update_clock();

    if (!wifi_ok) {
        if (ui.had_online_status) {
            // ciche laczenie w tle bez zaslaniania ekranu loaderem
            if (lv_scr_act() != ui.scr_main) {
                lv_scr_load(ui.scr_main);
            }
            set_wifi_reconnecting_ui(true);
            ui_idle_refresh_supply_banner();
            return;
        }
        if (lv_scr_act() != ui.scr_main) {
            lv_scr_load_anim(ui.scr_main, LV_SCR_LOAD_ANIM_FADE_IN, 160, 0, false);
        }
        lv_label_set_text(ui.lbl_err_title, "BRAK SIECI WI-FI");
        lv_label_set_text(ui.lbl_err_desc, "Sprawdzam połączenie z domowym Wi-Fi...");
        lv_obj_clear_flag(ui.btn_discover, LV_OBJ_FLAG_HIDDEN);
        ui_show_only_pane(ui.pane_offline);
        return;
    }

    if (lv_scr_act() != ui.scr_main) {
        lv_scr_load_anim(ui.scr_main, LV_SCR_LOAD_ANIM_FADE_IN, 160, 0, false);
    }

    set_wifi_reconnecting_ui(false);

    if (!status || !status->is_online) {
        lv_label_set_text(ui.lbl_err_title, "ZMYWARKA POZA ZASIĘGIEM");
        lv_label_set_text_fmt(ui.lbl_err_desc,
            "Brak odpowiedzi z IP %s.\nSprawdź zasilanie zmywarki.",
            status ? status->host_ip : DISHWASHER_IP);
        lv_obj_clear_flag(ui.btn_discover, LV_OBJ_FLAG_HIDDEN);
        ui_show_only_pane(ui.pane_offline);
        return;
    }

    ui.had_online_status = true;

    if (status->cycle_state == MIDEA_STATE_ERROR || status->error_code != 0) {
        const char *err_pl = "Sprawdź zmywarkę i zamknij zawór wody.";
        if (status->error_code == 1) err_pl = "Błąd E1: Brak dopływu wody lub zakręcony kran.";
        else if (status->error_code == 3) err_pl = "Błąd E3: Problem z grzaniem wody.";
        else if (status->error_code == 4) err_pl = "Błąd E4: Wykryto wyciek (Aquastop)!";

        lv_label_set_text(ui.lbl_err_title, "AWARIA ZMYWARKI");
        lv_label_set_text(ui.lbl_err_desc, err_pl);
        lv_obj_add_flag(ui.btn_discover, LV_OBJ_FLAG_HIDDEN);
        ui_show_only_pane(ui.pane_offline);
        return;
    }

    if (status->cycle_state == MIDEA_STATE_WORK) {
        ui_work_update_status(status);
        return;
    }

    if (status->cycle_state == MIDEA_STATE_FINISH && !ui.dismiss_finish) {
        ui.last_left_min = -1;
        ui.last_pct = -1;
        ui.peek_active = false;
        if (ui.peek_timer) lv_timer_pause(ui.peek_timer);
        if (ui.finish_hour < 0) {
            time_t now = 0;
            struct tm cur_t = {0};
            time(&now);
            localtime_r(&now, &cur_t);
            if (cur_t.tm_year >= (2024 - 1900)) {
                ui.finish_hour = cur_t.tm_hour;
                ui.finish_min = cur_t.tm_min;
            }
        }
        if (ui.lbl_fin_s) {
            if (ui.finish_hour >= 0) {
                lv_label_set_text_fmt(ui.lbl_fin_s, "Skończono o %02d:%02d · Uchyl drzwi.", ui.finish_hour, ui.finish_min);
            } else {
                lv_label_set_text(ui.lbl_fin_s, "Naczynia są czyste. Uchyl drzwiczki.");
            }
        }
        if (ui.lbl_finish_warn) {
            if (status->rinse_aid_lack && status->salt_lack) {
                lv_label_set_text(ui.lbl_finish_warn, "Przypomnienie: Dolej nabłyszczacz i dosyp sól");
                lv_obj_clear_flag(ui.lbl_finish_warn, LV_OBJ_FLAG_HIDDEN);
            } else if (status->rinse_aid_lack) {
                lv_label_set_text(ui.lbl_finish_warn, "Przypomnienie: Dolej nabłyszczacz!");
                lv_obj_clear_flag(ui.lbl_finish_warn, LV_OBJ_FLAG_HIDDEN);
            } else if (status->salt_lack) {
                lv_label_set_text(ui.lbl_finish_warn, "Przypomnienie: Dosyp sól do zmywarki!");
                lv_obj_clear_flag(ui.lbl_finish_warn, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui.lbl_finish_warn, LV_OBJ_FLAG_HIDDEN);
            }
        }
        ui_show_only_pane(ui.pane_finish);
        return;
    }

    // widok gotowosci
    if (status->cycle_state != MIDEA_STATE_FINISH) {
        ui.dismiss_finish = false;
    }
    ui.last_left_min = -1;
    ui.last_pct = -1;
    ui.finish_hour = -1;
    ui.finish_min = -1;
    ui.peek_active = false;
    if (ui.peek_timer) lv_timer_pause(ui.peek_timer);
    ui.door_closed = status->door_closed;
    ui.cached_rinse_lack = status->rinse_aid_lack;
    ui.cached_salt_lack = status->salt_lack;

    lv_obj_set_style_bg_color(ui.dot_idle_status, status->door_closed ? TH_MINT : TH_RED, 0);
    ui_idle_refresh_supply_banner();

    ui_idle_update_visuals();
    ui_show_only_pane(ui.pane_idle);
}
