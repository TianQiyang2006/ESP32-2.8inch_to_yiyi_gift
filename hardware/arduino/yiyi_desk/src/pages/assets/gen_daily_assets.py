# -*- coding: utf-8 -*-
"""Generate the LVGL C image assets used by the hand-written daily page (Page 4).

Sources are the locked daily-v1 design assets (and the original GUI Guider source
PNGs they were cut from).  Output is plain C99 LV_IMG_CF_TRUE_COLOR_ALPHA arrays so
both the firmware build (arduino compiles src/ recursively) and the host offscreen
renderer (test_native_lvgl.py compiles src/pages/assets/**/*.c) link them.

Run:
  D:\\python3.13\\python.exe hardware\\arduino\\yiyi_desk\\src\\pages\\assets\\gen_daily_assets.py

Outputs (same directory):
  daily_assets.h        declarations
  daily_wood_fish.c     transparent wood-fish cutout, 86x49 (design: (7,38) 86x49)
  daily_wood_deco.c     wood decoration / mallet, 34x31 (design: (60,20) 34x31)
  daily_water_wave.c    water wave ellipse slice, 99x29 (design: ellipse -20,62 139x48)
  daily_plus20.c        transparent "+", 20x20 (design: 7-day empty ring plus)
  daily_mood_week.c     6 small mood faces, 23x23 (design: 23x23 inside the 27 ring)
"""
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[5]  # <root>/hardware/arduino/yiyi_desk/src/pages/assets
DESIGN = ROOT / "build/design/daily/final-20261027-daily-v1"
SOURCE = ROOT / "lvgl_ui_project/images/assets"

MOODS = ["happy", "calm", "neutral", "tired", "anxious", "sad"]


def c_asset_image(path):
    """Decode one of the GUI Guider LV_IMG_CF_TRUE_COLOR_ALPHA C files to a PIL image."""
    import re
    text = Path(path).read_text(encoding="utf-8")
    match = re.search(r"\.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, \.always_zero = 0, \.reserved = 0, \.w = (\d+), \.h = (\d+)", text)
    if not match:
        match = re.search(r"\.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, \.w = (\d+), \.h = (\d+)", text)
    w, h = int(match.group(1)), int(match.group(2))
    body = text.split("= {", 1)[1]
    numbers = [int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
    image = Image.new("RGBA", (w, h))
    pixels = []
    for i in range(w * h):
        lo, hi, alpha = numbers[i * 3:i * 3 + 3]
        color = lo | (hi << 8)
        pixels.append((((color >> 11) & 31) * 255 // 31,
                       ((color >> 5) & 63) * 255 // 63,
                       (color & 31) * 255 // 31,
                       alpha))
    image.putdata(pixels)
    return image


def mood_week_source(mood):
    """The 27x27 face used by the design; a few only exist as the GUI Guider C asset."""
    png = SOURCE / ("gift_mood_%s_27.png" % mood)
    if png.exists():
        return Image.open(png).convert("RGBA")
    return c_asset_image(ROOT / ("hardware/arduino/yiyi_desk/src/saved_design/images/"
                                 "gift_mood_%s_27.c" % mood))



def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def pixels_tca(image):
    """RGB565 little-endian + alpha bytes, LV_IMG_CF_TRUE_COLOR_ALPHA layout."""
    image = image.convert("RGBA")
    out = bytearray()
    for r, g, b, a in image.getdata():
        c = rgb565(r, g, b)
        out += bytes((c & 0xFF, (c >> 8) & 0xFF, a))
    return out


def array_file(path, name, data, w, h):
    text = ['#include "daily_assets.h"', "", "static const uint8_t %s_pixels[] = {" % name]
    for i in range(0, len(data), 24):
        text.append(",".join(str(v) for v in data[i:i + 24]) + ",")
    text.append("};")
    text.append("const lv_img_dsc_t %s = {" % name)
    text.append("    .header = {.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = %d, .h = %d},"
                % (w, h))
    text.append("    .data_size = sizeof(%s_pixels)," % name)
    text.append("    .data = %s_pixels" % name)
    text.append("};")
    path.write_text("\n".join(text) + "\n", encoding="utf-8", newline="\n")
    print("wrote", path.name, w, h, len(data), "bytes")


def resize(src, size):
    return Image.open(src).convert("RGBA").resize(size, Image.LANCZOS)


def water_wave(w=99, h=29):
    """The design draws `ellipse([-20, 62, 119, 110])` inside the 99x91 water card.

    Card-row 62..90 is visible, so emit that 99x29 slice; the card's rounded-corner
    clip removes the outside.  Alpha is 4x4 supersampled so the curved top edge is
    smooth.
    """
    img = Image.new("RGBA", (w, h), (128, 192, 247, 0))
    px = img.load()
    cx, cy, rx, ry = 49.5, 24.0, 69.5, 24.0  # card-relative centre of the ellipse
    sub = 4
    for y in range(h):
        for x in range(w):
            hits = 0
            for sy in range(sub):
                for sx in range(sub):
                    fx = x + (sx + 0.5) / sub
                    fy = y + (sy + 0.5) / sub
                    if (fx - cx) ** 2 / rx ** 2 + (fy - cy) ** 2 / ry ** 2 <= 1.0:
                        hits += 1
            if hits:
                px[x, y] = (128, 192, 247, 255 * hits // (sub * sub))
    return img


def main():
    images = {}

    images["daily_wood_fish"] = resize(DESIGN / "wood-fish-cutout.png", (86, 49))
    images["daily_wood_deco"] = resize(SOURCE / "gift_wood_decoration.png", (34, 31))
    images["daily_plus20"] = resize(SOURCE / "gift_decor_4_9.png", (20, 20))
    images["daily_water_wave"] = water_wave()

    header = ['#pragma once', '#include <lvgl.h>', '#ifdef __cplusplus', 'extern "C" {', '#endif']
    for name, image in images.items():
        array_file(HERE / (name + ".c"), name, pixels_tca(image), image.width, image.height)
        header.append("extern const lv_img_dsc_t %s;" % name)

    mood_lines = []
    for mood in MOODS:
        name = "daily_mood_week_" + mood
        image = mood_week_source(mood).resize((23, 23), Image.LANCZOS)
        images[name] = image
        header.append("extern const lv_img_dsc_t %s;" % name)
        mood_lines.append((name, pixels_tca(image)))
    path = HERE / "daily_mood_week.c"
    text = ['#include "daily_assets.h"', ""]
    for name, data in mood_lines:
        text.append("static const uint8_t %s_pixels[] = {" % name)
        for i in range(0, len(data), 24):
            text.append(",".join(str(v) for v in data[i:i + 24]) + ",")
        text.append("};")
        text.append("const lv_img_dsc_t %s = {" % name)
        text.append("    .header = {.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = 23, .h = 23},")
        text.append("    .data_size = sizeof(%s_pixels)," % name)
        text.append("    .data = %s_pixels" % name)
        text.append("};")
        text.append("")
    path.write_text("\n".join(text) + "\n", encoding="utf-8", newline="\n")
    print("wrote", path.name, len(mood_lines), "23x23 mood faces")

    header += ['#ifdef __cplusplus', '}', '#endif', '']
    (HERE / "daily_assets.h").write_text("\n".join(header), encoding="utf-8", newline="\n")
    print("wrote daily_assets.h")


if __name__ == "__main__":
    main()
