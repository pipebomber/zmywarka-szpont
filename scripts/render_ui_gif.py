"""Renderuje glowny podglad GIF (docs/ui_showcase.gif) z gestami, pogoda 7-dniowa i animacjami."""

import math
import os
import sys
from pathlib import Path
from PIL import Image, ImageDraw

SCRIPTS_DIR = Path(__file__).resolve().parent
ROOT = SCRIPTS_DIR.parent
sys.path.insert(0, str(SCRIPTS_DIR))
import render_ui_1to1 as r1

OUT_GIF = str(ROOT / "docs" / "ui_showcase.gif")


def make_bezel(inner_screen: Image.Image, step_label: str) -> Image.Image:
    W, H = 520, 382
    frame = Image.new("RGBA", (W, H), (11, 15, 25, 255))
    d = ImageDraw.Draw(frame)
    r1.draw_text_left(d, 20, 20, "Zmywarka ESP (480×320 · 1:1)", r1.font(14, True), 0xF8FAFC)
    r1.draw_text_left(d, 232, 20, step_label, r1.font(13, False), 0x38BDF8)
    r1.draw_round_box(frame, (18, 40), (484, 324), 16, fill=0x1E293B, border_color=0x334155, border_w=2)
    frame.alpha_composite(inner_screen, (20, 42))
    return frame


