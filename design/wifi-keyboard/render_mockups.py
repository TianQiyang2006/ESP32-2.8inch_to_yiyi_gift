"""Render Wi-Fi keyboard layout mockups from firmware truth.

Reads:  build/review/device-allb/wifi-keyboard.png (real 320x240 device screenshot)
Writes: design/wifi-keyboard/*.png

Geometry/fonts/colors are taken from hardware/arduino/yiyi_desk/src/wifi_setup.cpp
(matrix at 0,134 320x106, 4 rows x 10 cols, pad 2/2/2, Montserrat 14, key border
#D8E3CC, pressed #E6EFDB) and src/ui_widgets.cpp (Ui::Ink/Green/Paper colors,
command buttons 72x26 at y=92, textareas 258x26 at x=52, status label y=118).

Run:  python design/wifi-keyboard/render_mockups.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "design" / "wifi-keyboard"
OUT.mkdir(parents=True, exist_ok=True)

SCREENSHOT = ROOT / "build" / "review" / "device-allb" / "wifi-keyboard.png"
FONT_CN = ROOT / "build" / "font-source" / "open" / "NotoSansSC-Regular.ttf"
FONT_CN_B = ROOT / "build" / "font-source" / "open" / "NotoSansSC-Bold.ttf"
FONT_KEY = ROOT / "lvgl_ui_project" / "fonts" / "Montserrat-Medium.ttf"

# ---- palette (firmware truth) ------------------------------------------------
PAPER = (246, 247, 239)        # Ui::Paper 0xF6F7EF
INK = (41, 76, 61)             # Ui::Ink   0x294C3D
GREEN = (220, 232, 212)        # Ui::Green 0xDCE8D4
MUTED = (99, 127, 104)         # Ui::Muted 0x637F68
BACK = (57, 70, 55)            # header ink 0x394637
KEY_BORDER = (216, 227, 204)   # 0xD8E3CC
KEY_PRESSED = (230, 239, 219)  # 0xE6EFDB
STATUS_RED = (169, 67, 48)     # 0xA94330
BAT_BLUE = (62, 143, 211)      # BatBlue 0x3E8FD3
TEXTAREA_BG = (255, 255, 255)

# ---- key maps (firmware truth; symbols page is the proposed redesign) --------
LOWER = [
    list("1234567890"),
    list("qwertyuiop"),
    list("asdfghjkl;"),
    list("zxcvbnm.-_"),
]
UPPER = [
    list("1234567890"),
    list("QWERTYUIOP"),
    list("ASDFGHJKL;"),
    list("ZXCVBNM.-_"),
]
# proposed symbols page: rows 1-3 stay the same char sets, row 4 becomes a wide
# SPACE + quick-access row of the symbols Wi-Fi passwords use most.
SYMBOLS = [
    list("!@#$%^&*()"),
    list("-_=+[]{}.,"),
    list("/?:;<>~`|'"),
    [("SPACE", 3), ("@", 1), (".", 1), ("-", 1), ("_", 1), (",", 1), (":", 1), (";", 1)],
]
CURRENT_SYMBOLS = [
    list("!@#$%^&*()"),
    list("-_=+[]{}.,"),
    list("/?:;<>~`|'"),
    [" "] + list("*+=()[]{}"),  # firmware today: blank space key + duplicated symbols
]

# ---- LVGL 8.4 btnmatrix geometry (see lv_btnmatrix.c refresh_btnm) ----------
MATRIX_X, MATRIX_Y, MATRIX_W, MATRIX_H = 0, 134, 320, 106
PAD = 2
PAD_ROW = 2
PAD_COL = 2
ROWS = 4


def row_rects(row_index, units):
    """Return list of (x1, y1, x2, y2, label) in 320x240 page coords."""
    content_w = MATRIX_W - 2 * PAD
    content_h = MATRIX_H - 2 * PAD
    no_gap_h = content_h - PAD_ROW * (ROWS - 1)
    by1 = PAD + (no_gap_h * row_index) // ROWS + row_index * PAD_ROW
    by2 = PAD + (no_gap_h * (row_index + 1)) // ROWS + row_index * PAD_ROW - 1
    y1, y2 = MATRIX_Y + by1, MATRIX_Y + by2
    total_units = sum(u for _, u in units)
    no_gap_w = content_w - PAD_COL * (len(units) - 1)
    out, u = [], 0
    for i, (label, width_units) in enumerate(units):
        x1 = MATRIX_X + PAD + (no_gap_w * u) // total_units + i * PAD_COL
        x2 = MATRIX_X + PAD + (no_gap_w * (u + width_units)) // total_units + i * PAD_COL - 1
        out.append((x1, y1, x2, y2, label))
        u += width_units
    return out


def normalize_rows(rows):
    out = []
    for row in rows:
        if row and isinstance(row[0], tuple):
            out.append(row)
        else:
            out.append([(ch, 1) for ch in row])
    return out


# ---- drawing ----------------------------------------------------------------
def font(path, px):
    return ImageFont.truetype(str(path), px)


def draw_text(d, xy, text, fnt, fill, anchor="la"):
    d.text(xy, text, font=fnt, fill=fill, anchor=anchor)


def draw_page(scale=3, keys=LOWER, mode="小写", active="password",
              status="已连接：qingyu", focus_ring=False):
    S = scale
    img = Image.new("RGB", (320 * S, 240 * S), PAPER)
    d = ImageDraw.Draw(img)
    f_cn = font(FONT_CN, 14 * S)
    f_cn_b = font(FONT_CN_B, 10 * S)
    f_key = font(FONT_KEY, 14 * S)
    f_key_small = font(FONT_KEY, 11 * S)

    def R(x1, y1, x2, y2, radius=0, fill=None, outline=None, width=1):
        d.rounded_rectangle([x1 * S, y1 * S, x2 * S, y2 * S], radius=radius * S,
                            fill=fill, outline=outline, width=max(1, round(width * S)))

    # header (matches device screenshot geometry)
    d.line([(17.5 * S, 10 * S), (10 * S, 18.5 * S), (17.5 * S, 27 * S)],
           fill=BACK, width=round(2.0 * S), joint="curve")
    draw_text(d, (54 * S, 8 * S), "Wi-Fi 配网", f_cn, BACK)
    cx, cy = 226.5 * S, 21.5 * S  # wifi glyph: device x218..235 y11..22
    for r, w in ((8.2, 1.7), (5.6, 1.7), (3.1, 1.7)):
        d.arc([cx - r * S, cy - r * S, cx + r * S, cy + r * S], 205, 335,
              fill=BACK, width=max(1, round(w * S)))
    d.ellipse([cx - 1.4 * S, cy - 1.4 * S, cx + 1.4 * S, cy + 1.4 * S], fill=BACK)
    R(242, 11.5, 263, 22.5, radius=2, fill=(241, 245, 234), outline=BAT_BLUE, width=1.2)
    R(263.6, 14.5, 266.5, 19.5, radius=0.8, fill=BAT_BLUE)
    for i in range(3):
        d.rectangle([(244.5 + i * 6) * S, 13.6 * S, (249 + i * 6) * S, 20.5 * S], fill=BAT_BLUE)
    draw_text(d, (271 * S, 11 * S), "100 %", font(FONT_KEY, 9.5 * S), BAT_BLUE)

    # name / password fields
    draw_text(d, (10 * S, 38 * S), "名称", f_cn, INK)
    draw_text(d, (10 * S, 67 * S), "密码", f_cn, INK)
    R(52, 34, 310, 60, radius=4, fill=TEXTAREA_BG, outline=KEY_BORDER, width=1.2)
    R(52, 63, 310, 89, radius=4, fill=TEXTAREA_BG, outline=KEY_BORDER, width=1.2)
    if focus_ring:
        target = (52, 63, 310, 89) if active == "password" else (52, 34, 310, 60)
        R(*target, radius=4, fill=None, outline=(127, 169, 127), width=1.8)

    # command row (退格 / mode / 保存 / 返回) — unchanged geometry
    for label, x in (("退格", 10), (mode, 88), ("保存", 166), ("返回", 244)):
        R(x, 92, x + 72, 118, radius=5, fill=GREEN)
        bbox = d.textbbox((0, 0), label, font=f_cn)
        d.text(((x + 36) * S - (bbox[2] - bbox[0]) / 2,
                (92 + 13) * S - (bbox[3] - bbox[1]) / 2 - bbox[1]),
               label, font=f_cn, fill=INK)

    draw_text(d, (10 * S, 118 * S), status, f_cn_b, STATUS_RED)

    # keyboard matrix
    for r, units in enumerate(normalize_rows(keys)):
        for (x1, y1, x2, y2, label) in row_rects(r, units):
            R(x1, y1, x2, y2, radius=4, fill=TEXTAREA_BG, outline=KEY_BORDER, width=1.1)
            if not label.strip():
                continue
            f = f_key_small if label == "SPACE" else f_key
            # anchor="mm" keeps glyph placement consistent with LVGL glyph-box centering
            d.text(((x1 + x2) / 2 * S, (y1 + y2) / 2 * S), label, font=f, fill=INK, anchor="mm")
    return img


def lift(img, scale=3):
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)


def captioned(img, text, pad=18, fnt_px=30):
    strip = Image.new("RGB", (img.width, pad * 2 + fnt_px), (255, 255, 255))
    d = ImageDraw.Draw(strip)
    d.text((pad, pad), text, font=font(FONT_CN, fnt_px), fill=(60, 60, 60))
    out = Image.new("RGB", (img.width, img.height + strip.height), (255, 255, 255))
    out.paste(img, (0, 0))
    out.paste(strip, (0, img.height))
    return out


def main():
    # 1) current state: real device screenshot, 3x
    cur = Image.open(SCREENSHOT).convert("RGB")
    lift(cur, 3).save(OUT / "wifi-keyboard-current-3x.png")

    # 2) proposed full pages, 3x
    draw_page(3, LOWER, "小写", "password", "已连接：qingyu", focus_ring=True) \
        .save(OUT / "wifi-keyboard-proposed-lower-3x.png")
    draw_page(3, SYMBOLS, "符号", "password", "已连接：qingyu", focus_ring=True) \
        .save(OUT / "wifi-keyboard-proposed-symbols-3x.png")

    # 3) three-page sheet (keyboard region only, 3x each)
    crops = []
    for keys, mode, caption in (
            (LOWER, "小写", "① 小写页（键位保持不变）"),
            (UPPER, "大写", "② 大写页（键位保持不变）"),
            (SYMBOLS, "符号", "③ 符号页（新键位：SPACE 占 3 格 + 常用符号快捷行）")):
        page = draw_page(3, keys, mode, "password", "已连接：qingyu", focus_ring=False)
        crops.append(captioned(page.crop((0, MATRIX_Y * 3, 320 * 3, 240 * 3)), caption))
    gap = 26
    sheet = Image.new("RGB", (320 * 3, sum(c.height for c in crops) + gap * 2), (235, 235, 235))
    y = 0
    for c in crops:
        sheet.paste(c, (0, y))
        y += c.height + gap
    sheet.save(OUT / "wifi-keyboard-pages-3x.png")

    # 4) before/after comparison (2x)
    before = lift(cur, 2)
    after = draw_page(2, LOWER, "小写", "password", "已连接：qingyu", focus_ring=True)
    gap = 30
    sheet = Image.new("RGB", (before.width + after.width + gap * 3, before.height + 120), (255, 255, 255))
    sheet.paste(before, (gap, 80))
    sheet.paste(after, (gap * 2 + before.width, 80))
    d = ImageDraw.Draw(sheet)
    f_cap = font(FONT_CN, 32)
    d.text((gap, 24), "现状（设备实拍 ×2）", font=f_cap, fill=(60, 60, 60))
    d.text((gap * 2 + before.width, 24), "设计稿 K2（页面布局不变 ×2）", font=f_cap, fill=(60, 60, 60))
    sheet.save(OUT / "wifi-keyboard-before-after-2x.png")

    # 5) annotated spec sheet
    page = draw_page(2, LOWER, "小写", "password", "已连接：qingyu", focus_ring=True)
    W, H = 1600, 700
    spec = Image.new("RGB", (W, H), (250, 250, 246))
    spec.paste(page, (30, 110))
    d = ImageDraw.Draw(spec)
    d.text((30, 24), "Wi-Fi 配网 · 键盘页规格稿（320×240，图中 ×2）", font=font(FONT_CN_B, 34), fill=(40, 40, 40))
    f_num = font(FONT_CN_B, 22)
    f_leg = font(FONT_CN, 20)
    f_leg_b = font(FONT_CN_B, 20)
    callouts = [(54, 8), (52, 34), (52, 63), (10, 118), (10, 92), (2, 136), (2, 214)]
    for i, (x, y) in enumerate(callouts, 1):
        cx, cy = 30 + x * 2, 110 + y * 2
        d.ellipse([cx - 13, cy - 13, cx + 13, cy + 13], fill=(200, 70, 50))
        d.text((cx, cy), str(i), font=f_num, fill=(255, 255, 255), anchor="mm")

    def wrap(text, fnt, max_w, seps=" · "):
        tokens = [part + seps for part in text.split(seps)]
        lines, cur = [], ""
        for tok in tokens:
            if d.textlength(cur + tok, font=fnt) <= max_w:
                cur += tok
            else:
                if cur:
                    lines.append(cur.rstrip(seps).strip())
                cur = tok
        if cur:
            lines.append(cur.rstrip(seps).strip())
        return lines

    legend = [
        ("①", "标题栏：返回 (8,6,36,32) · 标题 (54,8) · 状态图标 (220,10) / 电池 (243,11)"),
        ("②", "名称标签 (10,38) + 输入框 (52,34,258,26)：白底 #FFFFFF、边 #D8E3CC"),
        ("③", "密码标签 (10,67) + 输入框 (52,63,258,26)，聚焦框 #7FA97F（设计稿新增）"),
        ("④", "状态行 (10,118)：fd_sans_bold_10、#A94330、高约 14px"),
        ("⑤", "命令行 y=92 h=26：退格(10)/小写(88)/保存(166)/返回(244)，均 72×26"),
        ("⑥", "键盘区 (0,134,320,106)：4 行×10 列，pad 2/2/2，键约 29×24，圆角 4"),
        ("⑦", "符号页第 4 行：SPACE 占 3 格（约 90px）+ 7 个常用符号，需 ctrl_map 宽度"),
    ]
    x, y = 720, 110
    for mark, text in legend:
        d.text((x, y), mark, font=f_leg_b, fill=(200, 70, 50))
        for line in wrap(text, f_leg, 830):
            d.text((x + 36, y), line, font=f_leg, fill=(55, 55, 55))
            y += 30
        y += 14
    y += 8
    for line in wrap("配色：Paper #F6F7EF · Ink #294C3D · 按钮 #DCE8D4 · 键边 #D8E3CC · "
                     "键按下 #E6EFDB · 状态 #A94330 · 聚焦 #7FA97F", f_leg, 830):
        d.text((x, y), line, font=f_leg, fill=(90, 90, 90))
        y += 30
    y += 8
    for line in wrap("字体：标题/标签/按钮 fd_sans_14 · 键盘 Montserrat 14 · 状态 fd_sans_bold_10", f_leg, 830):
        d.text((x, y), line, font=f_leg, fill=(90, 90, 90))
        y += 30
    spec.save(OUT / "wifi-keyboard-spec-2x.png")

    px = cur.load()
    print("screenshot colors: bg", px[5, 130], "keyfill", px[10, 140],
          "button", px[20, 105], "status", px[20, 124])


if __name__ == "__main__":
    main()
