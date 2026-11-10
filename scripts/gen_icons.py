"""Generuje antyaliasowane ikony LVGL (ui_icons.h / ui_icons.c) z ksztaltow wektorowych."""

import math
import sys
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT_H = ROOT / "src" / "ui" / "include" / "ui_icons.h"
OUT_C = ROOT / "src" / "ui" / "ui_icons.c"
SS = 4


def render_icon(name, size, draw_fn):
    W = size * SS
    im = Image.new("L", (W, W), 0)
    d = ImageDraw.Draw(im)
    draw_fn(d, W)
    im = im.resize((size, size), Image.Resampling.LANCZOS)
    return name, size, size, list(im.getdata()), im


def draw_clock(d, W):
    p = int(W * 0.10)
    w = int(W * 0.11)
    d.ellipse([p, p, W - p - 1, W - p - 1], outline=255, width=w)
    cx, cy = W // 2, W // 2
    d.line([cx, cy, cx, int(W * 0.28)], fill=255, width=w)
    d.line([cx, cy, int(W * 0.68), int(W * 0.62)], fill=255, width=w)
    r = w // 2
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)


def draw_bolt(d, W):
    pts = [
        (W * 0.58, W * 0.08),
        (W * 0.22, W * 0.54),
        (W * 0.48, W * 0.54),
        (W * 0.38, W * 0.92),
        (W * 0.78, W * 0.44),
        (W * 0.52, W * 0.44),
    ]
    d.polygon(pts, fill=255)


def draw_play(d, W):
    pts = [(W * 0.24, W * 0.14), (W * 0.84, W * 0.50), (W * 0.24, W * 0.86)]
    d.polygon(pts, fill=255)


def draw_stop(d, W):
    p = int(W * 0.18)
    r = int(W * 0.16)
    d.rounded_rectangle([p, p, W - p - 1, W - p - 1], radius=r, fill=255)


def draw_moon(d, W):
    p = int(W * 0.10)
    d.ellipse([p, p, W - p - 1, W - p - 1], fill=255)
    d.ellipse([int(W * 0.34), int(W * 0.04), int(W * 1.06), int(W * 0.76)], fill=0)


