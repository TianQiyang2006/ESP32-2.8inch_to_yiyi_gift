"""Make review sheets from the actual native LVGL screenshots."""
from pathlib import Path
import argparse
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/days-style-refactor-20261115'))
    args = parser.parse_args()
    out = args.out.resolve()
    fonts = Path('C:/Windows/Fonts')

    def font(size, bold=False):
        return ImageFont.truetype(str(fonts/('msyhbd.ttc' if bold else 'msyh.ttc')), size)

    paper, ink, muted = '#F3EFE8', '#354E42', '#7E8277'
    hero = Image.new('RGB', (1416, 746), paper)
    draw = ImageDraw.Draw(hero)
    draw.text((40, 22), '纪念日 · 专属分类预览', font=font(32, True), fill=ink)
    draw.text((40, 72), '生日默认庆祝月桂，相恋默认渐变爱心；名称、日期和样式均可自定义', font=font(19), fill=muted)
    for x, title, subtitle, filename in [
        (40, '生日', '月桂庆祝卡 · 自定义生日与倒数', 'special-01-birthday.png'),
        (736, '相恋', '渐变爱心卡 · 自定义相伴天数', 'special-02-love.png'),
    ]:
        draw.text((x, 117), title, font=font(23, True), fill=ink)
        draw.text((x+72, 123), subtitle, font=font(17), fill=muted)
        draw.rounded_rectangle((x-3, 166, x+643, 652), radius=8, fill='#DAD5CA')
        shot = Image.open(out/'ui'/filename).convert('RGB')
        assert shot.size == (320, 240)
        hero.paste(shot.resize((640, 480), Image.Resampling.NEAREST), (x, 169))
    draw.text((40, 687), '实际 LVGL 渲染 · 原始屏幕 320 × 240 · 示例日期仅用于预览 · 待确认后烧录',
              font=font(18), fill=muted)
    hero.save(out/'ui-preview.png')

    gallery = Image.new('RGB', (1080, 690), paper)
    draw = ImageDraw.Draw(gallery)
    draw.text((24, 13), '参考图重构 · 五种可选样式', font=font(26, True), fill=ink)
    cards = [
        ('简约 · 淡蓝渐变', 'style-1-classic-0-blue.png'),
        ('月桂 · 纪念庆祝', 'style-2-laurel-0-blue.png'),
        ('笔触 · 手写下划线', 'style-3-ink-0-blue.png'),
        ('爱心 · 红粉渐变', 'style-4-heart-0-rose.png'),
        ('点阵 · 极简留白', 'style-5-dots-0-blue.png'),
        ('深色 · 夜间配色', 'style-5-dots-3-dark.png'),
    ]
    for i, (caption, filename) in enumerate(cards):
        x, y = 24+(i%3)*356, 68+(i//3)*286
        draw.text((x, y), caption, font=font(18, True), fill=ink)
        gallery.paste(Image.open(out/'ui'/filename).convert('RGB'), (x, y+31))
    draw.text((24, 654), '实际 LVGL 渲染 · 320 × 240 · 名称与日期仅作展示 · 确认后再烧录', font=font(16), fill=muted)
    gallery.save(out/'ui-styles.png')

    color_files = [('简约 · 自定义紫色文字', 'text-color-classic.png'),
                   ('爱心 · 自定义紫色文字', 'text-color-heart.png'),
                   ('爱心 · 恢复自动白字', 'text-color-auto-restored.png')]
    if all((out/'ui'/filename).exists() for _, filename in color_files):
        color_sheet = Image.new('RGB', (1080, 340), paper)
        draw = ImageDraw.Draw(color_sheet)
        draw.text((24, 12), '文字颜色 · 自动或自定义 HEX', font=font(24, True), fill=ink)
        for i, (caption, filename) in enumerate(color_files):
            x = 24+i*356
            draw.text((x, 58), caption, font=font(16), fill=ink)
            color_sheet.paste(Image.open(out/'ui'/filename).convert('RGB'), (x, 86))
        color_sheet.save(out/'ui-text-colors.png')

    states = [
        ('生日当天', 'special-11-birthday-today.png'),
        ('相恋开始当天', 'special-06-love-start-day.png'),
        ('相恋尚未开始', 'special-08-love-future.png'),
        ('任意自定义名称', 'special-03-love-custom-name.png'),
        ('生日：24字名称 / 5位天数', 'special-12-birthday-long-title-max-days.png'),
        ('相恋：24字名称 / 5位天数', 'special-05-love-long-title-max-days.png'),
    ]
    sheet = Image.new('RGB', (1080, 620), paper)
    draw = ImageDraw.Draw(sheet)
    draw.text((24, 12), '专属界面 · 状态预览', font=font(24, True), fill=ink)
    for i, (caption, filename) in enumerate(states):
        x, y = 24+(i%3)*356, 62+(i//3)*274
        draw.text((x, y), caption, font=font(16), fill=ink)
        sheet.paste(Image.open(out/'ui'/filename).convert('RGB'), (x, y+26))
    sheet.save(out/'ui-states.png')
    phone_files = [('生日 · 月桂样式', 'phone-birthday.png'), ('相恋 · 爱心样式', 'phone-love.png')]
    if all((out/'phone'/filename).exists() for _, filename in phone_files):
        phones = Image.new('RGB', (850, 1055), paper)
        draw = ImageDraw.Draw(phones)
        draw.text((24, 14), '手机端 · 实时预览与样式设置', font=font(25, True), fill=ink)
        for i, (caption, filename) in enumerate(phone_files):
            x = 24+i*414
            draw.text((x, 62), caption, font=font(17, True), fill=ink)
            shot = Image.open(out/'phone'/filename).convert('RGB')
            phones.paste(shot.crop((0, 60, 390, 1000)), (x, 96))
        phones.save(out/'phone-preview.png')
    print(f'Preview: {out / "ui-preview.png"}')
    print(f'Styles: {out / "ui-styles.png"}')
    print(f'States: {out / "ui-states.png"}')


if __name__ == '__main__':
    main()
