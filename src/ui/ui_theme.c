#include "ui_theme.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "config.h"

#define NVS_KEY_UI_THEME "ui_theme"

static const ui_palette_t s_palettes[] = {
    // 0. Domyslny
    {
        .name = "Domyślny (Ocean Cobalt)",
        .is_light = false,
        .bg = 0x05070B, .tile = 0x0E131F, .tile_dark = 0x080C14, .sleep_bg = 0x1E293B,
        .border = 0x222D42, .border_light = 0x334155,
        .text = 0xF8FAFC, .text_sub = 0xCBD5E1, .muted = 0x64748B,
        .start_top = 0x0EA5E9, .start_bot = 0x0369A1, .start_border = 0x38BDF8, .start_sub = 0xBAE6FD,
        .sel_bg = 0x0C4A6E, .sel_border = 0x38BDF8, .sel_icon_bg = 0x0EA5E9, .sel_dot = 0x38BDF8, .sel_title = 0xFFFFFF,
        .unsel_icon_bg = 0x111827, .unsel_title = 0x94A3B8,
        .accent = 0x38BDF8, .accent_orbit = 0x7DD3FC, .mint = 0x10B981, .mint_dark = 0x064E3B,
        .amber = 0xF59E0B, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xEF4444, .red_bot = 0xB91C1C, .red_dark = 0x3F0B0B, .red_border = 0xF87171,
    },
    // 1. Bialy Szmaragd
    {
        .name = "Biały Szmaragd",
        .is_light = true,
        .bg = 0xEFF3F6, .tile = 0xFFFFFF, .tile_dark = 0xE2E8F0, .sleep_bg = 0xE2E8F0,
        .border = 0xCBD5E1, .border_light = 0x94A3B8,
        .text = 0x0F172A, .text_sub = 0x475569, .muted = 0x64748B,
        .start_top = 0x10B981, .start_bot = 0x059669, .start_border = 0x047857, .start_sub = 0xD1FAE5,
        .sel_bg = 0xECFDF5, .sel_border = 0x059669, .sel_icon_bg = 0x059669, .sel_dot = 0x10B981, .sel_title = 0x064E3B,
        .unsel_icon_bg = 0xE2E8F0, .unsel_title = 0x475569,
        .accent = 0x059669, .accent_orbit = 0x34D399, .mint = 0x10B981, .mint_dark = 0x065F46,
        .amber = 0xD97706, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xEF4444, .red_bot = 0xDC2626, .red_dark = 0x7F1D1D, .red_border = 0xB91C1C,
    },
    // 2. Bialy Papier & Czerwien
    {
        .name = "Biały Papier & Czerwień",
        .is_light = true,
        .bg = 0xF1F1F2, .tile = 0xFFFFFF, .tile_dark = 0xE4E4E7, .sleep_bg = 0xE4E4E7,
        .border = 0xD4D4D8, .border_light = 0xA1A1AA,
        .text = 0x18181B, .text_sub = 0x52525B, .muted = 0x71717A,
        .start_top = 0x27272A, .start_bot = 0x09090B, .start_border = 0x09090B, .start_sub = 0xD4D4D8,
        .sel_bg = 0xFEF2F2, .sel_border = 0xDC2626, .sel_icon_bg = 0xDC2626, .sel_dot = 0xEF4444, .sel_title = 0x7F1D1D,
        .unsel_icon_bg = 0xE4E4E7, .unsel_title = 0x52525B,
        .accent = 0xDC2626, .accent_orbit = 0xF87171, .mint = 0x16A34A, .mint_dark = 0x14532D,
        .amber = 0xEA580C, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xDC2626, .red_bot = 0x991B1B, .red_dark = 0x7F1D1D, .red_border = 0x991B1B,
    },
    // 3. Bialy Krem & Pomarancz
    {
        .name = "Biały Krem & Pomarańcz",
        .is_light = true,
        .bg = 0xFBF7F0, .tile = 0xFFFFFF, .tile_dark = 0xF3EDE2, .sleep_bg = 0xEFE6D8,
        .border = 0xE2D6C3, .border_light = 0xC7B59B,
        .text = 0x1C1917, .text_sub = 0x57534E, .muted = 0x78716C,
        .start_top = 0xF97316, .start_bot = 0xEA580C, .start_border = 0xC2410C, .start_sub = 0xFFEDD5,
        .sel_bg = 0xFFFBEB, .sel_border = 0xD97706, .sel_icon_bg = 0xD97706, .sel_dot = 0xF59E0B, .sel_title = 0x78350F,
        .unsel_icon_bg = 0xF3EDE2, .unsel_title = 0x57534E,
        .accent = 0xEA580C, .accent_orbit = 0xFB923C, .mint = 0x16A34A, .mint_dark = 0x14532D,
        .amber = 0xD97706, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xE11D48, .red_bot = 0xBE123C, .red_dark = 0x881337, .red_border = 0x9F1239,
    },
    // 4. Sloneczny Pomarancz
    {
        .name = "Słoneczny Pomarańcz",
        .is_light = false,
        .bg = 0x08080A, .tile = 0x16161A, .tile_dark = 0x0D0D10, .sleep_bg = 0x27272E,
        .border = 0x2E2E36, .border_light = 0x454552,
        .text = 0xF8FAFC, .text_sub = 0xD4D4D8, .muted = 0x71717A,
        .start_top = 0xF97316, .start_bot = 0xC2410C, .start_border = 0xFDBA74, .start_sub = 0xFED7AA,
        .sel_bg = 0x451A03, .sel_border = 0xF59E0B, .sel_icon_bg = 0xF59E0B, .sel_dot = 0xFBBF24, .sel_title = 0xFFFFFF,
        .unsel_icon_bg = 0x27272E, .unsel_title = 0xA1A1AA,
        .accent = 0xFBBF24, .accent_orbit = 0xFDE68A, .mint = 0x22C55E, .mint_dark = 0x064E3B,
        .amber = 0xFB7185, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xEF4444, .red_bot = 0x991B1B, .red_dark = 0x3F0B0B, .red_border = 0xFCA5A5,
    },
    // 5. Krwisty OLED
    {
        .name = "Krwisty OLED",
        .is_light = false,
        .bg = 0x050506, .tile = 0x131316, .tile_dark = 0x0A0A0C, .sleep_bg = 0x232328,
        .border = 0x2A2A30, .border_light = 0x42424C,
        .text = 0xFAFAFA, .text_sub = 0xD4D4D8, .muted = 0x71717A,
        .start_top = 0xEF4444, .start_bot = 0x991B1B, .start_border = 0xF87171, .start_sub = 0xFECACA,
        .sel_bg = 0x3F0B0B, .sel_border = 0xEF4444, .sel_icon_bg = 0xEF4444, .sel_dot = 0xF87171, .sel_title = 0xFFFFFF,
        .unsel_icon_bg = 0x232328, .unsel_title = 0xA1A1AA,
        .accent = 0xEF4444, .accent_orbit = 0xFCA5A5, .mint = 0x22C55E, .mint_dark = 0x064E3B,
        .amber = 0xFBBF24, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xDC2626, .red_bot = 0x7F1D1D, .red_dark = 0x3F0B0B, .red_border = 0xFCA5A5,
    },
    // 6. Lesny Jadeit
    {
        .name = "Leśny Jadeit",
        .is_light = false,
        .bg = 0x070806, .tile = 0x141712, .tile_dark = 0x0C0E0B, .sleep_bg = 0x22281E,
        .border = 0x2C3327, .border_light = 0x444F3C,
        .text = 0xF7FEE7, .text_sub = 0xD9F99D, .muted = 0x718069,
        .start_top = 0x16A34A, .start_bot = 0x14532D, .start_border = 0x4ADE80, .start_sub = 0xBBF7D0,
        .sel_bg = 0x431407, .sel_border = 0xEA580C, .sel_icon_bg = 0xEA580C, .sel_dot = 0xFB923C, .sel_title = 0xFFFFFF,
        .unsel_icon_bg = 0x1F281A, .unsel_title = 0xA3B899,
        .accent = 0x84CC16, .accent_orbit = 0xBEF264, .mint = 0x4ADE80, .mint_dark = 0x064E3B,
        .amber = 0xFB923C, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xDC2626, .red_bot = 0x7F1D1D, .red_dark = 0x3F0B0B, .red_border = 0xF87171,
    },
    // 7. Krolewska Sliwka
    {
        .name = "Królewska Śliwka",
        .is_light = false,
        .bg = 0x09060C, .tile = 0x18121E, .tile_dark = 0x100B14, .sleep_bg = 0x2A2035,
        .border = 0x362944, .border_light = 0x513D66,
        .text = 0xFAF5FF, .text_sub = 0xE9D5FF, .muted = 0x8B7BA0,
        .start_top = 0x9333EA, .start_bot = 0x6B21A8, .start_border = 0xD8B4FE, .start_sub = 0xE9D5FF,
        .sel_bg = 0x500724, .sel_border = 0xF43F5E, .sel_icon_bg = 0xF43F5E, .sel_dot = 0xFDA4AF, .sel_title = 0xFFFFFF,
        .unsel_icon_bg = 0x2A2035, .unsel_title = 0xB8A6CC,
        .accent = 0xE879F9, .accent_orbit = 0xF5D0FE, .mint = 0x34D399, .mint_dark = 0x064E3B,
        .amber = 0xFDE047, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xF43F5E, .red_bot = 0x9F1239, .red_dark = 0x3F0B0B, .red_border = 0xFDA4AF,
    },
    // 8. Czysty OLED Minimal
    {
        .name = "Czysty OLED Minimal",
        .is_light = false,
        .bg = 0x020202, .tile = 0x121214, .tile_dark = 0x09090B, .sleep_bg = 0x222226,
        .border = 0x2C2C32, .border_light = 0x484852,
        .text = 0xFAFAFA, .text_sub = 0xD4D4D8, .muted = 0x71717A,
        .start_top = 0x3F3F46, .start_bot = 0x18181B, .start_border = 0x71717A, .start_sub = 0xA1A1AA,
        .sel_bg = 0x064E3B, .sel_border = 0x10B981, .sel_icon_bg = 0x10B981, .sel_dot = 0x6EE7B7, .sel_title = 0xFFFFFF,
        .unsel_icon_bg = 0x222226, .unsel_title = 0xA1A1AA,
        .accent = 0x10B981, .accent_orbit = 0x6EE7B7, .mint = 0x10B981, .mint_dark = 0x064E3B,
        .amber = 0xFACC15, .amber_top = 0xD97706, .amber_bot = 0x92400E,
        .red = 0xEF4444, .red_bot = 0x991B1B, .red_dark = 0x3F0B0B, .red_border = 0xF87171,
    },
};

