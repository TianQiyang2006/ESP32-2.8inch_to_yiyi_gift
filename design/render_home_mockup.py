"""Render an actual 320 x 240 screen design, with editable artwork layers.

This is a deterministic, locally drawn UI study, not an image-generation result.
Run: python design/render_home_mockup.py
Pillow and Microsoft YaHei / Bahnschrift fonts are required.
"""

from __future__ import annotations

import html
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
OUT = ROOT / "mockups"
ASSETS = OUT / "assets"
WIDTH, HEIGHT, SCALE = 320, 240, 4
CREAM = "#F7F7ED"
INK = "#284C3D"
SECONDARY = "#637F68"
BLACK = "#202829"
FONTS = {
    "body": (Path("C:/Windows/Fonts/msyh.ttc"), "Microsoft YaHei"),
    "clock": (Path("C:/Windows/Fonts/bahnschrift.ttf"), "Bahnschrift"),
}


class Canvas:
    """Record vector geometry while rasterizing it with supersampling."""

    def __init__(self, background: str | None = None):
        self.image = Image.new("RGBA", (WIDTH * SCALE, HEIGHT * SCALE), background or (0, 0, 0, 0))
        self.draw = ImageDraw.Draw(self.image)
        self.svg = []
        if background:
            self.svg.append(f'<rect width="{WIDTH}" height="{HEIGHT}" fill="{background}"/>')

    @staticmethod
    def scaled(box):
        return tuple(round(x * SCALE) for x in box)

    def ellipse(self, box, fill, outline=None, width=1):
        self.draw.ellipse(self.scaled(box), fill=fill, outline=outline, width=round(width * SCALE))
        x1, y1, x2, y2 = box
        extra = f' stroke="{outline}" stroke-width="{width}"' if outline else ""
        self.svg.append(f'<ellipse cx="{(x1+x2)/2}" cy="{(y1+y2)/2}" rx="{(x2-x1)/2}" ry="{(y2-y1)/2}" fill="{fill}"{extra}/>')

    def rounded_rect(self, box, radius, fill):
        self.draw.rounded_rectangle(self.scaled(box), radius=round(radius * SCALE), fill=fill)
        x1, y1, x2, y2 = box
        self.svg.append(f'<rect x="{x1}" y="{y1}" width="{x2-x1}" height="{y2-y1}" rx="{radius}" fill="{fill}"/>')

    def path(self, commands, fill=None, stroke=None, width=1, closed=False):
        points = []
        svg_commands = []
        current = (0, 0)
        start = current
        for command in commands:
            op, *values = command
            svg_commands.append(op + " " + " ".join(str(v) for v in values))
            if op == "M":
                current = (values[0], values[1])
                start = current
                points.append(current)
            elif op == "L":
                current = (values[0], values[1])
                points.append(current)
            elif op in ("C", "Q"):
                p0 = current
                p1 = (values[0], values[1])
                p2 = (values[2], values[3])
                p3 = (values[4], values[5]) if op == "C" else None
                for step in range(1, 49):
                    t = step / 48
                    u = 1 - t
                    if op == "C":
                        points.append(tuple(u**3*p0[i] + 3*u*u*t*p1[i] + 3*u*t*t*p2[i] + t**3*p3[i] for i in (0, 1)))
                    else:
                        points.append(tuple(u*u*p0[i] + 2*u*t*p1[i] + t*t*p2[i] for i in (0, 1)))
                current = p3 if op == "C" else p2
            elif op == "Z":
                points.append(start)
                current = start
        if closed and points[-1] != start:
            points.append(start)
            svg_commands.append("Z")
        scaled_points = [(round(x*SCALE), round(y*SCALE)) for x, y in points]
        if fill:
            self.draw.polygon(scaled_points, fill=fill)
        if stroke:
            self.draw.line(scaled_points, fill=stroke, width=round(width*SCALE), joint="curve")
            if not closed:
                radius = width / 2
                for x, y in (points[0], points[-1]):
                    self.draw.ellipse(self.scaled((x-radius, y-radius, x+radius, y+radius)), fill=stroke)
        self.svg.append(f'<path d="{" ".join(svg_commands)}" fill="{fill or "none"}" stroke="{stroke or "none"}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>')

    def text(self, position, value, size, fill, font="body"):
        font_path, family = FONTS[font]
        raster_font = ImageFont.truetype(str(font_path), round(size*SCALE))
        self.draw.text((round(position[0]*SCALE), round(position[1]*SCALE)), value, font=raster_font, fill=fill, anchor="ls")
        self.svg.append(f'<text x="{position[0]}" y="{position[1]}" font-family="{family}" font-size="{size}" fill="{fill}">{html.escape(value)}</text>')

    def bitmap(self):
        return self.image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)

    def save_svg(self, filename):
        contents = '\n'.join(self.svg)
        filename.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" role="img" aria-label="小黑主题桌面时钟主页视觉稿">\n{contents}\n</svg>\n', encoding="utf-8")


