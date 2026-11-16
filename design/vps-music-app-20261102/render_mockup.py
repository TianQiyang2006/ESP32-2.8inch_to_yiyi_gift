"""Render a design proposal only; no firmware or server changes."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
W, H, SCALE = 320, 240, 3
BG = '#F7F7EF'
PAPER = '#FFFDF8'
INK = '#294C3D'
MID = '#69816E'
MUTED = '#8D9C89'
SAGE = '#E6EEDF'
TRACK = '#D6E1D0'
GREEN = '#49775A'
FONT = r'C:\Windows\Fonts\msyh.ttc'
FONT_BOLD = r'C:\Windows\Fonts\msyhbd.ttc'

# Original placeholder lyrics, independent of the sample song.
LYRICS = ['在有风的午后', '和喜欢的旋律相遇', '让每一天都慢慢发光', '把心事轻轻唱给你听']


def font(size, bold=False, scale=SCALE):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, round(size * scale))


def coords(values):
    return tuple(round(v * SCALE) for v in values)


def rounded(d, box, radius, fill, outline=None, width=1):
    d.rounded_rectangle(coords(box), radius=round(radius*SCALE), fill=fill,
                        outline=outline, width=round(width*SCALE))


def text(d, xy, value, size=11, color=INK, bold=False, anchor='lt'):
    d.text(coords(xy), value, font=font(size, bold), fill=color, anchor=anchor)


def line(d, xy, fill=INK, width=1):
    d.line(coords(xy), fill=fill, width=round(width*SCALE))


def polygon(d, points, fill):
    d.polygon([coords(p) for p in points], fill=fill)


def circle(d, box, fill, outline=None, width=1):
    d.ellipse(coords(box), fill=fill, outline=outline, width=round(width*SCALE))


def base():
    return Image.new('RGB', (W*SCALE, H*SCALE), BG)


def export(img, name):
    native = img.resize((W, H), Image.Resampling.LANCZOS)
    if name == 'launcher-with-music':
        original = Image.open(ROOT / 'design/tetris-20261101/launcher-page-2.png').convert('RGB')
        original.paste(native.crop((30,154,91,240)), (30,154))
        native = original
    native.save(OUT / f'{name}-320x240.png')
    native.resize((W*3, H*3), Image.Resampling.NEAREST).save(
        OUT / f'{name}-preview-3x.png')
    return native


def chevron(d, x, y, direction='left', color=INK, size=6):
    offset = size if direction == 'left' else -size
    line(d, (x+offset/2, y-size, x-offset/2, y, x+offset/2, y+size), color, 2)


def list_icon(d, x, y, color=GREEN):
    for offset in (0, 5, 10):
        circle(d, (x, y+offset, x+2, y+offset+2), color)
        line(d, (x+6, y+offset+1, x+17, y+offset+1), color, 1.5)


def speaker(d, x, y, color=GREEN):
    polygon(d, [(x,y+5),(x+4,y+5),(x+9,y+1),(x+9,y+17),
                (x+4,y+13),(x,y+13)], color)
    d.arc(coords((x+5,y+3,x+15,y+15)), 295, 65, fill=color, width=4)


def pause(d, cx, cy, radius=22, filled=True):
    if filled:
        circle(d, (cx-radius,cy-radius,cx+radius,cy+radius), GREEN)
    color = PAPER if filled else GREEN
    rounded(d, (cx-7,cy-8,cx-2,cy+8), 1, color)
    rounded(d, (cx+2,cy-8,cx+7,cy+8), 1, color)


def skip(d, cx, cy, direction, radius=19):
    circle(d, (cx-radius,cy-radius,cx+radius,cy+radius), SAGE)
    if direction == 'left':
        polygon(d, [(cx+6,cy-7),(cx-3,cy),(cx+6,cy+7)], INK)
        line(d, (cx-6,cy-7,cx-6,cy+7), INK, 2)
    else:
        polygon(d, [(cx-6,cy-7),(cx+3,cy),(cx-6,cy+7)], INK)
        line(d, (cx+6,cy-7,cx+6,cy+7), INK, 2)


def loop_icon(d, x, y):
    line(d, (x+2,y+7,x+2,y+3,x+16,y+3), GREEN, 1.5)
    polygon(d, [(x+13,y),(x+18,y+3),(x+13,y+6)], GREEN)
    line(d, (x+16,y+10,x+16,y+14,x+2,y+14), GREEN, 1.5)
    polygon(d, [(x+5,y+11),(x,y+14),(x+5,y+17)], GREEN)


def back_header(d, title, badge='已连接'):
    chevron(d, 18, 16)
    text(d, (39, 8), title, 14, INK, True)
    rounded(d, (241, 7, 308, 26), 9, SAGE)
    circle(d, (249, 13, 254, 18), '#679465')
    text(d, (260, 10), badge, 10, GREEN)
    line(d, (12, 32, 308, 32), '#E3E8DC')


def launcher():
    # This source matches the current three-icon page.
    img = Image.open(ROOT / 'design/tetris-20261101/launcher-page-2.png').convert('RGB')
    img = img.resize((W*SCALE, H*SCALE), Image.Resampling.NEAREST)
    d = ImageDraw.Draw(img)
    cx, cy = 60, 183
    circle(d, (cx-27,cy-27,cx+27,cy+27), '#F8EAC9')
    circle(d, (cx-23,cy-23,cx+23,cy+23), '#E7AD43')
    circle(d, (cx-18,cy-18,cx+18,cy+18), '#315D4A')
    for radius in (11, 15):
        circle(d, (cx-radius,cy-radius,cx+radius,cy+radius), None, '#6E926F')
    circle(d, (cx-5,cy-5,cx+5,cy+5), '#F2D68F')
    circle(d, (cx-1,cy-1,cx+1,cy+1), '#315D4A')
    line(d, (cx+7,cy+6,cx+7,cy-14,cx+19,cy-17,cx+19,cy+3), '#FFF8E6', 3)
    circle(d, (cx+1,cy+3,cx+9,cy+10), '#E9775B')
    circle(d, (cx+13,cy,cx+21,cy+7), '#E9775B')
    text(d, (60, 219), '音乐', 14, '#FBF6E9', True, 'mt')
    return img


def album_art(d, x, y, side):
    rounded(d, (x,y,x+side,y+side), 12, '#315D4A')
    cx, cy = x+side/2, y+side/2
    # Original mockup illustration, not a fetched album cover.
    d.arc(coords((x+5,y+5,x+side-5,y+side-5)), 205, 305, fill='#73926B', width=15)
    d.arc(coords((x+5,y+5,x+side-5,y+side-5)), 30, 130, fill='#73926B', width=15)
    radius = side*0.34
    circle(d, (cx-radius,cy-radius,cx+radius,cy+radius), '#203B31')
    for r in (radius*.84, radius*.64, radius*.44):
        circle(d, (cx-r,cy-r,cx+r,cy+r), None, '#688767')
    label_r, hole_r = side*.10, side*.025
    circle(d, (cx-label_r,cy-label_r,cx+label_r,cy+label_r), '#E7AD43')
    circle(d, (cx-hole_r,cy-hole_r,cx+hole_r,cy+hole_r), '#315D4A')


def progress(d, y=157):
    text(d, (16,y-16), '01:26', 9, MID)
    text(d, (304,y-16), '04:29', 9, MID, anchor='rt')
    rounded(d, (16,y,304,y+3), 2, TRACK)
    rounded(d, (16,y,108,y+3), 2, GREEN)
    circle(d, (104,y-2,111,y+5), GREEN)


def player():
    img = base(); d = ImageDraw.Draw(img)
    back_header(d, '音乐')
    album_art(d, 14, 43, 82)
    text(d, (109,42), '晴天', 18, INK, True)
    text(d, (110,68), '周杰伦', 11, MID)
    text(d, (110,89), LYRICS[0], 11, MUTED)
    rounded(d, (103,105,307,129), 6, SAGE)
    text(d, (111,110), LYRICS[1], 13, GREEN, True)
    chevron(d, 296,117, 'right', GREEN, 3)
    progress(d)
    loop_icon(d, 28,180)
    skip(d, 104,188,'left')
    pause(d, 160,188)
    skip(d, 216,188,'right')
    speaker(d, 265,178)
    text(d, (286,183), '40', 11, GREEN)
    rounded(d, (12,216,308,239), 7, SAGE)
    list_icon(d, 23,222)
    text(d, (49,221), '酷狗我喜欢', 11, INK, True)
    text(d, (282,222), '131 首', 10, MID, anchor='rt')
    chevron(d, 296,228,'right',MID,4)
    return img


def playlist():
    img = base(); d = ImageDraw.Draw(img)
    back_header(d, '播放列表')
    rounded(d, (12,39,308,69), 7, SAGE)
    text(d, (23,47), '酷狗我喜欢', 12, INK, True)
    text(d, (276,49), '131 首', 10, GREEN, anchor='rt')
    line(d, (288,51,292,55,296,51), GREEN, 1.5)
    rows = [('01','晴天','周杰伦',True),
            ('02','晚星','逆时针向',False),
            ('03','夜曲','周杰伦',False)]
    for i,(number,title,artist,active) in enumerate(rows):
        y = 77+i*36
        if active:
            rounded(d, (12,y-2,308,y+31), 6, '#EDF2E7')
        if active:
            for j,height in enumerate((6,12,9)):
                rounded(d, (23+j*4,y+16-height/2,25+j*4,y+16+height/2),1,GREEN)
        else:
            text(d, (22,y+10),number,10,MUTED)
        text(d, (47,y+1),title,13,INK,active)
        text(d, (47,y+19),artist,10,MID)
        if active:
            text(d, (294,y+11),'播放中',9,GREEN,anchor='rt')
        else:
            chevron(d, 292,y+15,'right','#94A38E',4)
    rounded(d, (312,79,315,179),1,'#E7ECDC')
    rounded(d, (312,79,315,101),1,'#B4C5AA')
    rounded(d, (12,191,308,237),9,SAGE)
    album_art(d, 19,197,32)
    text(d, (60,199),'晴天',12,INK,True)
    text(d, (60,217),'周杰伦',10,MID)
    pause(d, 243,213,16)
    skip(d, 285,213,'right',16)
    return img


def lyrics():
    img = base(); d = ImageDraw.Draw(img)
    back_header(d, '晴天 · 歌词')
    text(d, (160,46),LYRICS[0],12,MUTED,anchor='mt')
    text(d, (160,75),LYRICS[1],13,MID,anchor='mt')
    rounded(d, (12,100,308,134),8,SAGE)
    text(d, (160,108),LYRICS[2],16,GREEN,True,'mt')
    text(d, (160,147),LYRICS[3],12,MUTED,anchor='mt')
    rounded(d, (12,179,308,237),10,SAGE)
    text(d, (23,188),'周杰伦',10,MID)
    text(d, (23,213),'01:26 / 04:29',9,MID)
    skip(d, 162,208,'left',17)
    pause(d, 209,208,21)
    skip(d, 256,208,'right',17)
    list_icon(d, 287,203)
    return img


def composite(screens):
    # Board helpers use output pixels, independently of the screen scale.
    border, gutter, screen_scale = 44, 38, 2
    sw, sh = W*screen_scale,H*screen_scale
    top, cell_h, bottom = 168, 568, 110
    board = Image.new('RGB',(border*2+sw*2+gutter,top+cell_h*2+bottom),'#E9EDE3')
    d = ImageDraw.Draw(board)

    def bt(xy, value, size=22, color=INK, bold=False):
        d.text(xy,value,font=font(size,bold,1),fill=color,anchor='lt')

    bt((border,31),'依依的音乐',40,INK,True)
    bt((border,92),'独立音乐 App  /  桌面入口 · 在线曲库 · 滚动歌词',21,MID)
    bt((board.width-300,44),'界面提案  01',19,MID)
    titles = ['01  桌面 · 新增音乐入口','02  播放器 · 一屏控制与歌词',
              '03  点击歌单 · 展开曲目','04  点击歌词 · 大字模式']
    subtitles = ['保留闹钟、番茄时钟与俄罗斯方块','默认音量 40，触屏切歌、暂停、续播',
                 '可滚动选歌，点击顶部歌单名切换歌单','高亮当前行，仍可暂停和切歌']
    for i,screen in enumerate(screens):
        x = border+(i%2)*(sw+gutter)
        y = top+(i//2)*cell_h
        bt((x,y),titles[i],24,INK,True)
        bt((x,y+37),subtitles[i],18,MID)
        box=(x-4,y+72,x+sw+4,y+72+sh+4)
        d.rounded_rectangle(box,radius=13,fill='#CFD9C9')
        board.paste(screen.resize((sw,sh),Image.Resampling.NEAREST),(x,y+74))
    bt((border,board.height-83),'音频、歌单与歌词由 VPS 提供；音乐与小智分别进入。',20,INK)
    bt((border,board.height-48),'320 × 240 实际比例 · 歌词、进度及曲目排序为示意 · 本次仅展示设计',17,MID)
    board.save(OUT / 'vps-music-app-overview.png')


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    screens = [export(launcher(),'launcher-with-music'),
               export(player(),'music-player'),
               export(playlist(),'music-playlist'),
               export(lyrics(),'music-lyrics')]
    composite(screens)


if __name__ == '__main__':
    main()