#define PALETTE_COUNT (sizeof(s_palettes) / sizeof(s_palettes[0]))
static uint8_t s_cur_idx = 0;

lv_style_t st_screen;
lv_style_t st_bento_tile;
lv_style_t st_bento_selected;
lv_style_t st_btn_hero_cobalt;
lv_style_t st_btn_hero_amber;
lv_style_t st_btn_danger;
lv_style_t st_btn_pressed;

static void apply_styles(const ui_palette_t *p)
{
    lv_style_set_bg_color(&st_screen, lv_color_hex(p->bg));
    lv_style_set_bg_opa(&st_screen, LV_OPA_COVER);

    lv_style_set_bg_color(&st_bento_tile, lv_color_hex(p->tile));
    lv_style_set_bg_opa(&st_bento_tile, LV_OPA_COVER);
    lv_style_set_border_color(&st_bento_tile, lv_color_hex(p->border));
    lv_style_set_border_width(&st_bento_tile, 1);
    lv_style_set_radius(&st_bento_tile, 20);
    lv_style_set_pad_all(&st_bento_tile, 12);
    lv_style_set_shadow_width(&st_bento_tile, 0);

    lv_style_set_bg_color(&st_bento_selected, lv_color_hex(p->sel_bg));
    lv_style_set_bg_opa(&st_bento_selected, LV_OPA_COVER);
    lv_style_set_border_color(&st_bento_selected, lv_color_hex(p->sel_border));
    lv_style_set_border_width(&st_bento_selected, 3);
    lv_style_set_shadow_width(&st_bento_selected, 0);

    lv_style_set_bg_color(&st_btn_hero_cobalt, lv_color_hex(p->start_top));
    lv_style_set_bg_grad_color(&st_btn_hero_cobalt, lv_color_hex(p->start_bot));
    lv_style_set_bg_grad_dir(&st_btn_hero_cobalt, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&st_btn_hero_cobalt, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn_hero_cobalt, lv_color_hex(p->start_border));
    lv_style_set_border_width(&st_btn_hero_cobalt, 2);
    lv_style_set_radius(&st_btn_hero_cobalt, 22);
    lv_style_set_shadow_width(&st_btn_hero_cobalt, 0);

    lv_style_set_bg_color(&st_btn_hero_amber, lv_color_hex(p->amber_top));
    lv_style_set_bg_grad_color(&st_btn_hero_amber, lv_color_hex(p->amber_bot));
    lv_style_set_bg_grad_dir(&st_btn_hero_amber, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&st_btn_hero_amber, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn_hero_amber, lv_color_hex(p->amber));
    lv_style_set_border_width(&st_btn_hero_amber, 2);
    lv_style_set_radius(&st_btn_hero_amber, 22);
    lv_style_set_shadow_width(&st_btn_hero_amber, 0);

    lv_style_set_bg_color(&st_btn_danger, lv_color_hex(p->red));
    lv_style_set_bg_grad_color(&st_btn_danger, lv_color_hex(p->red_bot));
    lv_style_set_bg_grad_dir(&st_btn_danger, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&st_btn_danger, LV_OPA_COVER);
    lv_style_set_border_color(&st_btn_danger, lv_color_hex(p->red_border));
    lv_style_set_border_width(&st_btn_danger, 2);
    lv_style_set_radius(&st_btn_danger, 18);
    lv_style_set_shadow_width(&st_btn_danger, 0);

    lv_style_set_bg_opa(&st_btn_pressed, LV_OPA_80);
    lv_style_set_transform_width(&st_btn_pressed, -2);
    lv_style_set_transform_height(&st_btn_pressed, -2);
}

