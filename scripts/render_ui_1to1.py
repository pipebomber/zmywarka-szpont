"""Renderuje statyczny podglad ekranow 1:1 (docs/ui_showcase.png) z definicji C."""

import math
import os
import re
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ICONS_C = str(ROOT / "src" / "ui" / "ui_icons.c")
THEME_C = str(ROOT / "src" / "ui" / "ui_theme.c")
OUT_SHOWCASE = str(ROOT / "docs" / "ui_showcase.png")

FONT_REG_CANDIDATES = [
    r"C:\Windows\Fonts\segoeui.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]
FONT_BOLD_CANDIDATES = [
    r"C:\Windows\Fonts\segoeuib.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]


def _pick_font(cands):
    for p in cands:
        if os.path.exists(p):
            return p
    return cands[0]


FONT_REG = _pick_font(FONT_REG_CANDIDATES)
FONT_BOLD = _pick_font(FONT_BOLD_CANDIDATES)


def hex_rgb(val: int):
    return ((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF)


def hex_rgba(val: int, alpha: int = 255):
    return ((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF, alpha)


def parse_icons(path: str):
    txt = open(path, "r", encoding="utf-8").read()
    arrays = {}
    for m in re.finditer(r"static const .*? uint8_t (\w+_map)\[\]\s*=\s*\{(.*?)\};", txt, re.S):
        name = m.group(1)
        bytes_list = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{2}", m.group(2))]
        arrays[name] = bytes_list

    icons = {}
    for m in re.finditer(
        r"const lv_img_dsc_t (\w+)\s*=\s*\{.*?\.header\.w\s*=\s*(\d+),\s*\.header\.h\s*=\s*(\d+),.*?\.data\s*=\s*(\w+_map)",
        txt,
        re.S,
    ):
        icon_name = m.group(1)
        w = int(m.group(2))
        h = int(m.group(3))
        arr = arrays[m.group(4)]
        img = Image.new("L", (w, h), 0)
        img.putdata(arr[: w * h])
        icons[icon_name] = img
    return icons


def parse_palettes(path: str):
    txt = open(path, "r", encoding="utf-8").read()
    arr_block = re.search(r"static const ui_palette_t s_palettes\[\]\s*=\s*\{(.*?)\n\};", txt, re.S).group(1)
    palettes = []
    for block in re.finditer(r"\{\s*(\.name\s*=.*?)\n\s*\},", arr_block, re.S):
        body = block.group(1)
        p = {}
        p["name"] = re.search(r'\.name\s*=\s*"([^"]+)"', body).group(1)
        p["is_light"] = re.search(r"\.is_light\s*=\s*(true|false)", body).group(1) == "true"
        for fm in re.finditer(r"\.(\w+)\s*=\s*0x([0-9a-fA-F]{6})", body):
            p[fm.group(1)] = int(fm.group(2), 16)
        palettes.append(p)
    return palettes


def font(size: int, bold: bool = False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT_REG, size)


def draw_round_box(canvas: Image.Image, xy, size, radius, fill=None, grad=None, border_color=None, border_w=0, ss=2):
    x, y = xy
    w, h = size
    W, H = w * ss, h * ss
    R = min(radius * ss, W // 2, H // 2)
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W - 1, H - 1], radius=R, fill=255)

    fill_img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if grad is not None:
        c_top, c_bot = grad
        r1, g1, b1 = hex_rgb(c_top)
        r2, g2, b2 = hex_rgb(c_bot)
        fd = ImageDraw.Draw(fill_img)
        for row in range(H):
            t = row / max(1, H - 1)
            rc = int(r1 + (r2 - r1) * t)
            gc = int(g1 + (g2 - g1) * t)
            bc = int(b1 + (b2 - b1) * t)
            fd.line([(0, row), (W - 1, row)], fill=(rc, gc, bc, 255))
    elif fill is not None:
        rgba = fill if isinstance(fill, tuple) else hex_rgba(fill, 255)
        fill_img.paste(rgba, [0, 0, W, H])

    fill_img.putalpha(Image.composite(fill_img.split()[3], Image.new("L", (W, H), 0), mask))

    if border_color is not None and border_w > 0:
        bc_rgba = border_color if isinstance(border_color, tuple) else hex_rgba(border_color, 255)
        ImageDraw.Draw(fill_img).rounded_rectangle([0, 0, W - 1, H - 1], radius=R, outline=bc_rgba, width=border_w * ss)

    final = fill_img.resize((w, h), Image.Resampling.LANCZOS)
    canvas.alpha_composite(final, (int(x), int(y)))


def paste_icon(canvas: Image.Image, mask_img: Image.Image, center_xy, color_hex, alpha=255):
    w, h = mask_img.size
    rgba = Image.new("RGBA", (w, h), hex_rgba(color_hex, alpha))
    scaled_mask = mask_img.point(lambda a: int(a * alpha / 255)) if alpha < 255 else mask_img
    rgba.putalpha(scaled_mask)
    x = int(round(center_xy[0] - w / 2))
    y = int(round(center_xy[1] - h / 2))
    canvas.alpha_composite(rgba, (x, y))


def draw_text_centered(draw: ImageDraw.ImageDraw, cx, cy, text, fnt, color_hex, alpha=255):
    bbox = draw.textbbox((0, 0), text, font=fnt)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    x = cx - tw / 2 - bbox[0]
    y = cy - th / 2 - bbox[1]
    draw.text((x, y), text, font=fnt, fill=hex_rgba(color_hex, alpha))


def draw_text_left(draw: ImageDraw.ImageDraw, x, cy, text, fnt, color_hex, alpha=255):
    bbox = draw.textbbox((0, 0), text, font=fnt)
    th = bbox[3] - bbox[1]
    y = cy - th / 2 - bbox[1]
    draw.text((x - bbox[0], y), text, font=fnt, fill=hex_rgba(color_hex, alpha))


def draw_lv_arc(canvas: Image.Image, center_xy, outer_d, stroke_w, start_deg, sweep_deg, color_hex, alpha=255, rounded=True, ss=4):
    cx, cy = center_xy
    D = outer_d * ss
    SW = stroke_w * ss
    pad = 4
    box_d = D + pad * 2
    box = Image.new("RGBA", (box_d, box_d), (0, 0, 0, 0))
    d = ImageDraw.Draw(box)
    a0 = -90.0 + start_deg
    a1 = a0 + sweep_deg
    rgba = hex_rgba(color_hex, alpha)
    d.arc([pad, pad, pad + D, pad + D], start=a0, end=a1, fill=rgba, width=SW)

    if rounded and sweep_deg < 359.5:
        r_mid = (D / 2.0) - (SW / 2.0)
        mc = box_d / 2.0
        cap_r = SW / 2.0
        for ang in (a0, a1):
            rad = math.radians(ang)
            kx = mc + r_mid * math.cos(rad)
            ky = mc + r_mid * math.sin(rad)
            d.ellipse([kx - cap_r, ky - cap_r, kx + cap_r, ky + cap_r], fill=rgba)

    out_d = outer_d + 2
    resized = box.resize((out_d, out_d), Image.Resampling.LANCZOS)
    canvas.alpha_composite(resized, (int(round(cx - out_d / 2)), int(round(cy - out_d / 2))))


def lerp_hex(c0: int, c1: int, t: float) -> int:
    t = max(0.0, min(1.0, t))
    r0, g0, b0 = (c0 >> 16) & 0xFF, (c0 >> 8) & 0xFF, c0 & 0xFF
    r1, g1, b1 = (c1 >> 16) & 0xFF, (c1 >> 8) & 0xFF, c1 & 0xFF
    return (int(round(r0 + (r1 - r0) * t)) << 16) | (int(round(g0 + (g1 - g0) * t)) << 8) | int(round(b0 + (b1 - b0) * t))


_GRAD_CACHE = {}


def draw_lv_arc_sweep(canvas: Image.Image, center_xy, outer_d, stroke_w, sweep_deg, p, ss=4):
    cx, cy = center_xy
    D = outer_d * ss
    SW = stroke_w * ss
    pad = 4
    box_d = D + pad * 2
    d_mask = Image.new("L", (box_d, box_d), 0)
    md = ImageDraw.Draw(d_mask)
    a0, a1 = -90.0, -90.0 + sweep_deg
    md.arc([pad, pad, pad + D, pad + D], start=a0, end=a1, fill=255, width=SW)
    r_mid = (D / 2.0) - (SW / 2.0)
    mc = box_d / 2.0
    cap_r = SW / 2.0
    for ang in (a0, a1):
        rad = math.radians(ang)
        kx, ky = mc + r_mid * math.cos(rad), mc + r_mid * math.sin(rad)
        md.ellipse([kx - cap_r, ky - cap_r, kx + cap_r, ky + cap_r], fill=255)

    c0 = lerp_hex(p["start_bot"], p["start_top"], 0.35)
    c1 = p["accent_orbit"] if p["accent"] in (p["start_bot"], p["start_top"]) else p["accent"]
    c2 = p["mint"]

    cache_key = (outer_d, c0, c1, c2)
    grad_img = _GRAD_CACHE.get(cache_key)
    if grad_img is None:
        grad_img = Image.new("RGBA", (outer_d, outer_d), (0, 0, 0, 0))
        px = grad_img.load()
        two_pi = 2.0 * math.pi
        for y in range(outer_d):
            dy = y - (outer_d / 2.0 - 0.5)
            for x in range(outer_d):
                dx = x - (outer_d / 2.0 - 0.5)
                ang = math.atan2(dx, -dy)
                if ang < 0:
                    ang += two_pi
                u = ang / two_pi
                if u >= 0.985:
                    col = c0
                elif u < 0.45:
                    col = lerp_hex(c0, c1, u / 0.45)
                else:
                    col = lerp_hex(c1, c2, (u - 0.45) / 0.45)
                px[x, y] = hex_rgba(col, 255)
        _GRAD_CACHE[cache_key] = grad_img

    out_d = outer_d + 2
    full = Image.new("RGBA", (out_d, out_d), (0, 0, 0, 0))
    full.paste(grad_img, (1, 1))
    alpha = d_mask.resize((out_d, out_d), Image.Resampling.LANCZOS)
    full.putalpha(alpha)
    canvas.alpha_composite(full, (int(round(cx - out_d / 2)), int(round(cy - out_d / 2))))


def weather_color(p, w_icon):
    if w_icon in ("ic_w_sun", "ic_w_partly_day", "ic_w_storm"):
        return 0xF59E0B
    if w_icon == "ic_w_snow":
        return 0x0284C7 if p["is_light"] else 0xBAE6FD
    if w_icon in ("ic_w_moon", "ic_w_partly_night", "ic_w_rain"):
        return 0x0284C7 if p["is_light"] else 0x38BDF8
    return p["text_sub"]


def render_idle(
    p,
    icons,
    clock_txt="14:32",
    supply_txt="Dosyp sól!",
    door_closed=True,
    selected_top=True,
    extra_dry=False,
    prog_top_eco=False,
    prog_bot_int=False,
    weather_icon="ic_w_sun",
    weather_temp="7°",
):
    img = Image.new("RGBA", (480, 320), hex_rgba(p["bg"], 255))

    draw_round_box(img, (10, 10), (460, 56), 18, fill=p["tile"], border_color=p["border"], border_w=1)
    dot_col = p["mint"] if door_closed else p["red"]
    draw_round_box(img, (24, 31), (14, 14), 7, fill=dot_col)

    if weather_icon and weather_icon in icons and weather_temp:
        paste_icon(img, icons[weather_icon], (127, 38), weather_color(p, weather_icon))

    # compact tile_sleep (96x38 @ 362, 19)
    draw_round_box(img, (362, 19), (96, 38), 12, fill=p["sleep_bg"], border_color=p["border_light"], border_w=1)
    paste_icon(img, icons["ic_moon"], (384, 38), p["text_sub"])

    d = ImageDraw.Draw(img)
    draw_text_left(d, 46, 38, clock_txt, font(21, True), p["text"])
    if weather_icon and weather_temp:
        draw_text_left(d, 142, 38, weather_temp, font(21, True), p["text"])
    if supply_txt:
        draw_text_left(d, 194, 38, supply_txt, font(15, True), p["amber"])
    draw_text_left(d, 400, 38, "Uśpij", font(15, True), p["text"])

    if door_closed:
        draw_round_box(img, (10, 76), (264, 234), 22, grad=(p["start_top"], p["start_bot"]), border_color=p["start_border"], border_w=2)
        paste_icon(img, icons["ic_play"], (142, 153), 0xFFFFFF)
        d = ImageDraw.Draw(img)
        draw_text_centered(d, 142, 201, "START", font(29, True), 0xFFFFFF)
        if selected_top:
            sub = "ECO 50°C" if prog_top_eco else ("1 GODZINA + SUSZ." if extra_dry else "1 GODZINA")
        else:
            sub = "INTENSYWNY" if prog_bot_int else "30 MINUT"
        draw_text_centered(d, 142, 279, sub, font(15, True), p["start_sub"])
    else:
        draw_round_box(img, (10, 76), (264, 234), 22, grad=(p["amber_top"], p["amber_bot"]), border_color=p["amber"], border_w=2)
        paste_icon(img, icons["ic_warn"], (142, 139), 0xFFFFFF)
        d = ImageDraw.Draw(img)
        draw_text_centered(d, 142, 185, "ZAMKNIJ", font(29, True), 0xFFFFFF)
        draw_text_centered(d, 142, 219, "DRZWICZKI", font(20, True), 0xF8FAFC)
        draw_text_centered(d, 142, 279, "Drzwi są otwarte", font(15, True), 0xFEF3C7)

    top_sel = selected_top
    draw_round_box(
        img,
        (284, 76),
        (186, 112),
        20,
        fill=p["sel_bg"] if top_sel else p["tile"],
        border_color=p["sel_border"] if top_sel else p["border"],
        border_w=3 if top_sel else 1,
    )
    draw_round_box(img, (296, 88), (38, 38), 12, fill=p["sel_icon_bg"] if top_sel else p["unsel_icon_bg"])
    paste_icon(img, icons["ic_clock"], (315, 107), 0xFFFFFF if top_sel else p["muted"])
    draw_round_box(
        img,
        (442, 90),
        (16, 16),
        8,
        fill=p["sel_dot"] if top_sel else p["tile"],
        border_color=(p["sel_border"] if p["is_light"] else 0xFFFFFF) if top_sel else p["border_light"],
        border_w=2,
    )
    d = ImageDraw.Draw(img)
    top_title = "ECO 50°C" if prog_top_eco else ("1H + SUSZ." if extra_dry else "1 GODZINA")
    draw_text_left(d, 296, 159, top_title, font(20, True), p["sel_title"] if top_sel else p["unsel_title"])

    bot_sel = not selected_top
    draw_round_box(
        img,
        (284, 198),
        (186, 112),
        20,
        fill=p["sel_bg"] if bot_sel else p["tile"],
        border_color=p["sel_border"] if bot_sel else p["border"],
        border_w=3 if bot_sel else 1,
    )
    draw_round_box(img, (296, 210), (38, 38), 12, fill=p["sel_icon_bg"] if bot_sel else p["unsel_icon_bg"])
    paste_icon(img, icons["ic_bolt"], (315, 229), 0xFFFFFF if bot_sel else p["muted"])
    draw_round_box(
        img,
        (442, 212),
        (16, 16),
        8,
        fill=p["sel_dot"] if bot_sel else p["tile"],
        border_color=(p["sel_border"] if p["is_light"] else 0xFFFFFF) if bot_sel else p["border_light"],
        border_w=2,
    )
    d = ImageDraw.Draw(img)
    bot_title = "INTENSYWNY" if prog_bot_int else "30 MINUT"
    draw_text_left(d, 296, 281, bot_title, font(20, True), p["sel_title"] if bot_sel else p["unsel_title"])
    return img


def render_work(p, icons, peek=False):
    img = Image.new("RGBA", (480, 320), hex_rgba(p["bg"], 255))
    draw_round_box(img, (10, 10), (460, 300), 20, fill=p["tile"], border_color=p["border"], border_w=1)

    draw_round_box(img, (24, 28), (10, 10), 5, fill=p["accent"])
    draw_round_box(img, (364, 20), (96, 34), 12, fill=p["sleep_bg"], border_color=p["border_light"], border_w=1)
    paste_icon(img, icons["ic_moon"], (386, 37), p["text_sub"])

    pct = 70
    sweep = 360.0 * pct / 100.0
    draw_lv_arc(img, (240, 160), 256, 20, 0, 359.9, p["sleep_bg"], 255, rounded=False)
    draw_lv_arc_sweep(img, (240, 160), 256, 20, sweep, p)
    draw_lv_arc(img, (240, 160), 268, 3, sweep - 22, 44, p["accent_orbit"], 180, rounded=True)

    draw_round_box(img, (328, 256), (132, 44), 14, grad=(p["red"], p["red_bot"]), border_color=p["red_border"], border_w=2)

    d = ImageDraw.Draw(img)
    draw_text_left(d, 40, 33, "1 GODZINA", font(15, True), p["text_sub"])
    draw_text_left(d, 402, 37, "Uśpij", font(15, True), p["text"])
    draw_text_centered(d, 394, 278, "ZATRZYMAJ", font(17, True), 0xFFFFFF)

    if peek:
        draw_text_centered(d, 240, 112, "Koniec o", font(16, True), p["text_sub"])
        draw_text_centered(d, 240, 156, "14:50", font(50, True), p["accent"])
        draw_text_centered(d, 240, 204, "Mycie główne", font(16, True), p["amber"])
    else:
        draw_text_centered(d, 240, 144, "18", font(88, True), p["text"])
        draw_text_centered(d, 240, 202, "minut", font(20, True), p["accent"])
        draw_text_centered(d, 240, 228, "55°C", font(15, True), p["amber"])
    return img


def render_finish(p, icons):
    img = Image.new("RGBA", (480, 320), hex_rgba(p["bg"], 255))
    draw_round_box(img, (10, 10), (460, 216), 20, fill=p["mint_dark"], border_color=p["mint"], border_w=2)
    paste_icon(img, icons["ic_check"], (240, 54), p["mint"])

    draw_round_box(img, (10, 236), (286, 74), 22, grad=(p["start_top"], p["start_bot"]), border_color=p["start_border"], border_w=2)
    draw_round_box(img, (306, 236), (164, 74), 20, fill=p["tile"], border_color=p["border"], border_w=1)
    paste_icon(img, icons["ic_moon"], (340, 273), p["text_sub"])

    d = ImageDraw.Draw(img)
    draw_text_centered(d, 240, 118, "ZMYWANIE ZAKOŃCZONE!", font(27, True), 0xFFFFFF)
    draw_text_centered(d, 240, 164, "Skończono o 14:50 · Uchyl drzwi.", font(20, True), 0xA7F3D0)
    draw_text_centered(d, 240, 206, "Przypomnienie: Dosyp sól do zmywarki!", font(15, True), p["amber"])
    draw_text_centered(d, 153, 273, "NOWE ZMYWANIE", font(20, True), 0xFFFFFF)
    draw_text_left(d, 358, 273, "Uśpij ekran", font(15, True), p["text"])
    return img


def render_weather_overlay(p, icons, city="Wrocław", days=None):
    if days is None:
        days = [
            ("Dziś", "ic_w_rain", 4, 1),
            ("Śr",   "ic_w_rain", 5, 1),
            ("Czw",  "ic_w_partly_day", 8, 3),
            ("Pt",   "ic_w_cloud", 4, 1),
            ("Sob",  "ic_w_cloud", 5, 0),
            ("Nd",   "ic_w_sun", 7, -1),
            ("Pon",  "ic_w_partly_day", 8, 1),
        ]
    base = render_idle(p, icons, supply_txt="")
    dim = Image.new("RGBA", (480, 320), hex_rgba(p["bg"], 230))
    base.alpha_composite(dim)

    # w_dlg: 456x270 @ (12, 25), inner x=24, y=37
    draw_round_box(base, (12, 25), (456, 270), 20, fill=p["tile"], border_color=p["accent"], border_w=2)
    draw_round_box(base, (368, 37), (88, 34), 10, fill=p["sleep_bg"], border_color=p["border_light"], border_w=1)

    for i, (dname, ic_name, hi, lo) in enumerate(days):
        cx = 24 + i * 62
        cy = 85
        today = (i == 0)
        draw_round_box(
            base, (cx, cy), (58, 194), 14,
            fill=p["sel_bg"] if today else p["sleep_bg"],
            border_color=p["accent"] if today else p["border_light"],
            border_w=1,
        )
        paste_icon(base, icons[ic_name], (cx + 29, cy + 91), weather_color(p, ic_name))

    d = ImageDraw.Draw(base)
    draw_text_left(d, 24, 54, f"Pogoda · {city}", font(20, True), p["text"])
    draw_text_centered(d, 412, 54, "Zamknij", font(15, True), p["text"])
    for i, (dname, _, hi, lo) in enumerate(days):
        cx = 24 + i * 62
        cy = 85
        today = (i == 0)
        draw_text_centered(d, cx + 29, cy + 22, dname, font(15, True), p["accent"] if today else p["text_sub"])
        draw_text_centered(d, cx + 29, cy + 144, f"{hi}°", font(20, True), p["text"])
        draw_text_centered(d, cx + 29, cy + 172, f"{lo}°", font(15, True), p["text_sub"])
    return base


def render_delay_overlay(p, icons):
    base = render_idle(p, icons, supply_txt="")
    dim = Image.new("RGBA", (480, 320), hex_rgba(p["bg"], 230))
    base.alpha_composite(dim)

    draw_round_box(base, (23, 38), (434, 244), 20, fill=p["tile"], border_color=p["accent"], border_w=2)
    opts = ["+30m", "+1h", "+2h", "+3h"]
    for i, _ in enumerate(opts):
        bx = 35 + i * 104
        draw_round_box(base, (bx, 122), (94, 74), 14, fill=p["sleep_bg"], border_color=p["border_light"], border_w=1)
    draw_round_box(base, (130, 222), (220, 48), 14, fill=p["sleep_bg"])

    d = ImageDraw.Draw(base)
    draw_text_centered(d, 240, 62, "Zaplanuj start", font(21, True), p["text"])
    draw_text_centered(d, 240, 92, "Program: 1 GODZINA", font(15, True), p["text_sub"])
    for i, lbl in enumerate(opts):
        draw_text_centered(d, 35 + i * 104 + 47, 159, lbl, font(21, True), p["text"])
    draw_text_centered(d, 240, 246, "Wróć", font(16, True), p["text"])
    return base


def render_theme_overlay(p, icons):
    base = render_idle(p, icons, supply_txt="")
    dim = Image.new("RGBA", (480, 320), hex_rgba(p["bg"], 230))
    base.alpha_composite(dim)

    draw_round_box(base, (16, 17), (448, 286), 20, fill=p["tile"], border_color=p["accent"], border_w=2)
    draw_round_box(base, (316, 29), (134, 34), 10, fill=p["sleep_bg"])
    draw_round_box(base, (30, 92), (56, 124), 14, fill=p["sleep_bg"])
    draw_round_box(base, (394, 92), (56, 124), 14, fill=p["sleep_bg"])

    draw_round_box(base, (95, 82), (290, 144), 14, fill=p["bg"], border_color=p["border"], border_w=2)
    draw_round_box(base, (105, 90), (270, 22), 8, fill=p["tile"], border_color=p["border"], border_w=1)
    draw_round_box(base, (111, 97), (8, 8), 4, fill=p["mint"])
    draw_round_box(base, (105, 124), (144, 92), 12, grad=(p["start_top"], p["start_bot"]), border_color=p["start_border"], border_w=2)
    draw_round_box(base, (255, 118), (118, 42), 10, fill=p["sel_bg"], border_color=p["sel_border"], border_w=2)
    draw_lv_arc(base, (314, 188), 44, 6, 0, 359.9, p["sleep_bg"], 255, rounded=False)
    draw_lv_arc(base, (314, 188), 44, 6, 0, 360 * 0.65, p["accent"], 255, rounded=True)

    draw_round_box(base, (30, 243), (126, 48), 14, fill=p["sleep_bg"])
    draw_round_box(base, (169, 243), (126, 48), 14, fill=p["sleep_bg"])
    draw_round_box(base, (298, 243), (152, 48), 14, grad=(p["start_top"], p["start_bot"]), border_color=p["start_border"], border_w=2)

    d = ImageDraw.Draw(base)
    draw_text_left(d, 32, 46, "1/9 · Domyślny (Ocean Cobalt)", font(15, True), p["text"])
    draw_text_centered(d, 383, 46, "Jasność: Auto", font(14, True), p["accent"])
    draw_text_centered(d, 58, 154, "<", font(26, True), p["text"])
    draw_text_centered(d, 422, 154, ">", font(26, True), p["text"])
    draw_text_centered(d, 177, 170, "START", font(20, True), 0xFFFFFF)
    draw_text_centered(d, 93, 267, "Wi-Fi OTA", font(15, True), p["accent"])
    draw_text_centered(d, 232, 267, "ANULUJ", font(15, True), p["text"])
    draw_text_centered(d, 374, 267, "ZAPISZ", font(20, True), 0xFFFFFF)
    return base


def build_showcase(palettes, icons):
    p0 = palettes[0]
    panels = [
        ("1. Ekran gotowości + pogoda (Wrocław)", render_idle(p0, icons)),
        ("2. Prognoza na cały tydzień (klik w pogodę)", render_weather_overlay(p0, icons, city="Wrocław")),
        ("3. Mycie w toku (pierścień postępu + 55°C)", render_work(p0, icons, peek=False)),
        ("4. Podgląd końca mycia (klik w środek tarczy)", render_work(p0, icons, peek=True)),
        ("5. Opóźniony start (przesuń START w dół)", render_delay_overlay(p0, icons)),
        ("6. Koniec zmywania + komunikat o soli", render_finish(p0, icons)),
        ("7. Wybór motywu, jasności i Wi-Fi OTA", render_theme_overlay(p0, icons)),
        ("8. Tryb nocny (ikona księżyca po zmroku)", render_idle(p0, icons, clock_txt="22:15", supply_txt="", weather_icon="ic_w_moon", weather_temp="-1°")),
    ]

    pad_x, pad_y = 28, 24
    col_gap, row_gap = 24, 22
    header_h = 74
    label_h = 30
    pw, ph = 480, 320
    rows = (len(panels) + 1) // 2

    W = pad_x * 2 + pw * 2 + col_gap
    H = header_h + pad_y + (label_h + ph) * rows + row_gap * (rows - 1) + pad_y
    sheet = Image.new("RGBA", (W, H), (11, 15, 25, 255))
    d = ImageDraw.Draw(sheet)

    draw_text_left(d, pad_x, 30, "Zmywarka ESP — Podgląd ekranów 1:1 (480×320)", font(24, True), 0xF8FAFC)
    draw_text_left(d, pad_x, 58, "Prawdziwe proporcje, czcionki i ikony z oprogramowania z przykładowymi danymi", font(14, False), 0x94A3B8)

    for idx, (title, screen_img) in enumerate(panels):
        col = idx % 2
        row = idx // 2
        x = pad_x + col * (pw + col_gap)
        y = header_h + pad_y + row * (label_h + ph + row_gap)
        draw_text_left(d, x + 2, y + 12, title, font(15, True), 0xE2E8F0)
        draw_round_box(sheet, (x - 2, y + label_h - 2), (pw + 4, ph + 4), 16, fill=0x1E293B, border_color=0x334155, border_w=1)
        sheet.alpha_composite(screen_img, (x, y + label_h))

    sheet.save(OUT_SHOWCASE)
    print(f"Saved {OUT_SHOWCASE} ({W}x{H})")


if __name__ == "__main__":
    icons = parse_icons(ICONS_C)
    palettes = parse_palettes(THEME_C)
    print(f"Parsed {len(icons)} icons, {len(palettes)} palettes")
    build_showcase(palettes, icons)
