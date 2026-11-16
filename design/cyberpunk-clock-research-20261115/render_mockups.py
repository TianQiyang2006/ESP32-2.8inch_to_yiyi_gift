# -*- coding: utf-8 -*-
"""Render three cyberpunk clock-face concept mockups at the real 320x240 screen size.

These are *design concept* renders, not final assets and not firmware code. They are
drawn locally with Pillow so the whole pipeline stays reproducible offline:

    python design/cyberpunk-clock-research-20261115/render_mockups.py

Outputs (in ./mockups/):
    face-01-neon-seven-320x240.png        + nearest-neighbour 3x preview
    face-02-holo-hud-320x240.png          + nearest-neighbour 3x preview
    face-03-glitch-terminal-320x240.png   + nearest-neighbour 3x preview
    faces-contact-sheet.png               3 faces at 2x with captions
    palette.png                           colour chips used by the three faces

Fonts used (all local):
    DSEG7 Classic Mini Bold (open source, SIL OFL)  build/research/.../DSEG7ClassicMini-Bold.ttf
    OCR A Extended, Bahnschrift, Consolas           C:/Windows/Fonts/*
    Microsoft YaHei / Noto Sans SC                  Chinese labels
"""
from __future__ import annotations

import math
import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = HERE / "mockups"

W, H = 320, 240
SS = 4               # supersampling factor for antialiasing
CW, CH = W * SS, H * SS

FONT_DSEG = ROOT / "build/research/clock-styles-20261113/src/3dClock-main/DSEG7ClassicMini-Bold.ttf"
FONT_OCR = Path("C:/Windows/Fonts/OCRAEXT.TTF")
FONT_BAHN = Path("C:/Windows/Fonts/bahnschrift.ttf")
FONT_MONO = Path("C:/Windows/Fonts/consola.ttf")
FONT_MONO_B = Path("C:/Windows/Fonts/consolab.ttf")
FONT_CN = Path("C:/Windows/Fonts/msyh.ttc")
FONT_CN_B = Path("C:/Windows/Fonts/msyhbd.ttc")

_font_cache: dict[tuple[str, int], ImageFont.FreeTypeFont] = {}


def font(path: Path, px: int) -> ImageFont.FreeTypeFont:
    """Load a font at a *final screen pixel* size (internally scaled by SS)."""
    key = (str(path), px)
    if key not in _font_cache:
        _font_cache[key] = ImageFont.truetype(str(path), px * SS)
    return _font_cache[key]


def sheet_font(path: Path, px: int) -> ImageFont.FreeTypeFont:
    """Load a font at an already-final pixel size for non-supersampled sheets."""
    key = (-px, str(path))
    if key not in _font_cache:  # type: ignore[operator]
        _font_cache[key] = ImageFont.truetype(str(path), px)  # type: ignore[index]
    return _font_cache[key]  # type: ignore[index]


def rgb(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]


def vertical_gradient(top: tuple[int, int, int], bottom: tuple[int, int, int]) -> Image.Image:
    ramp = np.linspace(np.array(top, np.float32), np.array(bottom, np.float32), CH)
    arr = np.repeat(ramp[:, None, :], CW, axis=1)
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def radial_glow(img: Image.Image, center: tuple[float, float], radius: float,
                color: tuple[int, int, int], strength: float = 1.0) -> Image.Image:
    cx, cy = center[0] * SS, center[1] * SS
    ys, xs = np.mgrid[0:CH, 0:CW]
    dist = np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2) / (radius * SS)
    mask = np.clip(1.0 - dist, 0.0, 1.0) ** 2
    arr = np.asarray(img).astype(np.float32)
    for i in range(3):
        arr[..., i] += color[i] * mask * strength
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def add_layer(img: Image.Image, layer: Image.Image, blur_px: float = 0.0,
              gain: float = 1.0) -> Image.Image:
    """Additive (screen-like) composite of an RGB glow layer."""
    out = layer
    if blur_px:
        out = out.filter(ImageFilter.GaussianBlur(blur_px * SS))
    if gain != 1.0:
        out = out.point(lambda v: min(255, round(v * gain)))
    return ImageChops.add(img, out)


