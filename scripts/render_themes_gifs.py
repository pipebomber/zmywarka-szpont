"""Renderuje rozdzielone podglady motywów (docs/themes_light.gif oraz docs/themes_dark.gif)."""

import math
import os
import sys
from pathlib import Path
from PIL import Image, ImageDraw

SCRIPTS_DIR = Path(__file__).resolve().parent
ROOT = SCRIPTS_DIR.parent
sys.path.insert(0, str(SCRIPTS_DIR))
import render_ui_1to1 as r1
import render_ui_gif as rg

OUT_LIGHT_GIF = str(ROOT / "docs" / "themes_light.gif")
OUT_DARK_GIF = str(ROOT / "docs" / "themes_dark.gif")


def render_theme_group_frame(title_main, subtitle, theme_entries, pw, ph, dot_alpha, fill_f, orbit_deg):
    pad_x, pad_y = 20, 16
    row_gap = 14
    inner_gap = 10
    card_pad = 10
    header_h = 56
    title_h = 26

    cw = pw * 2 + inner_gap + card_pad * 2
    ch = ph + title_h + card_pad * 2
    n = len(theme_entries)
    W = pad_x * 2 + cw
    H = header_h + pad_y + ch * n + row_gap * max(0, n - 1) + pad_y

    sheet = Image.new("RGBA", (W, H), (11, 15, 25, 255))
    d = ImageDraw.Draw(sheet)
    r1.draw_text_left(d, pad_x, 21, title_main, r1.font(19, True), 0xF8FAFC)
    r1.draw_text_left(d, pad_x, 42, subtitle, r1.font(12, False), 0x94A3B8)

    bezel_mask = Image.new("L", (pw, ph), 0)
    ImageDraw.Draw(bezel_mask).rounded_rectangle([0, 0, pw - 1, ph - 1], radius=10, fill=255)

    w_map = [
        ("ic_w_moon", "-1°"),
        ("ic_w_sun", "9°"),
        ("ic_w_partly_day", "6°"),
        ("ic_w_rain", "4°"),
        ("ic_w_partly_night", "2°"),
        ("ic_w_storm", "11°"),
        ("ic_w_snow", "-3°"),
        ("ic_w_fog", "1°"),
        ("ic_w_moon", "0°"),
    ]

    icons = render_theme_group_frame.icons
    for r_i, (orig_idx, p) in enumerate(theme_entries):
        cx = pad_x
        cy = header_h + pad_y + r_i * (ch + row_gap)
        r1.draw_round_box(sheet, (cx, cy), (cw, ch), 16, fill=0x111827, border_color=0x263248, border_w=1)
        d = ImageDraw.Draw(sheet)
        r1.draw_text_left(d, cx + card_pad + 2, cy + card_pad + 10, f"{orig_idx + 1}/9 · {p['name']}", r1.font(14, True), 0xF8FAFC)
        mode_tag = "JASNY" if p["is_light"] else "CIEMNY"
        mode_col = 0xFBBF24 if p["is_light"] else 0x38BDF8
        r1.draw_text_centered(d, cx + cw - card_pad - 26, cy + card_pad + 10, mode_tag, r1.font(11, True), mode_col)

        w_ic, w_tp = w_map[orig_idx % len(w_map)]
        idle_thumb = rg.render_idle_anim(
            p, icons, dot_alpha=dot_alpha, fill_frac=fill_f, supply_txt="",
            weather_icon=w_ic, weather_temp=w_tp,
        ).resize((pw, ph), Image.Resampling.LANCZOS)
        idle_thumb.putalpha(bezel_mask)
        sheet.alpha_composite(idle_thumb, (cx + card_pad, cy + card_pad + title_h))

        wash_thumb = rg.render_work_anim(
            p, icons, pct=72, orbit_deg=orbit_deg, dot_alpha=dot_alpha, peek=False, mins_left=18,
        ).resize((pw, ph), Image.Resampling.LANCZOS)
        wash_thumb.putalpha(bezel_mask)
        sheet.alpha_composite(wash_thumb, (cx + card_pad + pw + inner_gap, cy + card_pad + title_h))

    return sheet


def build_group_gif(out_path, title_main, subtitle, theme_entries, pw, ph, n_frames=24):
    frames = []
    durations = []
    for f in range(n_frames):
        dot_a = int(155 + 100 * math.sin(2.0 * math.pi * f / n_frames))
        orbit = (f * (360.0 / n_frames)) % 360.0
        if f < 4:
            fill_f = 0.0
        elif f <= 13:
            fill_f = (f - 3) / 10.0
        elif f <= 15:
            fill_f = 1.0
        else:
            fill_f = 0.0

        grid = render_theme_group_frame(title_main, subtitle, theme_entries, pw, ph, dot_a, fill_f, orbit)
        frames.append(grid.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG))
        durations.append(90)

    frames[0].save(
        out_path,
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
    )
    print(f"Saved {out_path} ({len(frames)} frames, {os.path.getsize(out_path)/1024:.1f} KB)")


def main():
    render_theme_group_frame.icons = r1.parse_icons(r1.ICONS_C)
    palettes = r1.parse_palettes(r1.THEME_C)

    light_entries = [(i, p) for i, p in enumerate(palettes) if p["is_light"]]
    dark_entries = [(i, p) for i, p in enumerate(palettes) if not p["is_light"]]

    build_group_gif(
        OUT_LIGHT_GIF,
        "Motywy jasne (3 palety) — Ekran główny + Mycie",
        "Każdy motyw z podglądem na żywo (animacja START, pogoda oraz gradient na łuku)",
        light_entries,
        pw=312,
        ph=208,
        n_frames=24,
    )
    build_group_gif(
        OUT_DARK_GIF,
        "Motywy ciemne (6 palet) — Ekran główny + Mycie",
        "Każdy motyw z podglądem na żywo (animacja START, pogoda nocna i gradient na łuku)",
        dark_entries,
        pw=312,
        ph=208,
        n_frames=24,
    )


if __name__ == "__main__":
    main()
