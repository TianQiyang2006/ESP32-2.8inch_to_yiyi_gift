# -*- coding: utf-8 -*-
"""Generate the 56x56 Tetris launcher icon (LV_IMG_CF_TRUE_COLOR_ALPHA).

The icon is drawn deterministically with Pillow at 4x and downscaled, then
emitted as a plain C99 array so both the ESP32 build and the host offscreen
renderer link the same asset.  Run:

  python hardware/arduino/yiyi_desk/src/pages/assets/gen_tetris_icon.py

Outputs (same directory):
  tetris_app_icon.h
  tetris_app_icon.c
"""
from pathlib import Path
import warnings

from PIL import Image, ImageDraw

warnings.filterwarnings("ignore", category=DeprecationWarning)  # Pillow getdata() is fine here

HERE = Path(__file__).resolve().parent
SCALE = 4
SIZE = 56
BLOCK = 12
GAP = 2
ORIGIN = (SIZE - (4 * BLOCK + 3 * GAP)) // 2  # centre the 4x4 block grid

# Filled cells of the illustration: a falling T piece over a two-row stack.
# (col, row) in a 4x4 block grid.
T_PIECE = [(1, 0), (0, 1), (1, 1), (2, 1)]
O_PIECE = [(0, 2), (1, 2), (0, 3), (1, 3)]
J_PIECE = [(2, 2), (3, 2), (3, 3)]

COLORS = {
    "T": (0x5F, 0x8F, 0x70),  # green
    "O": (0xC9, 0xCC, 0x8F),  # olive
    "J": (0x8C, 0xA0, 0xB5),  # slate
}


def scaled(value):
    return value * SCALE


def block_area(col, row):
    x = scaled(ORIGIN + col * (BLOCK + GAP))
    y = scaled(ORIGIN + row * (BLOCK + GAP))
    return x, y, x + scaled(BLOCK), y + scaled(BLOCK)


def draw_block(draw, col, row, color, shadow=False):
    x0, y0, x1, y1 = block_area(col, row)
    radius = scaled(3)
    if shadow:
        off = scaled(2)
        draw.rounded_rectangle((x0 + off, y0 + off, x1 + off, y1 + off), radius=radius, fill=(24, 45, 33, 70))
        return
    draw.rounded_rectangle((x0, y0, x1, y1), radius=radius, fill=color + (255,))
    # 1 px darker outline and a soft top highlight keep the flat blocks readable.
    inside = (x0 + scaled(1), y0 + scaled(1), x1 - scaled(1), y1 - scaled(1))
    darker = tuple(max(0, channel - 38) for channel in color)
    draw.rounded_rectangle(inside, radius=radius, outline=darker + (255,), width=scaled(1))
    highlight = tuple(min(255, channel + 30) for channel in color)
    draw.rounded_rectangle((x0 + scaled(2), y0 + scaled(2), x1 - scaled(2), y0 + scaled(4)), radius=radius,
                           fill=highlight + (150,))


def render():
    image = Image.new("RGBA", (SIZE * SCALE, SIZE * SCALE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    pieces = [(T_PIECE, "T"), (O_PIECE, "O"), (J_PIECE, "J")]
    for cells, name in pieces:
        for cell in cells:
            draw_block(draw, cell[0], cell[1], COLORS[name], shadow=True)
    for cells, name in pieces:
        for cell in cells:
            draw_block(draw, cell[0], cell[1], COLORS[name])
    return image.resize((SIZE, SIZE), Image.LANCZOS)


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def pixels_tca(image):
    out = bytearray()
    for r, g, b, a in image.convert("RGBA").getdata():
        c = rgb565(r, g, b)
        out += bytes((c & 0xFF, (c >> 8) & 0xFF, a))
    return out


def main():
    image = render()
    data = pixels_tca(image)
    name = "tetris_app_icon"
    lines = ['#include "tetris_app_icon.h"', "", "static const uint8_t %s_pixels[] = {" % name]
    for i in range(0, len(data), 24):
        lines.append(",".join(str(v) for v in data[i:i + 24]) + ",")
    lines.append("};")
    lines.append("const lv_img_dsc_t %s = {" % name)
    lines.append("    .header = {.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .always_zero = 0, .reserved = 0, .w = %d, .h = %d}," % (SIZE, SIZE))
    lines.append("    .data_size = sizeof(%s_pixels)," % name)
    lines.append("    .data = %s_pixels" % name)
    lines.append("};")
    (HERE / "tetris_app_icon.c").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    header = [
        "#pragma once",
        "#include <lvgl.h>",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "extern const lv_img_dsc_t %s;" % name,
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
    ]
    (HERE / "tetris_app_icon.h").write_text("\n".join(header), encoding="utf-8", newline="\n")
    # Preview for design review only; it is not linked into the firmware.
    preview = HERE.parents[5] / "build/tetris-20261101/icon-preview.png"
    preview.parent.mkdir(parents=True, exist_ok=True)
    image.save(preview)
    print("wrote tetris_app_icon.c %d bytes, %dx%d; preview %s" % (len(data), SIZE, SIZE, preview))


if __name__ == "__main__":
    main()