def leaf(canvas, x, y, dx, dy, color):
    # A tapered leaf, deliberately broad enough to survive native resolution.
    canvas.path([
        ("M", x, y),
        ("Q", x+dx*.15-dy*.22, y+dy*.15+dx*.22, x+dx, y+dy),
        ("Q", x+dx*.15+dy*.22, y+dy*.15-dx*.22, x, y),
        ("Z",),
    ], fill=color)


def draw_background(canvas):
    canvas.ellipse((193, 34, 326, 171), "#EAF0DF")
    canvas.path([("M", 197, 164), ("Q", 233, 147, 269, 157), ("Q", 290, 149, 308, 164), ("Q", 260, 178, 197, 164), ("Z",)], fill="#DCE8CD")
    canvas.ellipse((212, 157, 292, 166), "#C2D3B5")
    canvas.path([("M", 305, 116), ("Q", 295, 82, 319, 48)], stroke="#BFCEB1", width=1.6)
    for values in [(309, 68, -17, -10), (304, 81, 16, -10), (301, 95, -18, -9), (305, 111, 15, -15)]:
        leaf(canvas, *values, "#CFDDBD")
    canvas.path([("M", 210, 158), ("Q", 202, 146, 203, 137)], stroke="#97B28C", width=1.3)
    leaf(canvas, 205, 147, -12, -8, "#A8C297")
    leaf(canvas, 207, 152, 12, -9, "#B3CA9E")
    # Small, quiet leaf motif balances the status icons without adding text.
    canvas.path([("M", 20, 28), ("Q", 27, 28, 35, 20)], stroke="#9DB68C", width=1)
    leaf(canvas, 26, 27, 0, -10, "#B5CBA4")
    leaf(canvas, 30, 24, 12, -2, "#D2DFBF")


def draw_cat(canvas):
    # Original, locally drawn character study; not official franchise artwork.
    # Tail first, behind the body.
    canvas.path([("M", 262, 149), ("C", 283, 160, 306, 145, 304, 127), ("C", 304, 118, 298, 117, 293, 120)], stroke=BLACK, width=10)
    canvas.path([("M", 232, 108), ("C", 217, 115, 222, 135, 220, 147), ("C", 214, 162, 225, 165, 239, 161), ("C", 249, 165, 270, 164, 273, 151), ("C", 276, 137, 266, 120, 263, 110), ("Z",)], fill=BLACK)
    canvas.path([("M", 211, 76), ("Q", 207, 61, 211, 42), ("Q", 223, 45, 231, 57), ("Q", 246, 51, 260, 56), ("Q", 269, 43, 281, 39), ("Q", 285, 57, 280, 74), ("C", 289, 91, 283, 110, 270, 116), ("C", 257, 123, 225, 121, 215, 111), ("C", 204, 101, 204, 87, 211, 76), ("Z",)], fill=BLACK)
    canvas.path([("M", 214, 49), ("L", 219, 68), ("L", 226, 60), ("Z",)], fill="#354342")
    canvas.path([("M", 277, 47), ("L", 265, 60), ("L", 277, 67), ("Z",)], fill="#354342")
    # Soft forehead accent; broad silhouette remains readable at 320 x 240.
    canvas.path([("M", 231, 64), ("Q", 246, 58, 260, 63), ("Q", 250, 62, 243, 67), ("Q", 237, 63, 231, 64), ("Z",)], fill="#2C3836")
    canvas.ellipse((217, 77, 239, 101), "#BDD5A5")
    canvas.ellipse((252, 76, 274, 100), "#BDD5A5")
    canvas.ellipse((224, 81, 235, 98), "#132322")
    canvas.ellipse((257, 80, 268, 97), "#132322")
    canvas.ellipse((225, 82, 228.5, 85.5), "#F3F6E9")
    canvas.ellipse((258, 81, 261.5, 84.5), "#F3F6E9")
    canvas.path([("M", 242, 104), ("Q", 245, 106.5, 248, 104), ("Q", 246, 109, 245, 109), ("Q", 244, 109, 242, 104), ("Z",)], fill="#74958A")
    canvas.path([("M", 239, 112), ("Q", 242, 115, 245, 112), ("Q", 248, 115, 251, 111)], stroke="#455954", width=1)
    canvas.path([("M", 233, 140), ("Q", 229, 150, 231, 160)], stroke="#374642", width=1.4)
    canvas.path([("M", 256, 139), ("Q", 260, 150, 258, 160)], stroke="#374642", width=1.4)
    canvas.path([("M", 224, 159), ("L", 226, 155)], stroke="#374642", width=1)
    canvas.path([("M", 265, 159), ("L", 263, 155)], stroke="#374642", width=1)


