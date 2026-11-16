"""Music UI revision 2. Design assets only; no device or network access."""
from pathlib import Path
import json

from PIL import Image, ImageDraw, ImageFont


OUT = Path(__file__).resolve().parent
W, H, S = 320, 240, 3
CREAM = '#FAF8F0'
INK = '#244B3C'
GREEN = '#315E48'
MID = '#62745F'
FAINT = '#83917B'
SAGE = '#E8EDDE'
TRACK = '#DDE4D2'
GOLD = '#E5AF61'
DARK = '#203E32'
WHITE = '#FFFCF1'
FONT = r'C:\Windows\Fonts\msyh.ttc'
BOLD = r'C:\Windows\Fonts\msyhbd.ttc'
LYRICS = ['在有风的午后', '和喜欢的旋律相遇', '让每一天都慢慢发光', '把心事轻轻唱给你听']


def font(size, bold=False, scale=S):
    return ImageFont.truetype(BOLD if bold else FONT, round(size*scale))


def pt(values):
    return tuple(round(v*S) for v in values)


def rect(d, box, fill, radius=0, outline=None, width=1):
    d.rounded_rectangle(pt(box), radius=round(radius*S), fill=fill,
                        outline=outline, width=max(1, round(width*S)))


def circle(d, cx, cy, radius, fill, outline=None, width=1):
    d.ellipse(pt((cx-radius,cy-radius,cx+radius,cy+radius)),
              fill=fill, outline=outline, width=max(1,round(width*S)))


def text(d, xy, value, size=12, color=INK, bold=False, anchor='lt'):
    d.text(pt(xy),value,font=font(size,bold),fill=color,anchor=anchor)


def line(d, coords, color=INK, width=1):
    d.line(pt(coords),fill=color,width=max(1,round(width*S)))


def poly(d, points, color):
    d.polygon([pt(p) for p in points],fill=color)


def back(d, color=INK):
    line(d,(23,12,17,18,23,24),color,1.8)


def arrow(d, x, y, color=INK, size=3):
    line(d,(x-size/2,y-size,x+size/2,y,x-size/2,y+size),color,1.5)


def speaker(d,x,y,color=GREEN):
    poly(d,[(x,y+5),(x+4,y+5),(x+9,y+1),(x+9,y+15),
            (x+4,y+11),(x,y+11)],color)
    d.arc(pt((x+6,y+3,x+15,y+13)),300,60,fill=color,width=round(1.5*S))


def queue(d,cx,cy,color=GREEN):
    for dy in (-5,0,5):
        circle(d,cx-7,cy+dy,0.8,color)
        line(d,(cx-3,cy+dy,cx+8,cy+dy),color,1.5)


def loop(d,cx,cy,color=GREEN):
    line(d,(cx-8,cy,cx-8,cy-5,cx+7,cy-5),color,1.5)
    poly(d,[(cx+4,cy-8),(cx+9,cy-5),(cx+4,cy-2)],color)
    line(d,(cx+8,cy+1,cx+8,cy+6,cx-7,cy+6),color,1.5)
    poly(d,[(cx-4,cy+3),(cx-9,cy+6),(cx-4,cy+9)],color)


def skip(d,cx,cy,direction=1,color=GREEN):
    # direction -1 = previous; 1 = next.
    poly(d,[(cx-5*direction,cy-7),(cx+4*direction,cy),
            (cx-5*direction,cy+7)],color)
    line(d,(cx+7*direction,cy-7,cx+7*direction,cy+7),color,2)


def pause(d,cx,cy,r=24,dark=False):
    circle(d,cx,cy+1,r+1,'#18362B' if dark else '#DCE3D1')
    circle(d,cx,cy,r,GOLD if dark else GREEN)
    color = DARK if dark else WHITE
    rect(d,(cx-7,cy-8,cx-2,cy+8),color,1)
    rect(d,(cx+2,cy-8,cx+7,cy+8),color,1)


def controls(d, dark=False):
    color = '#D0DDC8' if dark else GREEN
    loop(d,36,212,color)
    skip(d,96,212,-1,color)
    pause(d,160,212,dark=dark)
    skip(d,224,212,1,color)
    queue(d,284,212,color)


def cover(side):
    """Original code-drawn artwork: warm sun and three layered hills."""
    art = Image.new('RGB',(side*S,side*S),'#DED8B3')
    d = ImageDraw.Draw(art)
    # All proportions work for the hero cover and mini player.
    k = side/88
    def box(vals):
        return pt(tuple(v*k for v in vals))
    d.ellipse(box((55,12,77,34)),fill='#E9AF64')
    d.ellipse(box((-30,35,120,155)),fill='#9CAA82')
    d.ellipse(box((18,49,160,159)),fill='#658464')
    d.ellipse(box((-56,64,110,184)),fill='#315C47')
    d.line(box((15,62,15,78)),fill='#D5DDAC',width=max(1,round(k*S)))
    d.line(box((15,68,10,64)),fill='#D5DDAC',width=max(1,round(k*S)))
    d.line(box((15,72,20,68)),fill='#D5DDAC',width=max(1,round(k*S)))
    mask=Image.new('L',art.size,0)
    ImageDraw.Draw(mask).rounded_rectangle((0,0,side*S-1,side*S-1),radius=round(side*.12*S),fill=255)
    return art,mask