def neon_text(img: Image.Image, xy: tuple[float, float], text: str,
              fnt: ImageFont.FreeTypeFont, halo: tuple[int, int, int],
              core: tuple[int, int, int], glow_px: tuple[float, ...] = (2.5, 9.0),
              anchor: str = "mm") -> Image.Image:
    layer = Image.new("RGB", (CW, CH), (0, 0, 0))
    ImageDraw.Draw(layer).text((xy[0] * SS, xy[1] * SS), text, font=fnt,
                               fill=halo, anchor=anchor)
    for radius in glow_px:
        img = add_layer(img, layer, blur_px=radius)
    arr = np.asarray(img).copy()
    img = Image.fromarray(arr)
    ImageDraw.Draw(img).text((xy[0] * SS, xy[1] * SS), text, font=fnt,
                             fill=core, anchor=anchor)
    return img


def tracked_text(img: Image.Image, xy: tuple[float, float], text: str,
                 fnt: ImageFont.FreeTypeFont, fill: tuple[int, int, int],
                 tracking: float = 0.0, align: str = "left",
                 glow_layer: Image.Image | None = None) -> Image.Image:
    draw = ImageDraw.Draw(img)
    widths = [draw.textlength(ch, font=fnt) for ch in text]
    total = sum(widths) + tracking * SS * max(0, len(text) - 1)
    x = xy[0] * SS
    if align == "center":
        x -= total / 2
    elif align == "right":
        x -= total
    for ch, w in zip(text, widths):
        draw.text((x, xy[1] * SS), ch, font=fnt, fill=fill, anchor="lm")
        x += w + tracking * SS
    return img