void ui_theme_init(void)
{
    lv_style_init(&st_screen);
    lv_style_init(&st_bento_tile);
    lv_style_init(&st_bento_selected);
    lv_style_init(&st_btn_hero_cobalt);
    lv_style_init(&st_btn_hero_amber);
    lv_style_init(&st_btn_danger);
    lv_style_init(&st_btn_pressed);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 0;
        if (nvs_get_u8(nvs, NVS_KEY_UI_THEME, &val) == ESP_OK && val < PALETTE_COUNT) {
            s_cur_idx = val;
        }
        nvs_close(nvs);
    }

    apply_styles(&s_palettes[s_cur_idx]);
}

uint8_t ui_theme_count(void) { return (uint8_t)PALETTE_COUNT; }
uint8_t ui_theme_get_idx(void) { return s_cur_idx; }
const ui_palette_t *ui_theme_get(void) { return &s_palettes[s_cur_idx]; }
const ui_palette_t *ui_theme_by_idx(uint8_t idx) { return &s_palettes[idx % PALETTE_COUNT]; }

void ui_theme_set(uint8_t idx)
{
    s_cur_idx = (uint8_t)(idx % PALETTE_COUNT);
    apply_styles(&s_palettes[s_cur_idx]);
}

void ui_theme_save(uint8_t idx)
{
    ui_theme_set(idx);
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_UI_THEME, s_cur_idx);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

lv_obj_t *ui_theme_create_icon(lv_obj_t *parent, const lv_img_dsc_t *src, lv_color_t tint)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_obj_set_style_img_recolor(img, tint, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}