def draw_sun(canvas, x, y):
    for degree in range(0, 360, 45):
        rad = math.radians(degree)
        canvas.path([("M", x+10*math.cos(rad), y+10*math.sin(rad)), ("L", x+13*math.cos(rad), y+13*math.sin(rad))], stroke="#CFA74F", width=1.8)
    canvas.ellipse((x-6.5, y-6.5, x+6.5, y+6.5), "#E1BA5D")


def draw_status(canvas):
    # Wi-Fi and microphone-ready indicators, not small touch buttons.
    for radius in (8, 5):
        x, y = 298, 24
        canvas.path([("M", x-radius*.72, y-radius*.72), ("Q", x, y-radius*1.3, x+radius*.72, y-radius*.72)], stroke=SECONDARY, width=1.4)
    canvas.ellipse((297, 22, 299, 24), SECONDARY)
    canvas.rounded_rect((276, 15, 280, 22), 2, SECONDARY)
    canvas.path([("M", 273.5, 19), ("C", 273, 27, 283, 27, 282.5, 19)], stroke=SECONDARY, width=1)
    canvas.path([("M", 278, 25), ("L", 278, 28)], stroke=SECONDARY, width=1)


def draw_interface(canvas):
    draw_status(canvas)
    canvas.text((18, 108), "10:08", 66, INK, "clock")
    canvas.text((22, 134), "10月6日  星期二", 13, SECONDARY)
    canvas.rounded_rect((16, 174, 304, 218), 12, "#E3ECD9")
    draw_sun(canvas, 39, 196)
    canvas.text((60, 202), "晴", 15, INK)
    canvas.text((120, 204), "24°", 29, INK, "clock")
    canvas.text((212, 201), "18° / 26°", 13, SECONDARY, "clock")
    # Five top-level pages: home / album / message / countdown / apps.
    canvas.rounded_rect((128, 227, 138, 231), 2, INK)
    for x in (147, 160, 173, 186):
        canvas.ellipse((x-2, 227, x+2, 231), "#C1CFB7")


def rgb565_preview(image):
    """Approximate the actual LCD's color precision without changing layout."""
    result = Image.new("RGB", image.size)
    rgb = image.convert("RGB")
    pixels = rgb.get_flattened_data() if hasattr(rgb, "get_flattened_data") else rgb.getdata()
    result.putdata([((r >> 3)*255//31, (g >> 2)*255//63, (b >> 3)*255//31) for r, g, b in pixels])
    return result


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    for path, _ in FONTS.values():
        if not path.is_file():
            raise FileNotFoundError(f"Required font not found: {path}")

    canvas = Canvas(CREAM)
    draw_background(canvas)
    draw_cat(canvas)
    canvas.bitmap().convert("RGB").save(ASSETS / "home-v1-artwork-320x240.png")
    canvas.save_svg(ASSETS / "home-v1-artwork.svg")

    character = Canvas()
    draw_cat(character)
    character.bitmap().crop((202, 34, 311, 168)).save(ASSETS / "xiaohei-study-v1.png")
    character.save_svg(ASSETS / "xiaohei-study-v1.svg")

    draw_interface(canvas)
    raw = canvas.bitmap().convert("RGB")
    raw.save(OUT / "home-v1-320x240.png")
    raw.resize((WIDTH*3, HEIGHT*3), Image.Resampling.NEAREST).save(OUT / "home-v1-preview-3x.png")
    rgb565_preview(raw).save(OUT / "home-v1-rgb565-preview.png")
    canvas.save_svg(OUT / "home-v1-320x240.svg")

    spec = {
        "version": "home-v1",
        "canvas": {"width": WIDTH, "height": HEIGHT, "orientation": "landscape"},
        "sample_data": {"clock": "10:08", "date": "2026-10-06", "weather": "晴", "temperature_c": 24, "low_c": 18, "high_c": 26},
        "colors": {"background": CREAM, "primary": INK, "secondary": SECONDARY, "weather_panel": "#E3ECD9"},
        "font_sizes_design_px": {"clock": 66, "date": 13, "weather": 15, "temperature": 29, "range": 13},
        "artwork": "Locally drawn character study; not official artwork or an AI-generated asset.",
        "preview": "3x nearest-neighbor enlargement of the native bitmap, not additional screen detail.",
        "touch_regions": [{"action": "open_weather", "x": 16, "y": 174, "width": 288, "height": 44}],
        "page_navigation": {"right_swipe": "home -> album -> message -> countdown -> apps -> home", "left_swipe": "reverse"},
        "voice": "Wake-word detection interrupts the UI and enters the existing Xiaozhi conversation flow; requires firmware integration.",
        "firmware_asset_rgb565_bytes": WIDTH*HEIGHT*2,
        "hardware_verified": False,
    }
    (OUT / "home-v1-layout.json").write_text(json.dumps(spec, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print("Rendered native PNG, 3x pixel preview, editable SVG, RGB565 preview, artwork layers and layout spec.")
    print(OUT)


if __name__ == "__main__":
    main()