def draw_touch_finger(img: Image.Image, x: float, y: float, trail_from=None, col=0x38BDF8, scale=1.0):
    ov = Image.new("RGBA", img.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    if trail_from is not None:
        fx, fy = trail_from
        steps = 8
        for s in range(steps):
            u = s / steps
            px = fx + (x - fx) * u
            py = fy + (y - fy) * u
            r = (4 + 6 * u) * scale
            a = int(30 + 95 * u)
            od.ellipse([px - r, py - r, px + r, py + r], fill=r1.hex_rgba(col, a))
    r_out = 16 * scale
    r_mid = 11 * scale
    r_in = 5 * scale
    od.ellipse([x - r_out, y - r_out, x + r_out, y + r_out], fill=r1.hex_rgba(col, 85), outline=(255, 255, 255, 190), width=2)
    od.ellipse([x - r_mid, y - r_mid, x + r_mid, y + r_mid], fill=(255, 255, 255, 110))
    od.ellipse([x - r_in, y - r_in, x + r_in, y + r_in], fill=(255, 255, 255, 235))
    img.alpha_composite(ov)


def render_idle_anim(
    p,
    icons,
    dot_alpha=255,
    fill_frac=0.0,
    supply_txt="Dosyp sól!",
    supply_col=None,
    eco=False,
    weather_icon="ic_w_sun",
    weather_temp="7°",
):
    img = r1.render_idle(
        p,
        icons,
        clock_txt="14:32",
        supply_txt="",
        door_closed=True,
        selected_top=True,
        prog_top_eco=eco,
        weather_icon=weather_icon,
        weather_temp=weather_temp,
    )
    if supply_txt:
        d = ImageDraw.Draw(img)
        r1.draw_text_left(d, 194, 38, supply_txt, r1.font(15, True), supply_col or p["amber"])

    if dot_alpha < 255:
        r1.draw_round_box(img, (24, 31), (14, 14), 7, fill=p["tile"])
        r1.draw_round_box(img, (24, 31), (14, 14), 7, fill=r1.hex_rgba(p["mint"], dot_alpha))

    if fill_frac > 0.0:
        w = int(round(264 * fill_frac))
        if w > 0:
            fill_layer = Image.new("RGBA", (264, 234), (0, 0, 0, 0))
            ImageDraw.Draw(fill_layer).rectangle([0, 0, w, 234], fill=r1.hex_rgba(p["mint"], 153))
            mask = Image.new("L", (264, 234), 0)
            ImageDraw.Draw(mask).rounded_rectangle([0, 0, 263, 233], radius=22, fill=255)
            fill_layer.putalpha(Image.composite(fill_layer.split()[3], Image.new("L", (264, 234), 0), mask))
            img.alpha_composite(fill_layer, (10, 76))
            r1.paste_icon(img, icons["ic_play"], (142, 153), 0xFFFFFF)
            d = ImageDraw.Draw(img)
            r1.draw_text_centered(d, 142, 201, "START", r1.font(29, True), 0xFFFFFF)
            r1.draw_text_centered(d, 142, 279, "ECO 50°C" if eco else "1 GODZINA", r1.font(15, True), p["start_sub"])
    return img


def render_delay_anim(base_p, icons, highlight_idx=-1):
    base = r1.render_idle(base_p, icons, clock_txt="14:32", supply_txt="")
    dim = Image.new("RGBA", (480, 320), r1.hex_rgba(base_p["bg"], 230))
    base.alpha_composite(dim)

    r1.draw_round_box(base, (23, 38), (434, 244), 20, fill=base_p["tile"], border_color=base_p["accent"], border_w=2)
    opts = ["+30m", "+1h", "+2h", "+3h"]
    for i, _ in enumerate(opts):
        bx = 35 + i * 104
        is_hl = (i == highlight_idx)
        r1.draw_round_box(
            base,
            (bx, 122),
            (94, 74),
            14,
            fill=base_p["sel_bg"] if is_hl else base_p["sleep_bg"],
            border_color=base_p["accent"] if is_hl else base_p["border_light"],
            border_w=2 if is_hl else 1,
        )
    r1.draw_round_box(base, (130, 222), (220, 48), 14, fill=base_p["sleep_bg"])

    d = ImageDraw.Draw(base)
    r1.draw_text_centered(d, 240, 62, "Zaplanuj start", r1.font(21, True), base_p["text"])
    r1.draw_text_centered(d, 240, 92, "Program: 1 GODZINA", r1.font(15, True), base_p["text_sub"])
    for i, lbl in enumerate(opts):
        r1.draw_text_centered(d, 35 + i * 104 + 47, 159, lbl, r1.font(21, True), base_p["text"])
    r1.draw_text_centered(d, 240, 246, "Wróć", r1.font(16, True), base_p["text"])
    return base


def render_theme_anim(dlg_p, preview_p, preview_idx, total_themes, bri_label, icons, press_next=False, press_bri=False):
    base = r1.render_idle(dlg_p, icons, clock_txt="14:32", supply_txt="")
    dim = Image.new("RGBA", (480, 320), r1.hex_rgba(dlg_p["bg"], 230))
    base.alpha_composite(dim)

    r1.draw_round_box(base, (16, 17), (448, 286), 20, fill=dlg_p["tile"], border_color=dlg_p["accent"], border_w=2)
    r1.draw_round_box(
        base,
        (316, 29),
        (134, 34),
        10,
        fill=dlg_p["sel_bg"] if press_bri else dlg_p["sleep_bg"],
        border_color=dlg_p["accent"] if press_bri else None,
        border_w=1 if press_bri else 0,
    )
    r1.draw_round_box(base, (30, 92), (56, 124), 14, fill=dlg_p["sleep_bg"])
    r1.draw_round_box(
        base,
        (394, 92),
        (56, 124),
        14,
        fill=dlg_p["sel_bg"] if press_next else dlg_p["sleep_bg"],
        border_color=dlg_p["accent"] if press_next else None,
        border_w=1 if press_next else 0,
    )

    r1.draw_round_box(base, (95, 82), (290, 144), 14, fill=preview_p["bg"], border_color=preview_p["border"], border_w=2)
    r1.draw_round_box(base, (105, 90), (270, 22), 8, fill=preview_p["tile"], border_color=preview_p["border"], border_w=1)
    r1.draw_round_box(base, (111, 97), (8, 8), 4, fill=preview_p["mint"])
    r1.draw_round_box(base, (105, 124), (144, 92), 12, grad=(preview_p["start_top"], preview_p["start_bot"]), border_color=preview_p["start_border"], border_w=2)
    r1.draw_round_box(base, (255, 118), (118, 42), 10, fill=preview_p["sel_bg"], border_color=preview_p["sel_border"], border_w=2)
    r1.draw_lv_arc(base, (314, 188), 44, 6, 0, 359.9, preview_p["sleep_bg"], 255, rounded=False)
    r1.draw_lv_arc_sweep(base, (314, 188), 44, 6, 240, preview_p)

    r1.draw_round_box(base, (30, 243), (126, 48), 14, fill=dlg_p["sleep_bg"])
    r1.draw_round_box(base, (169, 243), (126, 48), 14, fill=dlg_p["sleep_bg"])
    r1.draw_round_box(base, (298, 243), (152, 48), 14, grad=(dlg_p["start_top"], dlg_p["start_bot"]), border_color=dlg_p["start_border"], border_w=2)

    d = ImageDraw.Draw(base)
    r1.draw_text_left(d, 32, 46, f"{preview_idx + 1}/{total_themes} · {preview_p['name']}", r1.font(14, True), dlg_p["text"])
    r1.draw_text_centered(d, 383, 46, bri_label, r1.font(14, True), dlg_p["accent"])
    r1.draw_text_centered(d, 58, 154, "<", r1.font(26, True), dlg_p["text"])
    r1.draw_text_centered(d, 422, 154, ">", r1.font(26, True), dlg_p["text"])
    r1.draw_text_centered(d, 177, 170, "START", r1.font(20, True), 0xFFFFFF)
    r1.draw_text_centered(d, 93, 267, "Wi-Fi OTA", r1.font(15, True), dlg_p["accent"])
    r1.draw_text_centered(d, 232, 267, "ANULUJ", r1.font(15, True), dlg_p["text"])
    r1.draw_text_centered(d, 374, 267, "ZAPISZ", r1.font(20, True), 0xFFFFFF)
    return base


def render_work_anim(p, icons, pct=70, orbit_deg=0.0, dot_alpha=255, peek=False, mins_left=18):
    img = Image.new("RGBA", (480, 320), r1.hex_rgba(p["bg"], 255))
    r1.draw_round_box(img, (10, 10), (460, 300), 20, fill=p["tile"], border_color=p["border"], border_w=1)

    r1.draw_round_box(img, (24, 28), (10, 10), 5, fill=r1.hex_rgba(p["accent"], dot_alpha))
    r1.draw_round_box(img, (364, 20), (96, 34), 12, fill=p["sleep_bg"], border_color=p["border_light"], border_w=1)
    r1.paste_icon(img, icons["ic_moon"], (386, 37), p["text_sub"])

    sweep = 360.0 * pct / 100.0
    r1.draw_lv_arc(img, (240, 160), 256, 20, 0, 359.9, p["sleep_bg"], 255, rounded=False)
    r1.draw_lv_arc_sweep(img, (240, 160), 256, 20, sweep, p)
    r1.draw_lv_arc(img, (240, 160), 268, 3, orbit_deg, 44.0, p["accent_orbit"], 190, rounded=True)

    r1.draw_round_box(img, (328, 256), (132, 44), 14, grad=(p["red"], p["red_bot"]), border_color=p["red_border"], border_w=2)

    d = ImageDraw.Draw(img)
    r1.draw_text_left(d, 40, 33, "1 GODZINA", r1.font(15, True), p["text_sub"])
    r1.draw_text_left(d, 402, 37, "Uśpij", r1.font(15, True), p["text"])
    r1.draw_text_centered(d, 394, 278, "ZATRZYMAJ", r1.font(17, True), 0xFFFFFF)

    if peek:
        r1.draw_text_centered(d, 240, 112, "Koniec o", r1.font(16, True), p["text_sub"])
        r1.draw_text_centered(d, 240, 156, "14:50", r1.font(50, True), p["accent"])
        r1.draw_text_centered(d, 240, 204, "Mycie główne", r1.font(16, True), p["amber"])
    else:
        r1.draw_text_centered(d, 240, 144, str(mins_left), r1.font(88, True), p["text"])
        r1.draw_text_centered(d, 240, 202, "minut", r1.font(20, True), p["accent"])
        r1.draw_text_centered(d, 240, 228, "55°C", r1.font(15, True), p["amber"])
    return img


def build_gif():
    icons = r1.parse_icons(r1.ICONS_C)
    palettes = r1.parse_palettes(r1.THEME_C)
    p = palettes[0]

    frames = []
    durations = []

    def add(screen_img, label, dur_ms):
        frames.append(make_bezel(screen_img, label))
        durations.append(dur_ms)

    # 1. Idle + swipe program (1 GODZINA <-> ECO 50°C)
    for i in range(5):
        add(render_idle_anim(p, icons, dot_alpha=int(180 + 75 * math.sin(i * 0.6))), "1/7 · Gotowość + pogoda (Wrocław)", 110)

    for k in range(5):
        t = k / 4.0
        fy = 166 - 68 * t
        fr = render_idle_anim(p, icons, eco=(k >= 3))
        draw_touch_finger(fr, 377, fy, trail_from=(377, 170), col=p["accent"])
        add(fr, "1/7 · Przesunięcie ↕ na kafelku: ECO 50°C", 85)

    for _ in range(5):
        add(render_idle_anim(p, icons, eco=True), "1/7 · Wybrano ECO 50°C", 110)

    for k in range(4):
        t = k / 3.0
        fy = 98 + 68 * t
        fr = render_idle_anim(p, icons, eco=(k < 2))
        draw_touch_finger(fr, 377, fy, trail_from=(377, 94), col=p["accent"])
        add(fr, "1/7 · Przesunięcie ↕: powrót do 1 GODZINA", 85)

    # 2. Tap weather next to clock -> 7-day forecast -> tap Dziś -> Day details + hourly scroll
    fr_wtap = render_idle_anim(p, icons)
    draw_touch_finger(fr_wtap, 146, 38, col=p["amber"])
    add(fr_wtap, "2/7 · Klik w pogodę: prognoza na tydzień", 220)

    w_screen = r1.render_weather_overlay(p, icons, city="Wrocław")
    for _ in range(7):
        add(w_screen, "2/7 · Prognoza 7-dniowa (Wrocław)", 140)

    fr_dtap = r1.render_weather_overlay(p, icons, city="Wrocław")
    draw_touch_finger(fr_dtap, 53, 190, col=p["accent"])
    add(fr_dtap, "2/7 · Klik w dzień: szczegóły i godziny", 220)

    for _ in range(5):
        add(r1.render_weather_day_overlay(p, icons, scroll_x=0), "2/7 · Szczegóły dnia + 24h (Wrocław)", 130)
    for k in range(6):
        sx = int(round(205 * (k / 5.0)))
        fr_hscroll = r1.render_weather_day_overlay(p, icons, scroll_x=sx)
        fx = 310 - int(round(190 * (k / 5.0)))
        draw_touch_finger(fr_hscroll, fx, 218, trail_from=(325, 218), col=p["accent"])
        add(fr_hscroll, "2/7 · Przewijanie prognozy godzinowej", 95)
    for _ in range(5):
        add(r1.render_weather_day_overlay(p, icons, scroll_x=205), "2/7 · Nocne godziny (ikony księżyca)", 130)

    fr_wclose = r1.render_weather_day_overlay(p, icons, scroll_x=205)
    draw_touch_finger(fr_wclose, 412, 49, col=p["accent"])
    add(fr_wclose, "2/7 · Zamknij prognozę", 200)

    # 3. Hold clock -> Theme Switcher & Brightness / OTA Settings
    for k in range(3):
        fr = render_idle_anim(p, icons)
        draw_touch_finger(fr, 74, 38, col=p["accent"], scale=1.0 + 0.12 * k)
        add(fr, "3/7 · Przytrzymaj zegar: Motywy, jasność i OTA", 110)

    th_steps = [
        (0, "Jasność: Auto", False, False, 480),
        (0, "Jasność: Auto", True, False, 150),
        (1, "Jasność: Auto", False, False, 550),
        (1, "Jasność: Auto", True, False, 150),
        (4, "Jasność: Auto", False, False, 550),
        (4, "Jasność: 100%", False, True, 220),
        (4, "Jasność: 100%", False, False, 450),
    ]
    for tidx, bri, pn, pb, ms in th_steps:
        add(render_theme_anim(p, palettes[tidx], tidx, len(palettes), bri, icons, pn, pb), "3/7 · Motywy, jasność i miasto w OTA", ms)

    # 4. Swipe DOWN on START -> Delayed start picker + active countdown
    for k in range(6):
        t = k / 5.0
        fy = 122 + 125 * t
        fr = render_idle_anim(p, icons, supply_txt="")
        draw_touch_finger(fr, 142, fy, trail_from=(142, 118), col=0xFFFFFF)
        add(fr, "4/7 · Przesuń START w dół: Timer", 85)

    add(render_delay_anim(p, icons, highlight_idx=-1), "4/7 · Wybór opóźnionego startu", 550)
    fr_tap = render_delay_anim(p, icons, highlight_idx=1)
    draw_touch_finger(fr_tap, 186, 159, col=p["accent"])
    add(fr_tap, "4/7 · Wybór opóźnienia: +1h", 380)
    for _ in range(6):
        add(render_idle_anim(p, icons, supply_txt="Start za 1h 00m", supply_col=p["accent"]), "4/7 · Odliczanie obok pogody", 125)

    # 5. Hold START fill bar
    for i in range(11):
        fr = render_idle_anim(p, icons, fill_frac=(i + 1) / 11.0)
        draw_touch_finger(fr, 142, 195, col=p["mint"])
        add(fr, "5/7 · Przytrzymanie START (~0,8 s)", 65)

    # 6. Washing (gradient arc + spinning orbit + tap-to-peek)
    total_work = 40
    for i in range(total_work):
        t = i / (total_work - 1)
        pct = 24 + 62 * t
        mins = max(10, int(round(46 - 35 * t)))
        orbit = (i * (360.0 / 32.0)) % 360.0
        dot_a = int(160 + 95 * math.sin(i * 0.4))
        peek = (17 <= i < 29)
        lbl = "6/7 · Podgląd końca (klik tarczy)" if peek else "6/7 · Mycie (orbitująca tarcza)"
        add(render_work_anim(p, icons, pct=pct, orbit_deg=orbit, dot_alpha=dot_a, peek=peek, mins_left=mins), lbl, 70)

    # 7. Finish
    fin_screen = r1.render_finish(p, icons)
    for _ in range(9):
        add(fin_screen, "7/7 · Koniec cyklu", 120)

    pal_frames = [
        f.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
        for f in frames
    ]
    pal_frames[0].save(
        OUT_GIF,
        save_all=True,
        append_images=pal_frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
    )
    print(f"Saved {OUT_GIF} ({len(frames)} frames, {os.path.getsize(OUT_GIF)/1024:.1f} KB)")

    if "--samples" in sys.argv:
        w_screen.save(SCRIPTS_DIR / "sample_weather_7d.png")
        render_theme_anim(p, palettes[4], 4, len(palettes), "Jasność: 100%", icons).save(SCRIPTS_DIR / "sample_theme.png")


if __name__ == "__main__":
    build_gif()