def paste_cover(img,x,y,side):
    art,mask=cover(side)
    img.paste(art,pt((x,y)),mask)


def vinyl(d):
    cx,cy=94,89
    circle(d,cx,cy+2,40,'#DDE3D0')
    circle(d,cx,cy,39,'#294A3B')
    for r in (34,30,24,20):
        circle(d,cx,cy,r,None,'#49654C',0.8)
    circle(d,cx,cy,10,'#DFB973')
    circle(d,cx,cy,2,'#294A3B')


def header(d,title,volume=False,color=INK):
    back(d,color)
    text(d,(42,11),title,13,color,True)
    if volume:
        speaker(d,270,10,color)
        text(d,(303,12),'40',11,color,anchor='rt')


def progress(d,dark=False):
    y=170
    rect(d,(18,y,302,y+3),'#3D5B48' if dark else TRACK,2)
    rect(d,(18,y,109,y+3),GOLD if dark else GREEN,2)
    circle(d,109,y+1.5,3.5,GOLD)
    color = '#A7B79E' if dark else MID
    text(d,(18,179),'01:26',9,color)
    text(d,(302,179),'04:29',9,color,anchor='rt')


def player():
    img=Image.new('RGB',(W*S,H*S),CREAM)
    d=ImageDraw.Draw(img)
    header(d,'音乐',volume=True)
    vinyl(d)
    paste_cover(img,16,45,88)
    d=ImageDraw.Draw(img)
    text(d,(146,50),'晴天',22,INK,True)
    text(d,(147,83),'周杰伦',12,MID)
    rect(d,(144,107,304,128),SAGE,7)
    for i,h in enumerate((4,8,6)):
        rect(d,(153+i*3,118-h/2,154+i*3,118+h/2),GREEN,.5)
    text(d,(170,112),'酷狗我喜欢',10,GREEN)
    text(d,(160,144),LYRICS[2],13,GREEN,True,'mt')
    arrow(d,300,151,MID)
    progress(d)
    controls(d)
    return img


def playlist():
    img=Image.new('RGB',(W*S,H*S),CREAM)
    d=ImageDraw.Draw(img)
    header(d,'播放列表')
    text(d,(303,13),'顺序播放',10,MID,anchor='rt')
    rect(d,(12,40,190,72),GREEN,10)
    text(d,(24,49),'酷狗我喜欢',12,WHITE,True)
    rect(d,(151,47,179,65),'#54755B',6)
    text(d,(165,51),'131',9,WHITE,anchor='mt')
    rect(d,(198,40,308,72),SAGE,10)
    text(d,(210,50),'依依歌单',11,GREEN)
    text(d,(296,51),'3',10,GREEN,anchor='rt')
    rows=[('晴天','周杰伦','04:29'),('晚星','逆时针向','03:13'),('夜曲','周杰伦','03:46')]
    for i,(title,artist,duration) in enumerate(rows):
        y=78+i*40
        if i==0:
            rect(d,(12,y,308,y+38),'#EAF0E1',8)
            for j,h in enumerate((7,14,10)):
                rect(d,(23+j*4,y+19-h/2,25+j*4,y+19+h/2),GREEN,1)
        else:
            text(d,(23,y+14),f'{i+1:02}',10,FAINT)
        text(d,(48,y+4),title,13,INK,i==0)
        text(d,(49,y+24),artist,10,MID)
        text(d,(295,y+15),duration,10,MID,anchor='rt')
    rect(d,(313,81,315,190),SAGE,1)
    rect(d,(313,81,315,105),'#ACBB9F',1)
    # Fixed mini player is separate from the scrolling track rows.
    rect(d,(0,200,320,240),'#EEF0E4')
    rect(d,(12,200,308,202),TRACK,1)
    rect(d,(12,200,107,202),GREEN,1)
    paste_cover(img,14,207,27)
    d=ImageDraw.Draw(img)
    text(d,(51,206),'晴天',12,INK,True)
    text(d,(52,223),'周杰伦',9,MID)
    pause(d,245,220,16)
    skip(d,288,220,1)
    return img


def lyrics():
    img=Image.new('RGB',(W*S,H*S),DARK)
    d=ImageDraw.Draw(img)
    header(d,'晴天',color=WHITE)
    text(d,(302,13),'周杰伦',11,'#BCCCB1',anchor='rt')
    text(d,(160,51),LYRICS[0],12,'#91A68C',anchor='mt')
    text(d,(160,79),LYRICS[1],13,'#AABBA0',anchor='mt')
    rect(d,(16,111,19,126),GOLD,1.5)
    text(d,(160,108),LYRICS[2],18,WHITE,True,'mt')
    text(d,(160,145),LYRICS[3],12,'#91A68C',anchor='mt')
    progress(d,True)
    controls(d,True)
    return img


