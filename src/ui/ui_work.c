#include <math.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui_internal.h"

#define ARC_TEX_SIZE 256

static const char *TAG = "UI_WORK";

static lv_color_t *s_arc_grad_buf = NULL;
static lv_img_dsc_t s_arc_grad_dsc = {0};

static bool s_stop_hold_fired = false;
static bool s_stop_left_bounds = false;
static lv_timer_t *s_stop_hint_timer = NULL;

static lv_color_t lerp_rgb24_to_lv(uint32_t c0, uint32_t c1, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    uint8_t r0 = (c0 >> 16) & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = c0 & 0xFF;
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r = (uint8_t)(r0 + (r1 - r0) * t + 0.5f);
    uint8_t g = (uint8_t)(g0 + (g1 - g0) * t + 0.5f);
    uint8_t b = (uint8_t)(b0 + (b1 - b0) * t + 0.5f);
    return lv_color_make(r, g, b);
}

static uint32_t blend_rgb24_u32(uint32_t c0, uint32_t c1, float t)
{
    uint8_t r0 = (c0 >> 16) & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = c0 & 0xFF;
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint32_t r = (uint32_t)(r0 + (r1 - r0) * t + 0.5f);
    uint32_t g = (uint32_t)(g0 + (g1 - g0) * t + 0.5f);
    uint32_t b = (uint32_t)(b0 + (b1 - b0) * t + 0.5f);
    return (r << 16) | (g << 8) | b;
}

static bool update_arc_gradient_tex(const ui_palette_t *p)
{
    if (!p) return false;
    if (!s_arc_grad_buf) {
        size_t sz = (size_t)ARC_TEX_SIZE * ARC_TEX_SIZE * sizeof(lv_color_t);
        s_arc_grad_buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_arc_grad_buf) {
            s_arc_grad_buf = heap_caps_malloc(sz, MALLOC_CAP_8BIT);
        }
        if (!s_arc_grad_buf) {
            ESP_LOGW(TAG, "Brak pamieci na teksture luku");
            return false;
        }
        s_arc_grad_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_arc_grad_dsc.header.always_zero = 0;
        s_arc_grad_dsc.header.reserved = 0;
        s_arc_grad_dsc.header.w = ARC_TEX_SIZE;
        s_arc_grad_dsc.header.h = ARC_TEX_SIZE;
        s_arc_grad_dsc.data_size = (uint32_t)sz;
        s_arc_grad_dsc.data = (const uint8_t *)s_arc_grad_buf;
    }

    uint32_t c0 = blend_rgb24_u32(p->start_bot, p->start_top, 0.35f);
    uint32_t c1 = (p->accent == p->start_bot || p->accent == p->start_top) ? p->accent_orbit : p->accent;
    uint32_t c2 = p->mint;
    lv_color_t c0_lv = lv_color_hex(c0);

    const float two_pi = 6.283185307f;
    for (int y = 0; y < ARC_TEX_SIZE; y++) {
        float dy = (float)y - 127.5f;
        lv_color_t *row = s_arc_grad_buf + (y * ARC_TEX_SIZE);
        for (int x = 0; x < ARC_TEX_SIZE; x++) {
            float dx = (float)x - 127.5f;
            float r2 = dx * dx + dy * dy;
            if (r2 < 9216.0f || r2 > 18496.0f) { // poza pasmem r=[96..136]
                row[x] = c0_lv;
                continue;
            }
            float ang = atan2f(dx, -dy); // 0 rad = godzina 12:00
            if (ang < 0.0f) ang += two_pi;
            float u = ang / two_pi;
            // obcinamy ostatnie ~5 st. przy 12:00 zeby nie bylo szwu na zaokragleniu startu
            if (u >= 0.985f) {
                row[x] = c0_lv;
            } else if (u < 0.45f) {
                row[x] = lerp_rgb24_to_lv(c0, c1, u / 0.45f);
            } else {
                row[x] = lerp_rgb24_to_lv(c1, c2, (u - 0.45f) / 0.45f);
            }
        }
    }
    return true;
}

static void anim_arc_val_cb(void *var, int32_t v)
{
    lv_arc_set_value((lv_obj_t *)var, (int16_t)v);
}

