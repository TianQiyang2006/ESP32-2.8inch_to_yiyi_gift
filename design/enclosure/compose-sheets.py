"""Compose the locally rendered models into reviewable exterior proposal sheets."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "renders"
PAPER = (245, 242, 235)
INK = "#254936"
MUTED = "#7c8775"
LINE = "#d8decd"
REGULAR = "C:/Windows/Fonts/msyh.ttc"
BOLD = "C:/Windows/Fonts/msyhbd.ttc"


def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else REGULAR, size)


def text(draw, xy, value, size=24, fill=INK, bold=False):
    draw.text(xy, value, font=font(size, bold), fill=fill)


def tracking(draw, xy, value, size=16, spacing=4, fill=MUTED):
    x, y = xy
    f = font(size)
    for char in value:
        draw.text((x, y), char, font=f, fill=fill)
        x += draw.textlength(char, font=f) + spacing


def product(sheet, name, xy, width):
    image = Image.open(OUT / name).convert("RGB")
    image = image.crop((125, 145, 1375, 1375))
    height = round(width * image.height / image.width)
    image = image.resize((width, height), Image.Resampling.LANCZOS)
    sheet.paste(image, xy)


def comparison():
    sheet = Image.new("RGB", (2400, 1550), PAPER)
    d = ImageDraw.Draw(sheet)
    tracking(d, (92, 62), "A LITTLE COMPANION, EVERY DAY.", 17, 4)
    text(d, (88, 112), "给依依的，一只桌面小黑", 62, bold=True)
    text(d, (94, 208), "2.8 英寸横屏  /  三款外观方向  /  先看模样，再定做法", 25, MUTED)
    d.line((94, 282, 2306, 282), fill=LINE, width=2)
    concepts = [
        ("A", "小黑抱抱", "圆润墨黑 × 叶绿耳朵", "小猫爪落地，像一只陪伴的小黑。", "约 104 × 48 × 100 mm", "a-hero.png"),
        ("B", "像素小黑", "阶梯猫耳 × 积木轮廓", "保留亲手拼搭的趣味和礼物感。", "约 112 × 48 × 104 mm", "b-hero.png"),
        ("C", "森林信笺", "奶油相框 × 森林绿底座", "把时间、天气和照片装进一封信。", "约 110 × 55 × 88 mm", "c-hero.png"),
    ]
    for index, (letter, name, mood, description, size, image) in enumerate(concepts):
        x = 94 + index * 760
        if index:
            d.line((x - 30, 332, x - 30, 1328), fill=LINE, width=1)
        text(d, (x, 326), letter, 54, "#78906b")
        text(d, (x + 65, 342), name, 35, bold=True)
        if index == 0:
            d.rounded_rectangle((x + 232, 344, x + 310, 385), radius=9, fill="#718b64")
            text(d, (x + 245, 349), "推荐", 22, "#ffffff")
        product(sheet, image, (x - 15, 426), 720)
        text(d, (x + 9, 1163), mood, 27, bold=True)
        text(d, (x + 9, 1214), description, 23, MUTED)
        text(d, (x + 9, 1289), size, 26, "#4e6b45")
        text(d, (x + 9, 1333), "建议体量 · 宽 × 深 × 高", 18, "#939b88")
    d.line((94, 1421, 2306, 1421), fill=LINE, width=2)
    text(d, (94, 1460), "外观概念 v1 · 屏幕按实际显示区比例呈现；结构与制作方式待细化。", 20, MUTED)
    tracking(d, (1900, 1464), "2026.10 / FOR YIYI", 15, 3)
    sheet.save(OUT / "concept-comparison.png")


def detail_a():
    sheet = Image.new("RGB", (2400, 1550), PAPER)
    d = ImageDraw.Draw(sheet)
    tracking(d, (94, 63), "CONCEPT A / THE LITTLE BLACK CAT", 17, 4)
    text(d, (88, 110), "小黑抱抱", 64, bold=True)
    text(d, (425, 146), "圆润的轮廓，安静的陪伴。", 27, MUTED)
    text(d, (94, 211), "墨黑哑光机身  /  叶绿内耳  /  猫爪支脚  /  约 104 × 48 × 100 mm", 23, MUTED)
    d.line((94, 279, 2306, 279), fill=LINE, width=2)
    product(sheet, "a-hero.png", (32, 317), 1120)
    text(d, (94, 1398), "01 / 斜视", 21, "#6d8362")
    for name, xy, label in [
        ("a-front.png", (1225, 318), "02 / 正面 · 屏幕与装饰分开"),
        ("a-back.png", (1795, 318), "03 / 背面 · 后盖与声孔示意"),
        ("a-side.png", (1225, 883), "04 / 侧面 · USB 开口示意"),
    ]:
        product(sheet, name, xy, 515)
        text(d, (xy[0] + 26, xy[1] + 510), label, 20, "#6d8362")
    text(d, (1840, 1060), "把陪伴，留在桌上。", 30, bold=True)
    for i, line in enumerate(["两只浅色猫眼，亮屏熄屏都有表情。", "小猫爪让机身轻轻落在桌面。", "侧面供电，背后收纳与发声。", "屏幕仍是熟悉的小小森林。"]):
        text(d, (1840, 1124 + i * 47), line, 21, MUTED)
    d.line((94, 1464, 2306, 1464), fill=LINE, width=2)
    text(d, (94, 1495), "外观提案 / 接口位置、固定方式和内部声腔将在选定制作方式后按实物细化。", 18, MUTED)
    tracking(d, (2010, 1497), "FOR YIYI / 01", 14, 3)
    sheet.save(OUT / "a-design-sheet.png")


if __name__ == "__main__":
    comparison()
    detail_a()
    print("Saved concept-comparison.png and a-design-sheet.png")
