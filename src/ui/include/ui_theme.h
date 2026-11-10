/**
 * 2026 Bento Mosaic Dynamic Theme Engine (NVS-persisted palettes)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "pl_fonts.h"
#include "ui_icons.h"

typedef struct {
    const char *name;
    bool is_light;
    uint32_t bg;
    uint32_t tile;
    uint32_t tile_dark;
    uint32_t sleep_bg;
    uint32_t border;
    uint32_t border_light;
    uint32_t text;
    uint32_t text_sub;
    uint32_t muted;
    uint32_t start_top;
    uint32_t start_bot;
    uint32_t start_border;
    uint32_t start_sub;
    uint32_t sel_bg;
    uint32_t sel_border;
    uint32_t sel_icon_bg;
    uint32_t sel_dot;
    uint32_t sel_title;
    uint32_t unsel_icon_bg;
    uint32_t unsel_title;
    uint32_t accent;
    uint32_t accent_orbit;
    uint32_t mint;
    uint32_t mint_dark;
    uint32_t amber;
    uint32_t amber_top;
    uint32_t amber_bot;
    uint32_t red;
    uint32_t red_bot;
    uint32_t red_dark;
    uint32_t red_border;
} ui_palette_t;

extern lv_style_t st_screen;
extern lv_style_t st_bento_tile;
extern lv_style_t st_bento_selected;
extern lv_style_t st_btn_hero_cobalt;
extern lv_style_t st_btn_hero_amber;
extern lv_style_t st_btn_danger;
extern lv_style_t st_btn_pressed;

void ui_theme_init(void);
uint8_t ui_theme_count(void);
uint8_t ui_theme_get_idx(void);
const ui_palette_t *ui_theme_get(void);
const ui_palette_t *ui_theme_by_idx(uint8_t idx);
void ui_theme_set(uint8_t idx);
void ui_theme_save(uint8_t idx);

lv_obj_t *ui_theme_create_icon(lv_obj_t *parent, const lv_img_dsc_t *src, lv_color_t tint);

// Dynamic theme color accessors
#define TH_BG               lv_color_hex(ui_theme_get()->bg)
#define TH_TILE             lv_color_hex(ui_theme_get()->tile)
#define TH_TILE_DARK        lv_color_hex(ui_theme_get()->tile_dark)
#define TH_TILE_SEL         lv_color_hex(ui_theme_get()->sel_bg)
#define TH_BORDER           lv_color_hex(ui_theme_get()->border)
#define TH_BORDER_LIGHT     lv_color_hex(ui_theme_get()->border_light)

#define TH_TEXT             lv_color_hex(ui_theme_get()->text)
#define TH_TEXT_SUB         lv_color_hex(ui_theme_get()->text_sub)
#define TH_MUTED            lv_color_hex(ui_theme_get()->muted)

#define TH_COBALT_TOP       lv_color_hex(ui_theme_get()->start_top)
#define TH_COBALT_BOT       lv_color_hex(ui_theme_get()->start_bot)
#define TH_COBALT_BORDER    lv_color_hex(ui_theme_get()->sel_border)
#define TH_COBALT_DARK      lv_color_hex(ui_theme_get()->sel_bg)

#define TH_CYAN             lv_color_hex(ui_theme_get()->accent)
#define TH_MINT             lv_color_hex(ui_theme_get()->mint)
#define TH_MINT_DARK        lv_color_hex(ui_theme_get()->mint_dark)

#define TH_AMBER            lv_color_hex(ui_theme_get()->amber)
#define TH_AMBER_TOP        lv_color_hex(ui_theme_get()->amber_top)
#define TH_AMBER_BOT        lv_color_hex(ui_theme_get()->amber_bot)

#define TH_RED              lv_color_hex(ui_theme_get()->red)
#define TH_RED_BOT          lv_color_hex(ui_theme_get()->red_bot)
#define TH_RED_DARK         lv_color_hex(ui_theme_get()->red_dark)
#define TH_RED_BORDER       lv_color_hex(ui_theme_get()->red_border)