def scanlines(img: Image.Image, spacing: int = 3, strength: float = 0.20,
              phase: int = 1) -> Image.Image:
    arr = np.asarray(img).astype(np.float32)
    for y in range(H):
        if (y + phase) % spacing == 0:
            arr[y * SS:(y + 1) * SS] *= (1.0 - strength)
        elif (y + phase) % spacing == 1:
            arr[y * SS:(y + 1) * SS] *= (1.0 - strength * 0.35)
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def vignette(img: Image.Image, strength: float = 0.55) -> Image.Image:
    ys, xs = np.mgrid[0:CH, 0:CW]
    nx = (xs - CW / 2) / (CW / 2)
    ny = (ys - CH / 2) / (CH / 2)
    r = np.clip(np.sqrt(nx ** 2 + ny ** 2) / math.sqrt(2), 0, 1)
    mask = 1.0 - strength * (r ** 2.2)
    arr = np.asarray(img).astype(np.float32) * mask[..., None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def noise(img: Image.Image, amount: float = 5.0, seed: int = 7) -> Image.Image:
    rng = np.random.default_rng(seed)
    arr = np.asarray(img).astype(np.float32)
    grain = rng.normal(0, amount, (CH, CW, 1))
    return Image.fromarray(np.clip(arr + grain, 0, 255).astype(np.uint8))


def octagon(box: tuple[float, float, float, float], cut: float) -> list[tuple[float, float]]:
    x0, y0, x1, y1 = (v * SS for v in box)
    c = cut * SS
    return [(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c),
            (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)]


def glow_line(img: Image.Image, pts: list[tuple[float, float]], color: tuple[int, int, int],
              width: float = 1.0, glow: float = 0.0, gain: float = 1.0) -> Image.Image:
    layer = Image.new("RGB", (CW, CH), (0, 0, 0))
    ImageDraw.Draw(layer).line(pts, fill=color, width=max(1, round(width * SS)), joint="curve")
    if glow:
        img = add_layer(img, layer, blur_px=glow, gain=gain)
    return ImageChops.add(img, layer)


def hline(img: Image.Image, y: float, x0: float, x1: float,
          color: tuple[int, int, int], width: float = 1.0,
          glow: float = 0.0) -> Image.Image:
    return glow_line(img, [(x0 * SS, y * SS), (x1 * SS, y * SS)], color, width, glow)


def gradient_rule(img: Image.Image, y: float, x0: float, x1: float,
                  left: tuple[int, int, int], right: tuple[int, int, int],
                  width: float = 1.0) -> Image.Image:
    draw = ImageDraw.Draw(img)
    steps = int((x1 - x0) * SS)
    for i in range(steps):
        t = i / max(1, steps - 1)
        col = tuple(round(left[k] * (1 - t) + right[k] * t) for k in range(3))
        x = round((x0 * SS) + i)
        draw.line([(x, y * SS), (x, y * SS + width * SS)], fill=col, width=1)
    return img


def corner_brackets(img: Image.Image, inset: float, length: float,
                    color: tuple[int, int, int], width: float = 1.0,
                    glow: float = 0.0) -> Image.Image:
    x0, y0 = inset * SS, inset * SS
    x1, y1 = CW - inset * SS, CH - inset * SS
    L = length * SS
    for pts in (
        [(x0, y0 + L), (x0, y0), (x0 + L, y0)],
        [(x1 - L, y0), (x1, y0), (x1, y0 + L)],
        [(x0, y1 - L), (x0, y1), (x0 + L, y1)],
        [(x1 - L, y1), (x1, y1), (x1, y1 - L)],
    ):
        img = glow_line(img, pts, color, width, glow)
    return img


# ---------------------------------------------------------------------------
# Face 1 - NEON SEVEN (neon seven-segment + night skyline)
# ---------------------------------------------------------------------------
def face_neon_seven() -> Image.Image:
    cyan = rgb("#39e8ff")
    cyan_core = rgb("#d6fcff")
    magenta = rgb("#ff2d8d")
    amber = rgb("#ffb347")

    img = vertical_gradient(rgb("#05060e"), rgb("#120a20"))
    img = radial_glow(img, (40, 30), 260, (0, 54, 74), 1.0)
    img = radial_glow(img, (300, 190), 220, (66, 0, 52), 0.9)
    img = radial_glow(img, (160, 108), 150, (0, 30, 42), 0.8)

    # faint blueprint grid
    grid = Image.new("RGB", (CW, CH), (0, 0, 0))
    gd = ImageDraw.Draw(grid)
    for gx in range(0, W + 1, 16):
        gd.line([(gx * SS, 0), (gx * SS, CH)], fill=(0, 15, 22), width=1)
    for gy in range(0, H + 1, 16):
        gd.line([(0, gy * SS), (CW, gy * SS)], fill=(0, 15, 22), width=1)
    img = ImageChops.add(img, grid)

    # clipped-corner neon frame
    frame = Image.new("RGB", (CW, CH), (0, 0, 0))
    fd = ImageDraw.Draw(frame)
    fd.line(octagon((7, 7, 313, 233), 14) + [octagon((7, 7, 313, 233), 14)[0]],
            fill=(0, 70, 92), width=2 * SS, joint="curve")
    fd.line(octagon((3, 3, 317, 237), 20) + [octagon((3, 3, 317, 237), 20)[0]],
            fill=(80, 0, 48), width=1 * SS, joint="curve")
    img = add_layer(img, frame, blur_px=3.0)
    img = ImageChops.add(img, frame)

    # top status bar
    img = tracked_text(img, (18, 16), "YIYI_DESKTOP // CLOCK-07",
                       font(FONT_MONO, 7), rgb("#7fdbe8"), tracking=0.3)
    img = tracked_text(img, (302, 16), "SIG ▮▮▮▮", font(FONT_MONO, 7),
                       rgb("#ff5f9e"), tracking=0.2, align="right")
    dot = Image.new("RGB", (CW, CH), (0, 0, 0))
    ImageDraw.Draw(dot).ellipse([(236 * SS, 12 * SS), (242 * SS, 18 * SS)],
                                fill=rgb("#ff2d3d"))
    img = add_layer(img, dot, blur_px=2.0)
    img = ImageChops.add(img, dot)

    # huge seven-segment time
    dseg = font(FONT_DSEG, 78)
    img = neon_text(img, (160, 100), "23:47", dseg, cyan, cyan_core,
                    glow_px=(3.0, 11.0), anchor="mm")
    # seconds tick, magenta, tucked under the right end of the time
    img = neon_text(img, (300, 146), "47", font(FONT_DSEG, 15), magenta,
                    rgb("#ffd0e6"), glow_px=(2.0, 5.0), anchor="rm")
    img = tracked_text(img, (299, 156), "SEC", font(FONT_MONO, 5),
                       rgb("#8a5f78"), tracking=0.4, align="right")

    # cyan -> magenta rule
    img = gradient_rule(img, 160, 18, 302, rgb("#1c6d85"), rgb("#7a2048"), width=1)

    # date row
    img = tracked_text(img, (18, 174), "2026.11.15", font(FONT_MONO, 12),
                       rgb("#bfefff"), tracking=0.4)
    img = tracked_text(img, (18 + 74, 174), "星期六", font(FONT_CN, 10),
                       rgb("#9ad7e8"))
    img = tracked_text(img, (302, 174), "SYS.NOMINAL", font(FONT_MONO, 6),
                       rgb("#5f8f9c"), tracking=0.4, align="right")

    # city skyline with lit windows, neon signs and a rooftop billboard
    rng = random.Random(11)
    sil = Image.new("RGB", (CW, CH), (0, 0, 0))
    sd = ImageDraw.Draw(sil)
    lights = Image.new("RGB", (CW, CH), (0, 0, 0))
    ld = ImageDraw.Draw(lights)
    buildings = []
    x = 10.0
    while x < 312:
        bw = rng.randint(12, 32)
        bh = rng.randint(20, 56)
        top = 236 - bh
        sd.rectangle([(x * SS, top * SS), ((x + bw) * SS, 236 * SS)],
                     fill=(7, 9, 19))
        sd.line([(x * SS, top * SS), ((x + bw) * SS, top * SS)],
                fill=(16, 40, 52), width=SS)
        # occasional stepped setback for silhouette variety
        if bw >= 22 and rng.random() < 0.55:
            inset = rng.uniform(3, 6)
            sd.rectangle([((x + inset) * SS, (top - rng.randint(6, 12)) * SS),
                          ((x + bw - inset) * SS, top * SS)], fill=(7, 9, 19))
        # roof antenna
        if rng.random() < 0.45:
            ax = (x + bw * 0.5) * SS
            ah = rng.randint(4, 10)
            sd.line([(ax, top * SS), (ax, (top - ah) * SS)],
                    fill=(14, 34, 45), width=SS)
            if rng.random() < 0.5:
                ld.ellipse([(ax - SS, (top - ah) * SS - SS),
                            (ax + SS, (top - ah) * SS + SS)], fill=(190, 20, 40))
        # windows: mostly warm amber and cyan, sparse magenta accents
        for wy in range(top + 3, 233, 4):
            for wx in range(int(x) + 2, int(x + bw) - 1, 4):
                if rng.random() < 0.30:
                    color = rng.choices(
                        [(255, 186, 92), (86, 226, 245), (255, 86, 158), (130, 255, 190)],
                        weights=(5, 3, 1, 1))[0]
                    dim = tuple(round(c * rng.uniform(0.18, 0.55)) for c in color)
                    ld.rectangle([(wx * SS, wy * SS),
                                  (wx * SS + SS, wy * SS + SS)], fill=dim)
        buildings.append((x, top, bw, bh))
        x += bw + rng.randint(1, 4)

    # two vertical neon signs
    for sx, sy, sh, col, label in (
        (44, 190, 40, magenta, "夜"),
        (252, 184, 46, rgb("#2be7ff"), "酒"),
    ):
        ld.rectangle([(sx * SS, sy * SS), ((sx + 9) * SS, (sy + sh) * SS)],
                     fill=tuple(round(c * 0.35) for c in col))
        ld.rectangle([(sx * SS, sy * SS), ((sx + 2) * SS, (sy + sh) * SS)],
                     fill=col)
        ld.rectangle([((sx + 7) * SS, sy * SS), ((sx + 9) * SS, (sy + sh) * SS)],
                     fill=tuple(round(c * 0.7) for c in col))
        ld.text(((sx + 4.8) * SS, (sy + sh * 0.5) * SS), label,
                font=font(FONT_CN_B, 9), fill=(255, 255, 255), anchor="mm")

    # small horizontal billboard on the skyline, gift easter egg
    bx0, by0, bx1, by1 = 126, 200, 176, 214
    ld.rectangle([(bx0 * SS, by0 * SS), (bx1 * SS, by1 * SS)],
                 fill=(40, 6, 28))
    ld.rectangle([(bx0 * SS, by0 * SS), (bx1 * SS, by1 * SS)],
                 outline=(255, 90, 170), width=SS)
    ld.text(((bx0 + 25) * SS, (by0 + 7) * SS), "依依 · 07",
            font=font(FONT_CN_B, 7), fill=(255, 214, 236), anchor="mm")

    img = add_layer(img, lights, blur_px=2.4, gain=1.2)
    img = ImageChops.add(img, lights)
    img = ImageChops.add(img, sil)

    # horizon and wet-street reflection glow
    img = hline(img, 236, 8, 312, rgb("#2ad9ef"), 1, glow=3.0)
    img = radial_glow(img, (160, 236), 120, (0, 70, 90), 0.6)

    img = scanlines(img, spacing=3, strength=0.22)
    img = vignette(img, 0.5)
    img = noise(img, 3.5, seed=3)
    return img.resize((W, H), Image.Resampling.LANCZOS)


# ---------------------------------------------------------------------------
# Face 2 - HOLO HUD (holographic head-up display)
# ---------------------------------------------------------------------------
def face_holo_hud() -> Image.Image:
    cyan = rgb("#35f0e0")
    cyan_core = rgb("#e2fffb")
    amber = rgb("#ffb000")
    dim = rgb("#2a7f7c")

    img = vertical_gradient(rgb("#041014"), rgb("#010507"))
    img = radial_glow(img, (160, 116), 180, (0, 60, 58), 1.0)
    img = radial_glow(img, (270, 30), 120, (0, 40, 52), 0.7)

    # dot matrix background
    dots = Image.new("RGB", (CW, CH), (0, 0, 0))
    dd = ImageDraw.Draw(dots)
    for gy in range(6, H, 10):
        for gx in range(6, W, 10):
            dd.rectangle([(gx * SS, gy * SS), (gx * SS + SS, gy * SS + SS)],
                         fill=(10, 38, 40))
    img = ImageChops.add(img, dots)

    # HUD rings (cropped by screen edges, deliberately partial)
    ring = Image.new("RGB", (CW, CH), (0, 0, 0))
    rd = ImageDraw.Draw(ring)
    cx, cy = 160 * SS, 112 * SS
    for radius, col, width in ((118, (18, 92, 94), 1), (104, (14, 68, 72), 1)):
        box = [cx - radius * SS, cy - radius * SS, cx + radius * SS, cy + radius * SS]
        rd.arc(box, 200, 340, fill=col, width=width * SS)
        rd.arc(box, 20, 130, fill=tuple(round(c * 0.75) for c in col), width=width * SS)
    # bright active arc
    rd.arc([cx - 118 * SS, cy - 118 * SS, cx + 118 * SS, cy + 118 * SS],
           214, 268, fill=(30, 190, 180), width=2 * SS)
    # tick marks
    for deg in range(0, 360, 6):
        r0 = 118
        r1 = 124 if deg % 30 else 128
        a = math.radians(deg)
        p0 = (cx + r0 * SS * math.cos(a), cy + r0 * SS * math.sin(a))
        p1 = (cx + r1 * SS * math.cos(a), cy + r1 * SS * math.sin(a))
        col = (44, 150, 148) if deg % 30 == 0 else (18, 82, 86)
        rd.line([p0, p1], fill=col, width=SS)
    # amber bearing marks
    for deg in (238, 246, 254):
        a = math.radians(deg)
        p0 = (cx + 92 * SS * math.cos(a), cy + 92 * SS * math.sin(a))
        p1 = (cx + 104 * SS * math.cos(a), cy + 104 * SS * math.sin(a))
        rd.line([p0, p1], fill=(120, 70, 0), width=SS)
    img = add_layer(img, ring, blur_px=1.6)
    img = ImageChops.add(img, ring)

    # central horizon line + crosshair gaps
    img = hline(img, 112, 8, 118, rgb("#1f6f70"), 1, glow=1.5)
    img = hline(img, 112, 202, 312, rgb("#1f6f70"), 1, glow=1.5)
    img = hline(img, 112, 132, 188, rgb("#2b8f8c"), 1, glow=2.0)
    for tx in (118, 202):
        img = glow_line(img, [(tx * SS, 108 * SS), (tx * SS, 116 * SS)], rgb("#2b8f8c"), 1, 1.5)
    img = corner_brackets(img, 9, 13, rgb("#1e6d6c"), 1, glow=1.2)

    # header / footer micro labels
    img = tracked_text(img, (16, 16), "HOLO // LOCAL TIME", font(FONT_MONO, 6),
                       rgb("#5fc9c2"), tracking=0.5)
    img = tracked_text(img, (304, 16), "NODE 07 · ONLINE", font(FONT_MONO, 6),
                       rgb("#5fc9c2"), tracking=0.5, align="right")
    img = tracked_text(img, (16, 28), "系统待机 · 本地时间", font(FONT_CN, 9),
                       rgb("#3f8f8d"))
    img = tracked_text(img, (304, 28), "SAMPLE VALUES · UI CONCEPT", font(FONT_MONO, 5),
                       rgb("#4f8f8a"), tracking=0.4, align="right")

    # big time
    img = neon_text(img, (160, 108), "23:47", font(FONT_BAHN, 100), cyan, cyan_core,
                    glow_px=(3.0, 12.0), anchor="mm")

    # date + data strip
    img = gradient_rule(img, 168, 16, 304, rgb("#0f4a4a"), rgb("#0f4a4a"), width=1)
    img = tracked_text(img, (16, 180), "2026.11.15", font(FONT_MONO, 12),
                       rgb("#bff5ea"), tracking=0.4)
    img = tracked_text(img, (96, 180), "星期六", font(FONT_CN, 10), rgb("#7fc9c1"))
    img = tracked_text(img, (304, 180), "23:47:12", font(FONT_OCR, 10),
                       rgb("#bff5ea"), align="right")

    # three telemetry cells (example values, design only)
    cells = (("TEMP", "23.5°C"), ("HUM", "52%"), ("PWR", "USB"))
    x = 16
    for label, value in cells:
        img = glow_line(img, [(x * SS, 192 * SS), (x * SS, 206 * SS)], rgb("#155e5c"), 1, 1.0)
        img = tracked_text(img, (x + 5, 196), label, font(FONT_MONO, 5),
                           rgb("#4f9a96"), tracking=0.5)
        img = tracked_text(img, (x + 5, 205), value, font(FONT_MONO, 9),
                           rgb("#a9efe4"))
        x += 96
    img = hline(img, 214, 16, 304, rgb("#14524f"), 1, glow=1.0)

    img = scanlines(img, spacing=2, strength=0.14, phase=0)
    img = vignette(img, 0.55)
    img = noise(img, 2.5, seed=5)
    return img.resize((W, H), Image.Resampling.LANCZOS)


# ---------------------------------------------------------------------------
# Face 3 - GLITCH TERMINAL (cursed CRT terminal)
# ---------------------------------------------------------------------------
def face_glitch_terminal() -> Image.Image:
    green = rgb("#39ff88")
    mint = rgb("#d9ffe6")
    red = rgb("#ff2f4f")
    amber = rgb("#fcee0a")

    img = vertical_gradient(rgb("#020805"), rgb("#010302"))
    img = radial_glow(img, (160, 70), 210, (0, 54, 30), 0.9)
    img = radial_glow(img, (40, 210), 130, (0, 30, 18), 0.6)

    # outer terminal frame
    img = glow_line(img, [(8 * SS, 4 * SS), (312 * SS, 4 * SS), (312 * SS, 236 * SS),
                          (8 * SS, 236 * SS), (8 * SS, 4 * SS)],
                    rgb("#0d4a2b"), 1, glow=1.2)

    # inverted header bar
    bar = Image.new("RGB", (CW, CH), (0, 0, 0))
    bd = ImageDraw.Draw(bar)
    bd.rectangle([(9 * SS, 12 * SS), (311 * SS, 30 * SS)], fill=(4, 26, 15))
    bd.line([(9 * SS, 12 * SS), (311 * SS, 12 * SS)], fill=rgb("#2fbf74"), width=SS)
    bd.line([(9 * SS, 30 * SS), (311 * SS, 30 * SS)], fill=rgb("#1a6b43"), width=SS)
    img = ImageChops.add(img, bar)
    img = tracked_text(img, (15, 21), ">> TIME.SYS v2.077", font(FONT_MONO_B, 7),
                       rgb("#7dffb0"), tracking=0.3)
    img = tracked_text(img, (305, 21), "依依的桌面终端", font(FONT_CN, 8),
                       rgb("#4fd98c"), align="right")

    # chromatic-aberration big time
    mask = Image.new("L", (CW, CH), 0)
    md = ImageDraw.Draw(mask)
    md.text((160 * SS, 86 * SS), "23:47", font=font(FONT_OCR, 84), fill=255,
            anchor="mm")
    m = np.asarray(mask).copy()

    # horizontal glitch slice displacement
    rng = random.Random(23)
    top, bottom = 46 * SS, 126 * SS
    for _ in range(5):
        y = rng.randint(top, bottom - 2)
        h = rng.randint(1, 3) * SS
        shift = rng.choice([-1, 1]) * rng.randint(2, 6) * SS
        band = m[y:y + h, :].copy()
        shifted = np.zeros_like(band)
        if shift > 0:
            shifted[:, shift:] = band[:, :-shift]
        else:
            shifted[:, :shift] = band[:, -shift:]
        m[y:y + h, :] = shifted
    # two dropped scan bands
    for _ in range(2):
        y = rng.randint(top, bottom - SS)
        m[y:y + SS, :] = 0

    for color, dx, dy, blur in ((rgb("#ff2f4f"), -2, -1, 1.2),
                                (rgb("#2fe8ff"), 2, 1, 1.2),
                                (rgb("#39ff88"), 0, 0, 2.5)):
        layer = Image.new("RGB", (CW, CH), (0, 0, 0))
        arr = np.zeros((CH, CW, 3), np.uint8)
        arr[m > 0] = color
        layer = Image.fromarray(arr)
        layer = ImageChops.offset(layer, round(dx * SS), round(dy * SS))
        img = add_layer(img, layer, blur_px=blur, gain=0.75)

    core_layer = Image.new("RGB", (CW, CH), (0, 0, 0))
    arr = np.zeros((CH, CW, 3), np.uint8)
    arr[m > 0] = mint
    core_layer = Image.fromarray(arr)
    img = ImageChops.add(img, core_layer)

    # date + seconds readout
    img = tracked_text(img, (16, 138), "2026-11-15  SAT", font(FONT_MONO_B, 11),
                       rgb("#9dffc0"), tracking=0.4)
    img = tracked_text(img, (160, 138), "星期六", font(FONT_CN, 10), rgb("#6bd494"))
    img = neon_text(img, (304, 138), "47s", font(FONT_OCR, 10), green,
                    rgb("#d9ffe6"), glow_px=(1.2, 3.0), anchor="rm")
    img = hline(img, 150, 16, 304, rgb("#0f5c34"), 1, glow=1.0)

    # terminal lines
    lines = (
        ("> uplink yiyi_desk ........ OK", rgb("#6bffa8")),
        ("> ntp sync ................ 0.043s", rgb("#3fcf7d")),
        ("> render clockface ........ GLITCH ON", rgb("#b9ff7d")),
        ("> err 0x077 .............. RECOVERED", rgb("#ff7d9a")),
    )
    y = 158
    for text, col in lines:
        img = tracked_text(img, (16, y), text, font(FONT_MONO, 7), col, tracking=0.2)
        y += 11
    # blinking cursor block
    cursor = Image.new("RGB", (CW, CH), (0, 0, 0))
    ImageDraw.Draw(cursor).rectangle([(16 * SS, (y - 7) * SS), (24 * SS, (y - 1) * SS)],
                                     fill=rgb("#39ff88"))
    img = ImageChops.add(img, cursor)

    # right-edge binary column
    rng2 = random.Random(4)
    for i, by in enumerate(range(44, 190, 8)):
        bits = "".join(rng2.choice("01") for _ in range(5))
        img = tracked_text(img, (309, by), bits, font(FONT_MONO, 5),
                           rgb("#1e6b3e"), align="right")

    # hazard tape
    tape = Image.new("RGB", (CW, CH), (0, 0, 0))
    td = ImageDraw.Draw(tape)
    td.rectangle([(9 * SS, 220 * SS), (311 * SS, 232 * SS)], fill=(38, 32, 0))
    for x0 in range(9, 311, 16):
        td.polygon([(x0 * SS, 232 * SS), ((x0 + 8) * SS, 232 * SS),
                    ((x0 + 14) * SS, 220 * SS), ((x0 + 6) * SS, 220 * SS)],
                   fill=(80, 68, 0))
    img = ImageChops.add(img, tape)
    img = tracked_text(img, (160, 226), "WARNING // TEMPORAL DRIFT 0.00s",
                       font(FONT_MONO_B, 7), rgb("#ffe83f"), tracking=0.5, align="center")

    img = scanlines(img, spacing=2, strength=0.30, phase=0)
    img = vignette(img, 0.65)
    img = noise(img, 7.0, seed=9)
    return img.resize((W, H), Image.Resampling.LANCZOS)


# ---------------------------------------------------------------------------
# Sheets
# ---------------------------------------------------------------------------
def save_outputs() -> dict[str, Path]:
    OUT.mkdir(parents=True, exist_ok=True)
    faces = {
        "01-neon-seven": ("霓虹七段 · NEON SEVEN",
                          "DSEG7 七段数码 + 城市夜景天际线，青/洋红双色霓虹",
                          face_neon_seven),
        "02-holo-hud": ("全息 HUD · HOLO HUD",
                        "HUD 圆环刻度 + 等宽小字，青绿单色全息界面",
                        face_holo_hud),
        "03-glitch-terminal": ("故障终端 · GLITCH TERMINAL",
                               "CRT 扫描线 + 色差切片 + 终端命令行，绿色磷光",
                               face_glitch_terminal),
    }
    saved = {}
    for key, (title, caption, fn) in faces.items():
        img = fn()
        base = OUT / f"face-{key}-320x240"
        img.save(base.with_suffix(".png"))
        img.resize((W * 3, H * 3), Image.Resampling.NEAREST).save(
            f"{base}-preview-3x.png")
        saved[key] = (title, caption, img)

    # contact sheet at 2x
    scale = 2
    pad = 18
    label_h = 44
    cell_w, cell_h = W * scale, H * scale
    sheet_w = pad * 2 + cell_w * 3 + pad * 2
    sheet_h = pad + label_h + cell_h + 44 + pad
    sheet = Image.new("RGB", (sheet_w, sheet_h), rgb("#0c0d12"))
    draw = ImageDraw.Draw(sheet)
    for col, (key, (title, caption, img)) in enumerate(saved.items()):
        x = pad + col * (cell_w + pad)
        y = pad
        draw.text((x, y + 2), f"{col + 1:02d}  {title}", font=sheet_font(FONT_CN_B, 13),
                  fill=rgb("#e8fbff"))
        draw.text((x, y + 22), caption, font=sheet_font(FONT_CN, 8), fill=rgb("#7fa6b0"))
        big = img.resize((cell_w, cell_h), Image.Resampling.NEAREST)
        sheet.paste(big, (x, y + label_h))
        draw.rectangle([x - 1, y + label_h - 1, x + cell_w, y + label_h + cell_h],
                       outline=rgb("#26323a"))
    draw.text((pad, sheet_h - 30), "320×240 真机分辨率示意稿 · 2× 最近邻放大 · 2026-11-15",
              font=sheet_font(FONT_CN, 9), fill=rgb("#5d7b84"))
    sheet.save(OUT / "faces-contact-sheet.png")

    # palette card
    palettes = [
        ("霓虹七段", ["#05060e", "#39e8ff", "#ff2d8d", "#ffb347", "#d6fcff"]),
        ("全息 HUD", ["#041014", "#35f0e0", "#ffb000", "#2a7f7c", "#e2fffb"]),
        ("故障终端", ["#020805", "#39ff88", "#ff2f4f", "#fcee0a", "#d9ffe6"]),
    ]
    pw, ph = 960, 372
    pal = Image.new("RGB", (pw, ph), rgb("#0c0d12"))
    pd = ImageDraw.Draw(pal)
    pd.text((24, 18), "赛博朋克钟面配色 · Cyberpunk clock palettes",
            font=sheet_font(FONT_CN_B, 16), fill=rgb("#e8fbff"))
    for row, (name, colors) in enumerate(palettes):
        y = 62 + row * 96
        pd.text((24, y), name, font=sheet_font(FONT_CN_B, 13), fill=rgb("#cfe8ee"))
        for i, value in enumerate(colors):
            x = 150 + i * 150
            pd.rectangle([x, y, x + 128, y + 52], fill=rgb(value),
                         outline=rgb("#2a3540"))
            pd.text((x, y + 60), value.upper(), font=sheet_font(FONT_MONO, 10),
                    fill=rgb("#9fb9c2"))
    pd.text((24, ph - 26), "左起：背景 / 主色 / 强调 / 辅色 / 高光", font=sheet_font(FONT_CN, 9),
            fill=rgb("#5d7b84"))
    pal.save(OUT / "palette.png")
    return {key: OUT / f"face-{key}-320x240.png" for key in saved}


if __name__ == "__main__":
    for key, path in save_outputs().items():
        print(f"wrote {key}: {path.relative_to(ROOT)}")
    print(f"wrote contact sheet + palette: {(OUT / 'faces-contact-sheet.png').relative_to(ROOT)}")
