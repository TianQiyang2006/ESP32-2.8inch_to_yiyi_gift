"""Course-table (schedule page) font-option previews, 2026-10-28.

Draws three variants of page 5 at 3x using the firmware palette/geometry:
  A: same grid, bigger course text (8/9px adaptive line count) + header/detail bump
  B: 8 visible periods + vertical scroll, 10px course text
  C: day view (large list), week grid becomes a secondary mode
Also saves a 2x2 comparison sheet with the current device screenshot.

Run: python design/schedule-fonts/render_options.py
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "design" / "schedule-fonts"
OUT.mkdir(parents=True, exist_ok=True)
FB = ROOT / "build" / "font-source" / "open" / "NotoSansSC-Bold.ttf"
FR = ROOT / "build" / "font-source" / "open" / "NotoSansSC-Regular.ttf"

# firmware palette (screen_schedule.cpp)
BG = (251, 250, 242)
HEADER = (111, 154, 125)
HEADER_INK = (255, 255, 255)
HEADER_SUB = (235, 245, 233)
HEADER_DARK = (60, 88, 68)
LINE = (229, 228, 217)
TODAY_TINT = (246, 251, 245)
TODAY_PILL = (232, 242, 218)
MUTED = (130, 138, 124)

COURSE_COLORS = {
    "peach": ((246, 227, 201), (107, 74, 47)),
    "yellow": ((245, 239, 184), (107, 93, 31)),
    "green": ((207, 232, 200), (46, 82, 51)),
    "pink": ((243, 212, 216), (110, 47, 58)),
    "mint": ((214, 237, 227), (35, 82, 71)),
    "salmon": ((246, 211, 196), (110, 53, 36)),
    "cream": ((247, 240, 210), (110, 90, 31)),
    "blue": ((216, 230, 243), (39, 74, 107)),
    "purple": ((230, 221, 240), (74, 51, 99)),
}

DAY_NAMES = ["一", "二", "三", "四", "五", "六", "日"]
DAYS = [26, 27, 28, 29, 30, 31, 1]
# (day, start period, row span, name, palette)
COURSES = [
    (1, 1, 2, "数据结构与算法", "peach"), (1, 5, 2, "数据结构与算法", "peach"),
    (1, 7, 2, "大学英语（3）", "blue"),
    (2, 1, 2, "计算机系统结构", "yellow"), (2, 3, 2, "计算机系统结构", "yellow"),
    (2, 5, 2, "马克思主义基本原理", "green"), (2, 7, 2, "计算机系统结构", "yellow"),
    (2, 9, 2, "编程能力提升", "salmon"),
    (3, 1, 2, "网络攻击与防御", "pink"), (3, 3, 2, "马克思主义基本原理", "green"),
    (3, 5, 2, "机器学习技术与实践", "mint"), (3, 7, 2, "体育（3）", "purple"),
    (4, 1, 2, "编程能力提升", "salmon"), (4, 3, 2, "数字电路", "cream"),
    (4, 5, 2, "数字电路", "cream"), (4, 7, 2, "大学生健康素养", "blue"),
    (5, 3, 2, "信息安全导论", "yellow"), (5, 7, 2, "国家安全教育", "blue"),
]

GUTTER, COL_W = 12, 44
ROWS = 10


def f(path, px):
    return ImageFont.truetype(str(path), px)


def draw_header(d, S, title="第 9 周", date="10/26 - 10/30", count="38 节", x_offset=0):
    d.rectangle([0, 0, 320 * S, 32 * S], fill=HEADER)
    d.text((8 * S + x_offset, 10 * S), title, font=f(FB, 11 * S), fill=HEADER_INK)
    tw = d.textlength(title, font=f(FB, 11 * S))
    d.text((8 * S + tw + 8 * S + x_offset, 11 * S), date, font=f(FB, 8 * S), fill=HEADER_SUB)
    d.text((154 * S + x_offset, 11 * S), count, font=f(FB, 8 * S), fill=HEADER_SUB)
    # 本周 pill + two arrow circles
    d.rounded_rectangle([204 * S + x_offset, 8 * S, 252 * S + x_offset, 25 * S], radius=8 * S,
                        outline=HEADER_INK, width=max(1, round(1.2 * S)))
    d.text((228 * S + x_offset, 16.5 * S), "本周", font=f(FB, 7 * S), fill=HEADER_INK, anchor="mm")
    for cx, arrow in ((270, "‹"), (296, "›")):
        d.ellipse([(cx - 11) * S + x_offset, 5 * S, (cx + 11) * S + x_offset, 27 * S],
                  outline=HEADER_INK, width=max(1, round(1.2 * S)))
        d.text((cx * S + x_offset, 16 * S), arrow, font=f(FB, 12 * S), fill=HEADER_INK, anchor="mm")


def draw_week_row(d, S):
    d.rectangle([0, 32 * S, 320 * S, 50 * S], fill=BG)
    for col in range(7):
        x = GUTTER + col * COL_W
        today = col == 0  # Monday in this fixture
        if today:
            d.rounded_rectangle([(x + 1) * S, 32 * S, (x + COL_W - 1) * S, 49 * S],
                                radius=5 * S, fill=TODAY_PILL)
        color = HEADER_DARK if today else MUTED
        d.text(((x + COL_W / 2) * S, 37 * S), DAY_NAMES[col], font=f(FB, 7 * S),
               fill=color, anchor="mm")
        d.text(((x + COL_W / 2) * S, 44 * S), str(DAYS[col]), font=f(FB, 6 * S),
               fill=color, anchor="mm")


def draw_grid_base(d, S, rows=ROWS, grid_bot=238):
    grid_top = 50
    row_h = (grid_bot - grid_top) / float(rows)
    for r in range(rows + 1):
        y = grid_top + row_h * r
        d.line([GUTTER * S, y * S, 320 * S, y * S], fill=LINE, width=max(1, round(S * 0.8)))
    for c in range(8):
        x = GUTTER + c * COL_W
        d.line([x * S, grid_top * S, x * S, grid_bot * S], fill=LINE, width=max(1, round(S * 0.8)))
    for r in range(1, rows + 1):
        y = grid_top + row_h * (r - 1) + row_h / 2
        d.text((7 * S, y * S), str(r), font=f(FB, 6 * S), fill=MUTED, anchor="mm")
    return grid_top, row_h


def clip_line(d, text, fnt, max_px):
    if d.textlength(text, font=fnt) <= max_px:
        return text
    while text and d.textlength(text + "…", font=fnt) > max_px:
        text = text[:-1]
    return text + "…"


def draw_courses(d, S, option, grid_top=50, grid_bot=238, rows=ROWS):
    row_h = (grid_bot - grid_top) / float(rows)
    for day, start, span, name, key in COURSES:
        if start > rows:
            continue
        span_vis = min(span, rows - start + 1)
        if span_vis <= 0:
            continue
        x = GUTTER + (day - 1) * COL_W + 1
        y = grid_top + row_h * (start - 1) + 1
        w = COL_W - 2
        h = row_h * span_vis - 2
        fill, ink = COURSE_COLORS[key]
        d.rounded_rectangle([x * S, y * S, (x + w) * S, (y + h) * S], radius=4 * S, fill=fill)
        # text
        chars = list(name)
        cpl = 4 if option in ("A", "C") else 5
        if option == "A":
            line_h = 10
            font_px = 9 if h > 24 else 8
            capacity = max(1, int((h - 2) / line_h))
            need = (len(chars) + cpl - 1) // cpl
            lines = min(capacity, need)
            texts = []
            for i in range(lines):
                part = "".join(chars[i * cpl:(i + 1) * cpl])
                if i == lines - 1 and len(chars) > lines * cpl:
                    part += "…"
                texts.append(clip_line(d, part, f(FB, font_px * S), (w - 4) * S))
            fnt = f(FB, font_px * S)
            total_h = len(texts) * line_h
            ty = y + (h - total_h) / 2 + line_h / 2 + 1
            for i, line in enumerate(texts):
                d.text(((x + 2) * S, (ty + i * line_h) * S), line, font=fnt, fill=ink, anchor="lm")
        elif option == "B":
            # 10 px, up to 3 lines of 4 chars; same algorithm, bigger
            line_h = 12
            capacity = max(1, int((h - 3) / line_h))
            need = (len(chars) + cpl - 1) // cpl
            lines = min(capacity, need)
            texts = []
            for i in range(lines):
                part = "".join(chars[i * cpl:(i + 1) * cpl])
                if i == lines - 1 and len(chars) > lines * cpl:
                    part += "…"
                texts.append(clip_line(d, part, f(FB, 10 * S), (w - 4) * S))
            total_h = len(texts) * line_h
            ty = y + (h - total_h) / 2 + line_h / 2
            for i, line in enumerate(texts):
                d.text(((x + 2) * S, (ty + i * line_h) * S), line, font=f(FB, 10 * S), fill=ink, anchor="lm")
        else:
            # current 7/6px, 5 chars per line, 9 px line height
            line_h = 9
            capacity = max(1, int((h - 2) / line_h))
            need = (len(chars) + cpl - 1) // cpl
            lines = min(capacity, need)
            total_h = lines * line_h
            ty = y + (h - total_h) / 2 + line_h / 2
            for i in range(lines):
                part = "".join(chars[i * cpl:(i + 1) * cpl])
                if i == lines - 1 and len(chars) > lines * cpl:
                    part += "…"
                px = 7 if i == 0 else 6
                line = clip_line(d, part, f(FB, px * S), (w - 4) * S)
                d.text(((x + 2) * S, (ty + i * line_h) * S), line, font=f(FB, px * S), fill=ink, anchor="lm")


def page_current(S=3):
    src = ROOT / "build" / "device-golden" / "baseline" / "page-5-schedule.png"
    img = Image.open(src).convert("RGB")
    return img.resize((320 * S, 240 * S), Image.NEAREST)


def page_A(S=3):
    img = Image.new("RGB", (320 * S, 240 * S), BG)
    d = ImageDraw.Draw(img)
    draw_header(d, S)
    draw_week_row(d, S)
    draw_grid_base(d, S)
    draw_courses(d, S, "A")
    return img


def page_B(S=3):
    img = Image.new("RGB", (320 * S, 240 * S), BG)
    d = ImageDraw.Draw(img)
    draw_header(d, S)
    draw_week_row(d, S)
    visible = 8
    draw_grid_base(d, S, rows=visible, grid_bot=238)
    draw_courses(d, S, "B", rows=visible, grid_bot=238)
    # scrollbar hint on the right
    d.rounded_rectangle([314 * S, 56 * S, 317 * S, 180 * S], radius=2 * S, fill=(222, 228, 214))
    d.rounded_rectangle([314 * S, 56 * S, 317 * S, 130 * S], radius=2 * S, fill=(150, 168, 146))
    return img


def page_C(S=3):
    img = Image.new("RGB", (320 * S, 240 * S), BG)
    d = ImageDraw.Draw(img)
    # header: selected day title
    draw_header(d, S, title="周二", date="10/27", count="5 门")
    # replace the "本周" pill label with a "周视图" switch hint for the day layout
    d.rectangle([204 * S, 8 * S, 252 * S, 25 * S], fill=HEADER)
    d.rounded_rectangle([204 * S, 8 * S, 252 * S, 25 * S], radius=8 * S,
                        outline=HEADER_INK, width=max(1, round(1.2 * S)))
    d.text((228 * S, 16.5 * S), "周视图", font=f(FB, 7 * S), fill=HEADER_INK, anchor="mm")
    # day tabs
    d.rectangle([0, 32 * S, 320 * S, 50 * S], fill=BG)
    for col in range(7):
        x = GUTTER + col * COL_W
        sel = col == 1
        if sel:
            d.rounded_rectangle([(x + 1) * S, 32 * S, (x + COL_W - 1) * S, 49 * S],
                                radius=5 * S, fill=TODAY_PILL)
        color = HEADER_DARK if sel else MUTED
        d.text(((x + COL_W / 2) * S, 37 * S), DAY_NAMES[col], font=f(FB, 7 * S), fill=color, anchor="mm")
        d.text(((x + COL_W / 2) * S, 44 * S), str(DAYS[col]), font=f(FB, 6 * S), fill=color, anchor="mm")
    # list of that day's courses (large font)
    items = [(1, 2, "计算机系统结构", "08:00 - 09:35 · 教室未安排", "yellow"),
             (3, 4, "计算机系统结构", "10:05 - 11:40 · 教室未安排", "yellow"),
             (5, 6, "马克思主义基本原理", "14:00 - 15:35 · 教室未安排", "green"),
             (7, 8, "计算机系统结构", "16:05 - 17:40 · 教室未安排", "yellow"),
             (9, 10, "编程能力提升", "19:00 - 20:35 · 教室未安排", "salmon")]
    y = 54
    for start, end, name, sub, key in items:
        fill, ink = COURSE_COLORS[key]
        d.rounded_rectangle([8 * S, y * S, 312 * S, (y + 33) * S], radius=6 * S, fill=fill)
        d.text((14 * S, (y + 13) * S), "%d-%d节" % (start, end), font=f(FB, 8 * S), fill=ink)
        d.text((56 * S, (y + 4) * S), name, font=f(FB, 12 * S), fill=ink)
        d.text((56 * S, (y + 20) * S), sub, font=f(FR, 7 * S), fill=ink)
        y += 36
    # hint
    d.text((160 * S, 235 * S), "左右滑切天 · 点课程看详情", font=f(FR, 6 * S), fill=MUTED, anchor="mm")
    return img


def captioned(img, text, pad=14, fp=26):
    strip = Image.new("RGB", (img.width, pad * 2 + fp), (255, 255, 255))
    d = ImageDraw.Draw(strip)
    d.text((pad, pad), text, font=f(FR, fp), fill=(60, 60, 60))
    out = Image.new("RGB", (img.width, img.height + strip.height), (255, 255, 255))
    out.paste(img, (0, 0))
    out.paste(strip, (0, img.height))
    return out


def main():
    current = page_current(3)
    a = page_A(3)
    b = page_B(3)
    c = page_C(3)
    current.save(OUT / "current-3x.png")
    a.save(OUT / "option-A-bigger-cell-text-3x.png")
    b.save(OUT / "option-B-8-periods-scroll-3x.png")
    c.save(OUT / "option-C-day-view-3x.png")

    # comparison sheet: current / A / C (one row at 2x)
    tiles = [
        (page_current(2), "现状（设备实拍）：课程名 6–7px"),
        (page_A(2), "方案 A：格子内 8/9px（布局不变）"),
        (page_C(2), "方案 C：日视图大列表，12px"),
    ]
    gap, label_h = 18, 34
    tw, th = 320 * 2, 240 * 2
    sheet = Image.new("RGB", (tw * len(tiles) + gap * (len(tiles) + 1), th + label_h + gap * 2),
                      (246, 246, 242))
    d = ImageDraw.Draw(sheet)
    for i, (img, label) in enumerate(tiles):
        cx = gap + i * (tw + gap)
        cy = gap
        sheet.paste(img, (cx, cy))
        d.text((cx + 4, cy + th + 8), label, font=f(FR, 22), fill=(50, 50, 50))
    sheet.save(OUT / "comparison-2x.png")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