def draw_warn(d, W):
    w = int(W * 0.11)
    pts = [(W * 0.50, W * 0.12), (W * 0.90, W * 0.86), (W * 0.10, W * 0.86)]
    d.line(pts + [pts[0]], fill=255, width=w, joint="curve")
    d.line([(W * 0.50, W * 0.38), (W * 0.50, W * 0.60)], fill=255, width=w)
    r = int(w * 0.55)
    cy = int(W * 0.72)
    d.ellipse([W // 2 - r, cy - r, W // 2 + r, cy + r], fill=255)


def draw_check(d, W):
    w = int(W * 0.11)
    p = int(W * 0.08)
    d.ellipse([p, p, W - p - 1, W - p - 1], outline=255, width=w)
    d.line([(W * 0.28, W * 0.52), (W * 0.44, W * 0.68), (W * 0.74, W * 0.34)], fill=255, width=w, joint="curve")


def fill_cloud(d, x, y, w, h, val=255):
    r1 = h * 0.42
    r2 = h * 0.56
    r3 = h * 0.38
    base_y = y + h
    d.ellipse([x, base_y - 2 * r1, x + 2 * r1, base_y], fill=val)
    d.ellipse([x + w * 0.22, y, x + w * 0.22 + 2 * r2, y + 2 * r2], fill=val)
    d.ellipse([x + w - 2 * r3, base_y - 2 * r3, x + w, base_y], fill=val)
    d.rectangle([x + r1, base_y - h * 0.56, x + w - r3, base_y], fill=val)


def draw_w_sun(d, W):
    cx, cy = W * 0.5, W * 0.5
    r = W * 0.21
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    rw = max(4, int(W * 0.08))
    for i in range(8):
        ang = i * math.pi / 4.0
        d.line([
            (cx + math.cos(ang) * (W * 0.29), cy + math.sin(ang) * (W * 0.29)),
            (cx + math.cos(ang) * (W * 0.42), cy + math.sin(ang) * (W * 0.42)),
        ], fill=255, width=rw)


def draw_w_moon(d, W):
    cx, cy, r = W * 0.44, W * 0.54, W * 0.34
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    d.ellipse([cx - r * 0.30, cy - r * 1.15, cx + r * 1.55, cy + r * 0.70], fill=0)
    sx, sy, sr = W * 0.76, W * 0.25, W * 0.12
    sw = max(3, int(W * 0.06))
    d.line([(sx - sr, sy), (sx + sr, sy)], fill=255, width=sw)
    d.line([(sx, sy - sr), (sx, sy + sr)], fill=255, width=sw)


def draw_w_partly_day(d, W):
    cx, cy, r = W * 0.32, W * 0.36, W * 0.16
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    rw = max(3, int(W * 0.065))
    for deg in (-150, -105, -60, 165, 120):
        ang = math.radians(deg)
        d.line([
            (cx + math.cos(ang) * (W * 0.21), cy + math.sin(ang) * (W * 0.21)),
            (cx + math.cos(ang) * (W * 0.29), cy + math.sin(ang) * (W * 0.29)),
        ], fill=255, width=rw)
    fill_cloud(d, W * 0.24, W * 0.36, W * 0.70, W * 0.48, val=0)
    fill_cloud(d, W * 0.28, W * 0.40, W * 0.64, W * 0.42, val=255)


def draw_w_partly_night(d, W):
    cx, cy, r = W * 0.30, W * 0.34, W * 0.22
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    d.ellipse([cx - r * 0.15, cy - r * 1.10, cx + r * 1.45, cy + r * 0.55], fill=0)
    fill_cloud(d, W * 0.24, W * 0.36, W * 0.70, W * 0.48, val=0)
    fill_cloud(d, W * 0.28, W * 0.40, W * 0.64, W * 0.42, val=255)


def draw_w_cloud(d, W):
    fill_cloud(d, W * 0.09, W * 0.24, W * 0.82, W * 0.54, val=255)


def draw_w_fog(d, W):
    fill_cloud(d, W * 0.14, W * 0.14, W * 0.72, W * 0.42, val=255)
    lw = max(4, int(W * 0.08))
    d.line([(W * 0.14, W * 0.68), (W * 0.86, W * 0.68)], fill=255, width=lw)
    d.line([(W * 0.22, W * 0.84), (W * 0.78, W * 0.84)], fill=255, width=lw)


def draw_w_rain(d, W):
    fill_cloud(d, W * 0.12, W * 0.12, W * 0.76, W * 0.44, val=255)
    rw = max(4, int(W * 0.085))
    for rx in (0.30, 0.52, 0.74):
        d.line([(W * rx, W * 0.66), (W * (rx - 0.08), W * 0.86)], fill=255, width=rw)


def draw_w_snow(d, W):
    cx, cy = W * 0.5, W * 0.5
    lw = max(4, int(W * 0.08))
    arm = W * 0.37
    tick = W * 0.12
    for k in range(3):
        ang = math.radians(k * 60)
        dx, dy = math.cos(ang) * arm, math.sin(ang) * arm
        d.line([(cx - dx, cy - dy), (cx + dx, cy + dy)], fill=255, width=lw)
        for sign in (-1, 1):
            bx, by = cx + sign * dx * 0.62, cy + sign * dy * 0.62
            for da in (-48, 48):
                ta = ang + math.radians(da)
                d.line(
                    [(bx, by), (bx + sign * math.cos(ta) * tick, by + sign * math.sin(ta) * tick)],
                    fill=255,
                    width=max(3, lw - 1),
                )


def draw_w_storm(d, W):
    fill_cloud(d, W * 0.12, W * 0.10, W * 0.76, W * 0.43, val=255)
    pts = [
        (W * 0.56, W * 0.56),
        (W * 0.36, W * 0.74),
        (W * 0.51, W * 0.74),
        (W * 0.43, W * 0.94),
        (W * 0.66, W * 0.70),
        (W * 0.50, W * 0.70),
    ]
    d.polygon(pts, fill=255)


def main():
    icons = [
        render_icon("ic_clock",          28, draw_clock),
        render_icon("ic_bolt",           28, draw_bolt),
        render_icon("ic_play",           28, draw_play),
        render_icon("ic_stop",           20, draw_stop),
        render_icon("ic_moon",           20, draw_moon),
        render_icon("ic_warn",           24, draw_warn),
        render_icon("ic_check",          36, draw_check),
        render_icon("ic_w_sun",          22, draw_w_sun),
        render_icon("ic_w_moon",         22, draw_w_moon),
        render_icon("ic_w_partly_day",   22, draw_w_partly_day),
        render_icon("ic_w_partly_night", 22, draw_w_partly_night),
        render_icon("ic_w_cloud",        22, draw_w_cloud),
        render_icon("ic_w_fog",          22, draw_w_fog),
        render_icon("ic_w_rain",         22, draw_w_rain),
        render_icon("ic_w_snow",         22, draw_w_snow),
        render_icon("ic_w_storm",        22, draw_w_storm),
    ]

    with open(OUT_H, "w", encoding="utf-8") as fh:
        fh.write('#pragma once\n#include "lvgl.h"\n\n')
        for name, _, _, _, _ in icons:
            fh.write(f"LV_IMG_DECLARE({name});\n")

    with open(OUT_C, "w", encoding="utf-8") as fc:
        fc.write('#include "ui_icons.h"\n\n')
        for name, w, h, data, _ in icons:
            fc.write(f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}_map[] = {{\n")
            for i in range(0, len(data), 16):
                row = ", ".join(f"0x{b:02x}" for b in data[i : i + 16])
                fc.write(f"    {row},\n")
            fc.write("};\n\n")
            fc.write(f"const lv_img_dsc_t {name} = {{\n")
            fc.write("    .header.cf = LV_IMG_CF_ALPHA_8BIT,\n")
            fc.write("    .header.always_zero = 0,\n")
            fc.write("    .header.reserved = 0,\n")
            fc.write(f"    .header.w = {w},\n")
            fc.write(f"    .header.h = {h},\n")
            fc.write(f"    .data_size = {len(data)},\n")
            fc.write(f"    .data = {name}_map,\n")
            fc.write("};\n\n")

    if "--preview" in sys.argv:
        sheet = Image.new("RGB", (9 * 48, 64), (14, 19, 31))
        for idx, (name, w, h, _, im) in enumerate(icons[7:]):
            col = (245, 158, 11) if ("sun" in name or "partly_day" in name or "storm" in name) else \
                  (56, 189, 248) if ("moon" in name or "partly_night" in name or "rain" in name) else \
                  (224, 242, 254) if "snow" in name else (148, 163, 184)
            tile = Image.new("RGB", (w, h), col)
            sheet.paste(tile, (idx * 48 + 13, 21), im)
        out_png = ROOT / "scripts" / "weather_icons_preview.png"
        sheet.resize((9 * 96, 128), Image.Resampling.NEAREST).save(out_png)
        print("Saved preview:", out_png)

    print("Generated icons:", [i[0] for i in icons])


if __name__ == "__main__":
    main()