def volume():
    img=player().convert('RGBA')
    img=Image.alpha_composite(img,Image.new('RGBA',img.size,(21,39,28,105)))
    d=ImageDraw.Draw(img)
    rect(d,(0,105,320,256),CREAM,18)
    rect(d,(141,112,179,115),'#D6DDCB',1.5)
    text(d,(20,129),'音量',15,INK,True)
    text(d,(270,126),'40',22,GREEN,True,anchor='rt')
    line(d,(290,130,298,138),MID,1.5)
    line(d,(298,130,290,138),MID,1.5)
    circle(d,34,174,17,SAGE)
    line(d,(28,174,40,174),GREEN,1.5)
    rect(d,(65,171,255,176),TRACK,2.5)
    rect(d,(65,171,141,176),GREEN,2.5)
    circle(d,141,173.5,6,CREAM,GREEN,2)
    circle(d,286,174,17,SAGE)
    line(d,(280,174,292,174),GREEN,1.5)
    line(d,(286,168,286,180),GREEN,1.5)
    for x,label,active in [(18,'20',False),(116,'40',True),(214,'60',False)]:
        rect(d,(x,201,x+88,235),GREEN if active else SAGE,9)
        text(d,(x+44,212),label,12,WHITE if active else GREEN,active,'mt')
    return img.convert('RGB')


def native_export(screen,name):
    img=screen.resize((W,H),Image.Resampling.LANCZOS).convert('RGB')
    img.save(OUT/f'{name}-320x240.png')
    img.resize((W*3,H*3),Image.Resampling.NEAREST).save(OUT/f'{name}-preview-3x.png')
    return img


def board(screens):
    width,height=1400,1410
    img=Image.new('RGB',(width,height),'#F0F1E8')
    d=ImageDraw.Draw(img)
    def bt(xy,value,size,color=INK,bold=False):
        d.text(xy,value,font=font(size,bold,1),fill=color,anchor='lt')
    bt((40,31),'依依 · 音乐',40,INK,True)
    bt((42,90),'让歌名更清楚，让每次操作更顺手。',21,MID)
    d.rounded_rectangle((1124,40,1360,79),radius=19,fill='#DFE7D5')
    bt((1147,50),'UI 02   /   320 × 240',17,GREEN)
    labels=[('01  正在播放','封面与歌名分区，常用操作固定在底部'),
            ('02  按需展开歌单','点击列表按钮进入，两个歌单直接切换'),
            ('03  沉浸歌词','深绿底色、大字高亮，保留完整播放控制'),
            ('04  随手调音量','默认 40，滑动、加减和常用档位均可调节')]
    for i,(screen,(title,caption)) in enumerate(zip(screens,labels)):
        x=40+(i%2)*680
        y=155+(i//2)*568
        bt((x,y),title,25,INK,True)
        bt((x,y+37),caption,18,MID)
        # Each screen is an honest 2x nearest-neighbor enlargement.
        d.rounded_rectangle((x-3,y+71,x+643,y+557),radius=11,fill='#CDD6C3')
        img.paste(screen.resize((640,480),Image.Resampling.NEAREST),(x,y+74))
    d.line((40,1320,1360,1320),fill='#D4DDCA',width=1)
    bt((40,1341),'独立音乐入口 · 音源来自 VPS · 进入小智时停止音乐',20,INK)
    bt((40,1375),'设计预览，尚未刷机；封面、歌词、进度和排序均为示意。',16,MID)
    img.save(OUT/'music-ui-v2-overview.png')


INTERACTIONS={
    'player': {
        'back':[0,0,40,36], 'volume':[258,0,320,36],
        'lyrics':[12,136,308,162], 'seek':[16,164,304,188],
        'mode':[16,190,56,238], 'previous':[76,190,116,238],
        'pause':[136,188,184,240], 'next':[204,190,244,238],
        'playlist':[264,190,304,238],
    },
    'playlist': {
        'back':[0,0,40,36], 'favorites':[12,40,190,72],
        'yiyi_playlist':[198,40,308,72],
        'track_1':[12,78,308,118], 'track_2':[12,118,308,158],
        'track_3':[12,158,308,198], 'player':[12,200,218,240],
        'pause':[224,200,264,240], 'next':[268,200,308,240],
    },
    'volume': {
        'close':[276,118,312,150], 'minus':[14,154,54,194],
        'slider':[60,156,260,192], 'plus':[266,154,306,194],
        'preset_20':[18,201,106,235], 'preset_40':[116,201,204,235],
        'preset_60':[214,201,302,235],
    },
}


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    screens=[native_export(player(),'music-player-v2'),
             native_export(playlist(),'music-playlist-v2'),
             native_export(lyrics(),'music-lyrics-v2'),
             native_export(volume(),'music-volume-v2')]
    board(screens)
    INTERACTIONS['lyrics']={key:value for key,value in INTERACTIONS['player'].items()
                            if key not in ('volume','lyrics')}
    (OUT/'interaction-map.json').write_text(
        json.dumps(INTERACTIONS,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('Rendered 4 design states at 320x240 and a 2x overview.')


if __name__=='__main__':
    main()