static void on_stop_hint_timeout(lv_timer_t *t)
{
    if (t) lv_timer_pause(t);
    if (ui.lbl_stop) lv_label_set_text(ui.lbl_stop, "ZATRZYMAJ");
}

static void on_stop_fill_done(lv_anim_t *a)
{
    (void)a;
    if (ui.obj_stop_fill) lv_obj_set_width(ui.obj_stop_fill, 0);
    if (s_stop_left_bounds || !ui.btn_stop || !lv_obj_has_state(ui.btn_stop, LV_STATE_PRESSED)) return;
    s_stop_hold_fired = true;
    if (s_stop_hint_timer) lv_timer_pause(s_stop_hint_timer);
    if (ui.lbl_stop) lv_label_set_text(ui.lbl_stop, "ZATRZYMAJ");
    if (ui.action_cb) ui.action_cb(UI_ACTION_CANCEL);
}

static void on_stop_hold(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_stop_hold_fired = false;
        s_stop_left_bounds = false;
        ui_start_hold_fill_anim(ui.obj_stop_fill, 132, 90, 760, on_stop_fill_done);
    } else if (code == LV_EVENT_PRESSING) {
        if (s_stop_hold_fired || s_stop_left_bounds) return;
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t pt = {0};
        lv_indev_get_point(indev, &pt);
        if (!ui_point_in_obj(ui.btn_stop, &pt)) {
            s_stop_left_bounds = true;
            ui_cancel_hold_fill_anim(ui.obj_stop_fill, true);
        }
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        if (s_stop_hold_fired || s_stop_left_bounds) return;
        if (ui.lbl_stop) lv_label_set_text(ui.lbl_stop, "Przytrzymaj");
        if (!s_stop_hint_timer) {
            s_stop_hint_timer = lv_timer_create(on_stop_hint_timeout, 1200, NULL);
        } else {
            lv_timer_reset(s_stop_hint_timer);
            lv_timer_resume(s_stop_hint_timer);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        ui_cancel_hold_fill_anim(ui.obj_stop_fill, !s_stop_hold_fired && !s_stop_left_bounds);
        s_stop_hold_fired = false;
        s_stop_left_bounds = false;
    }
}

void ui_work_update_center_view(void)
{
    if (!ui.lbl_work_time || !ui.lbl_work_peek_eta) return;

    if (!ui.last_door_closed) {
        lv_obj_add_flag(ui.lbl_work_time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_peek_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_peek_eta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_peek_stage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.lbl_work_door_warn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(ui.lbl_work_door_warn, LV_OBJ_FLAG_HIDDEN);
    if (ui.peek_active) {
        lv_obj_add_flag(ui.lbl_work_time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.lbl_work_peek_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.lbl_work_peek_eta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.lbl_work_peek_stage, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.lbl_work_peek_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_peek_eta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.lbl_work_peek_stage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.lbl_work_time, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.lbl_work_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui.lbl_work_temp, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_peek_timeout(lv_timer_t *t)
{
    ui.peek_active = false;
    if (t) lv_timer_pause(t);
    ui_work_update_center_view();
}

static void on_dial_peek(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_LONG_PRESSED) {
        ui_open_theme_menu();
        return;
    }
    if (code != LV_EVENT_SHORT_CLICKED) return;
    if (!ui.last_door_closed) return;

    ui.peek_active = !ui.peek_active;
    if (ui.peek_timer) {
        if (ui.peek_active) {
            lv_timer_reset(ui.peek_timer);
            lv_timer_resume(ui.peek_timer);
        } else {
            lv_timer_pause(ui.peek_timer);
        }
    }
    ui_work_update_center_view();
}

void ui_create_work_pane(lv_obj_t *scr)
{
    const ui_palette_t *p = ui_theme_get();

    ui.pane_work = ui_make_bento_container(scr, true);

    ui.tile_dial = lv_obj_create(ui.pane_work);
    lv_obj_set_size(ui.tile_dial, 460, 300);
    lv_obj_add_style(ui.tile_dial, &st_bento_tile, 0);
    lv_obj_set_style_pad_all(ui.tile_dial, 6, 0);
    lv_obj_clear_flag(ui.tile_dial, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui.tile_dial, 10, 10);

    ui.dot_work = lv_obj_create(ui.tile_dial);
    lv_obj_set_size(ui.dot_work, 10, 10);
    lv_obj_set_style_radius(ui.dot_work, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui.dot_work, 0, 0);
    lv_obj_set_style_bg_color(ui.dot_work, TH_CYAN, 0);
    lv_obj_align(ui.dot_work, LV_ALIGN_TOP_LEFT, 8, 12);

    ui.lbl_work_prog = lv_label_create(ui.tile_dial);
    lv_label_set_text(ui.lbl_work_prog, "30 MINUT");
    lv_obj_set_style_text_font(ui.lbl_work_prog, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_work_prog, TH_TEXT_SUB, 0);
    lv_obj_align(ui.lbl_work_prog, LV_ALIGN_TOP_LEFT, 24, 7);

    ui.btn_w_sleep = lv_btn_create(ui.tile_dial);
    lv_obj_set_size(ui.btn_w_sleep, 96, 34);
    lv_obj_set_style_bg_color(ui.btn_w_sleep, lv_color_hex(p->sleep_bg), 0);
    lv_obj_set_style_radius(ui.btn_w_sleep, 12, 0);
    lv_obj_set_style_border_color(ui.btn_w_sleep, TH_BORDER_LIGHT, 0);
    lv_obj_set_style_border_width(ui.btn_w_sleep, 1, 0);
    lv_obj_set_style_pad_all(ui.btn_w_sleep, 0, 0);
    lv_obj_set_style_shadow_width(ui.btn_w_sleep, 0, 0);
    lv_obj_align(ui.btn_w_sleep, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_ext_click_area(ui.btn_w_sleep, 10);
    lv_obj_add_event_cb(ui.btn_w_sleep, ui_on_sleep_now, LV_EVENT_CLICKED, NULL);

    ui.w_moon_ic = ui_theme_create_icon(ui.btn_w_sleep, &ic_moon, TH_TEXT_SUB);
    lv_obj_align(ui.w_moon_ic, LV_ALIGN_LEFT_MID, 12, 0);

    ui.lbl_ws = lv_label_create(ui.btn_w_sleep);
    lv_label_set_text(ui.lbl_ws, "Uśpij");
    lv_obj_set_style_text_font(ui.lbl_ws, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_ws, TH_TEXT, 0);
    lv_obj_align(ui.lbl_ws, LV_ALIGN_LEFT_MID, 38, 0);

    ui.arc_orbit = lv_arc_create(ui.tile_dial);
    lv_obj_set_size(ui.arc_orbit, 268, 268);
    lv_obj_center(ui.arc_orbit);
    lv_arc_set_bg_angles(ui.arc_orbit, 0, 360);
    lv_arc_set_angles(ui.arc_orbit, 0, 44);
    lv_arc_set_rotation(ui.arc_orbit, 270);
    lv_obj_clear_flag(ui.arc_orbit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(ui.arc_orbit, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_opa(ui.arc_orbit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui.arc_orbit, lv_color_hex(p->accent_orbit), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ui.arc_orbit, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ui.arc_orbit, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ui.arc_orbit, true, LV_PART_INDICATOR);

    ui.arc_work = lv_arc_create(ui.tile_dial);
    lv_obj_set_size(ui.arc_work, 256, 256);
    lv_obj_center(ui.arc_work);
    lv_arc_set_rotation(ui.arc_work, 270);
    lv_arc_set_bg_angles(ui.arc_work, 0, 360);
    lv_arc_set_range(ui.arc_work, 0, 100);
    lv_arc_set_value(ui.arc_work, 25);
    lv_obj_clear_flag(ui.arc_work, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(ui.arc_work, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(ui.arc_work, lv_color_hex(p->sleep_bg), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ui.arc_work, 20, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(ui.arc_work, true, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ui.arc_work, TH_CYAN, LV_PART_INDICATOR);
    if (update_arc_gradient_tex(p)) {
        lv_obj_set_style_arc_img_src(ui.arc_work, &s_arc_grad_dsc, LV_PART_INDICATOR);
    }
    lv_obj_set_style_arc_width(ui.arc_work, 20, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ui.arc_work, true, LV_PART_INDICATOR);

    ui.lbl_work_time = lv_label_create(ui.arc_work);
    lv_label_set_text(ui.lbl_work_time, "--");
    lv_obj_set_style_text_font(ui.lbl_work_time, &pl_font_104, 0);
    lv_obj_set_style_text_color(ui.lbl_work_time, TH_TEXT, 0);
    lv_obj_align(ui.lbl_work_time, LV_ALIGN_CENTER, 0, -16);

    ui.lbl_work_unit = lv_label_create(ui.arc_work);
    lv_label_set_text(ui.lbl_work_unit, "minut");
    lv_obj_set_style_text_font(ui.lbl_work_unit, &pl_font_22, 0);
    lv_obj_set_style_text_color(ui.lbl_work_unit, TH_CYAN, 0);
    lv_obj_align(ui.lbl_work_unit, LV_ALIGN_CENTER, 0, 42);

    ui.lbl_work_temp = lv_label_create(ui.arc_work);
    lv_label_set_text(ui.lbl_work_temp, "");
    lv_obj_set_style_text_font(ui.lbl_work_temp, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_work_temp, TH_AMBER, 0);
    lv_obj_align(ui.lbl_work_temp, LV_ALIGN_CENTER, 0, 68);

    ui.lbl_work_peek_cap = lv_label_create(ui.arc_work);
    lv_label_set_text(ui.lbl_work_peek_cap, "Koniec o");
    lv_obj_set_style_text_font(ui.lbl_work_peek_cap, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_work_peek_cap, TH_TEXT_SUB, 0);
    lv_obj_align(ui.lbl_work_peek_cap, LV_ALIGN_CENTER, 0, -48);
    lv_obj_add_flag(ui.lbl_work_peek_cap, LV_OBJ_FLAG_HIDDEN);

    ui.lbl_work_peek_eta = lv_label_create(ui.arc_work);
    lv_label_set_text(ui.lbl_work_peek_eta, "--:--");
    lv_obj_set_style_text_font(ui.lbl_work_peek_eta, &pl_font_52, 0);
    lv_obj_set_style_text_color(ui.lbl_work_peek_eta, TH_CYAN, 0);
    lv_obj_align(ui.lbl_work_peek_eta, LV_ALIGN_CENTER, 0, -4);
    lv_obj_add_flag(ui.lbl_work_peek_eta, LV_OBJ_FLAG_HIDDEN);

    ui.lbl_work_peek_stage = lv_label_create(ui.arc_work);
    lv_label_set_text(ui.lbl_work_peek_stage, "");
    lv_obj_set_style_text_font(ui.lbl_work_peek_stage, &pl_font_16, 0);
    lv_obj_set_style_text_color(ui.lbl_work_peek_stage, TH_AMBER, 0);
    lv_obj_align(ui.lbl_work_peek_stage, LV_ALIGN_CENTER, 0, 44);
    lv_obj_add_flag(ui.lbl_work_peek_stage, LV_OBJ_FLAG_HIDDEN);

    ui.lbl_work_door_warn = lv_label_create(ui.arc_work);
    lv_label_set_text(ui.lbl_work_door_warn, "DRZWI\nOTWARTE!");
    lv_obj_set_style_text_align(ui.lbl_work_door_warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ui.lbl_work_door_warn, &pl_font_30, 0);
    lv_obj_set_style_text_color(ui.lbl_work_door_warn, TH_AMBER, 0);
    lv_obj_center(ui.lbl_work_door_warn);
    lv_obj_add_flag(ui.lbl_work_door_warn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn_dial_peek = lv_obj_create(ui.tile_dial);
    lv_obj_set_size(btn_dial_peek, 190, 190);
    lv_obj_center(btn_dial_peek);
    lv_obj_clear_flag(btn_dial_peek, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn_dial_peek, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(btn_dial_peek, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_dial_peek, 0, 0);
    lv_obj_add_event_cb(btn_dial_peek, on_dial_peek, LV_EVENT_ALL, NULL);

    ui.peek_timer = lv_timer_create(on_peek_timeout, 4000, NULL);
    if (ui.peek_timer) lv_timer_pause(ui.peek_timer);

    ui.btn_stop = lv_btn_create(ui.tile_dial);
    lv_obj_set_size(ui.btn_stop, 132, 44);
    lv_obj_add_style(ui.btn_stop, &st_btn_danger, 0);
    lv_obj_add_style(ui.btn_stop, &st_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui.btn_stop, 14, 0);
    lv_obj_set_style_pad_all(ui.btn_stop, 0, 0);
    lv_obj_set_style_clip_corner(ui.btn_stop, true, 0);
    lv_obj_align(ui.btn_stop, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_set_ext_click_area(ui.btn_stop, 12);
    lv_obj_add_event_cb(ui.btn_stop, on_stop_hold, LV_EVENT_ALL, NULL);

    ui.obj_stop_fill = lv_obj_create(ui.btn_stop);
    lv_obj_set_pos(ui.obj_stop_fill, 0, 0);
    lv_obj_set_size(ui.obj_stop_fill, 0, 44);
    lv_obj_clear_flag(ui.obj_stop_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(ui.obj_stop_fill, 0, 0);
    lv_obj_set_style_border_width(ui.obj_stop_fill, 0, 0);
    lv_obj_set_style_pad_all(ui.obj_stop_fill, 0, 0);
    lv_obj_set_style_bg_color(ui.obj_stop_fill, lv_color_hex(0xFCA5A5), 0);
    lv_obj_set_style_bg_opa(ui.obj_stop_fill, LV_OPA_50, 0);

    ui.lbl_stop = lv_label_create(ui.btn_stop);
    lv_label_set_text(ui.lbl_stop, "ZATRZYMAJ");
    lv_obj_set_style_text_font(ui.lbl_stop, &pl_font_18, 0);
    lv_obj_set_style_text_color(ui.lbl_stop, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(ui.lbl_stop);
}

void ui_work_apply_theme(const ui_palette_t *p)
{
    lv_color_t c_text = lv_color_hex(p->text);
    lv_color_t c_sub = lv_color_hex(p->text_sub);
    lv_color_t c_sleep = lv_color_hex(p->sleep_bg);
    lv_color_t c_accent = lv_color_hex(p->accent);

    if (ui.dot_work) lv_obj_set_style_bg_color(ui.dot_work, c_accent, 0);
    if (ui.lbl_work_prog) lv_obj_set_style_text_color(ui.lbl_work_prog, c_sub, 0);
    if (ui.btn_w_sleep) {
        lv_obj_set_style_bg_color(ui.btn_w_sleep, c_sleep, 0);
        lv_obj_set_style_border_color(ui.btn_w_sleep, lv_color_hex(p->border_light), 0);
    }
    if (ui.w_moon_ic) lv_obj_set_style_img_recolor(ui.w_moon_ic, c_sub, 0);
    if (ui.lbl_ws) lv_obj_set_style_text_color(ui.lbl_ws, c_text, 0);
    if (ui.arc_work) {
        lv_obj_set_style_arc_color(ui.arc_work, c_sleep, LV_PART_MAIN);
        if (update_arc_gradient_tex(p)) {
            lv_obj_set_style_arc_img_src(ui.arc_work, &s_arc_grad_dsc, LV_PART_INDICATOR);
        }
        lv_obj_set_style_arc_color(ui.arc_work, (ui.last_pct >= 80) ? lv_color_hex(p->mint) : c_accent, LV_PART_INDICATOR);
        lv_obj_invalidate(ui.arc_work);
    }
    if (ui.arc_orbit) {
        lv_obj_set_style_arc_color(ui.arc_orbit, lv_color_hex(p->accent_orbit), LV_PART_INDICATOR);
    }
    if (ui.lbl_work_time) lv_obj_set_style_text_color(ui.lbl_work_time, c_text, 0);
    if (ui.lbl_work_unit) lv_obj_set_style_text_color(ui.lbl_work_unit, c_accent, 0);
    if (ui.lbl_work_temp) lv_obj_set_style_text_color(ui.lbl_work_temp, lv_color_hex(p->amber), 0);
    if (ui.lbl_work_peek_cap) lv_obj_set_style_text_color(ui.lbl_work_peek_cap, c_sub, 0);
    if (ui.lbl_work_peek_eta) lv_obj_set_style_text_color(ui.lbl_work_peek_eta, c_accent, 0);
    if (ui.lbl_work_peek_stage) lv_obj_set_style_text_color(ui.lbl_work_peek_stage, lv_color_hex(p->amber), 0);
    if (ui.lbl_work_door_warn) lv_obj_set_style_text_color(ui.lbl_work_door_warn, lv_color_hex(p->amber), 0);
}

void ui_work_update_status(const midea_status_t *status)
{
    ui.dismiss_finish = false;
    ui.finish_hour = -1;
    ui.finish_min = -1;
    ui.last_door_closed = status->door_closed;
    if (ui.dot_work) lv_obj_set_style_bg_color(ui.dot_work, TH_CYAN, 0);
    lv_label_set_text(ui.lbl_work_prog, status->mode_name_pl);

    int left_min = status->left_time_min;
    if (left_min != ui.last_left_min) {
        ui.last_left_min = left_min;
        if (left_min > 0) {
            lv_label_set_text_fmt(ui.lbl_work_time, "%d", left_min);
            if (left_min == 1) lv_label_set_text(ui.lbl_work_unit, "minuta");
            else if (left_min % 10 >= 2 && left_min % 10 <= 4 && (left_min % 100 < 10 || left_min % 100 >= 20)) lv_label_set_text(ui.lbl_work_unit, "minuty");
            else lv_label_set_text(ui.lbl_work_unit, "minut");
        } else {
            lv_label_set_text(ui.lbl_work_time, "--");
            lv_label_set_text(ui.lbl_work_unit, "minut");
        }
    }

    if (status->temperature_c > 0) {
        lv_label_set_text_fmt(ui.lbl_work_temp, "%u°C", status->temperature_c);
    } else {
        lv_label_set_text(ui.lbl_work_temp, "");
    }

    time_t now = 0;
    struct tm cur_t = {0};
    time(&now);
    localtime_r(&now, &cur_t);
    if (cur_t.tm_year >= (2024 - 1900) && left_min > 0) {
        time_t fin = now + ((time_t)left_min * 60);
        struct tm fin_t = {0};
        localtime_r(&fin, &fin_t);
        lv_label_set_text(ui.lbl_work_peek_cap, "Koniec o");
        lv_label_set_text_fmt(ui.lbl_work_peek_eta, "%02d:%02d", fin_t.tm_hour, fin_t.tm_min);
    } else {
        lv_label_set_text(ui.lbl_work_peek_cap, "Trwa cykl");
        lv_label_set_text(ui.lbl_work_peek_eta, "--:--");
    }
    lv_label_set_text(ui.lbl_work_peek_stage, status->stage_name_pl[0] ? status->stage_name_pl : "");
    ui_work_update_center_view();

    int est_total = 60;
    if (status->mode_code == 0x07) est_total = 30;
    else if (status->mode_code == 0x04) est_total = 195;
    else if (status->mode_code == 0x02) est_total = 160;
    int pct = 100 - (left_min * 100) / est_total;
    if (pct < 5) pct = 5;
    if (pct > 99) pct = 99;
    if (pct != ui.last_pct) {
        int16_t from_pct = (ui.last_pct < 0) ? 0 : (int16_t)lv_arc_get_value(ui.arc_work);
        ui.last_pct = pct;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ui.arc_work);
        lv_anim_set_exec_cb(&a, anim_arc_val_cb);
        lv_anim_set_values(&a, from_pct, pct);
        lv_anim_set_time(&a, 550);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);

        const ui_palette_t *p = ui_theme_get();
        lv_color_t c = (pct >= 80) ? lv_color_hex(p->mint) : lv_color_hex(p->accent);
        lv_obj_set_style_arc_color(ui.arc_work, c, LV_PART_INDICATOR);
        if (ui.arc_orbit) {
            lv_obj_set_style_arc_color(ui.arc_orbit,
                (pct >= 80) ? lv_color_hex(p->mint) : lv_color_hex(p->accent_orbit), LV_PART_INDICATOR);
        }
    }

    ui_show_only_pane(ui.pane_work);
}
