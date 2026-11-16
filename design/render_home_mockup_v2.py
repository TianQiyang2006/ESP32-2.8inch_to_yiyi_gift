from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'mockups'
source=OUT/'rohei-generated-artwork.jpg'
im=Image.open(source).convert('RGB').resize((320,240),Image.Resampling.LANCZOS)
# Keep the generated illustration as an independent native-resolution art layer.
im.save(OUT/'home-v2-artwork-320x240.jpg',quality=95)
d=ImageDraw.Draw(im,'RGBA')
cream=(247,247,237,235); ink=(40,76,61,255); secondary=(99,127,104,255)
clock=ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf',66)
body=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',13)
weather=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',15)
temp=ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf',29)
# Warm translucent veil keeps the generated artwork while preserving readable clock text.
d.rectangle((0,0,188,166),fill=(247,247,237,105))
d.text((18,108),'10:08',font=clock,fill=ink,anchor='ls')
d.text((22,134),'10月6日  星期二',font=body,fill=secondary,anchor='ls')
# Weather band stays separate and touch-friendly.
d.rounded_rectangle((16,174,304,218),radius=12,fill=(227,236,217,235))
# sun icon
cx,cy=39,196
for dx,dy in [(0,-13),(9,-9),(13,0),(9,9),(0,13),(-9,9),(-13,0),(-9,-9)]: d.line((cx+dx*.72,cy+dy*.72,cx+dx,cy+dy),fill=(207,167,79,255),width=2)
d.ellipse((32,189,46,203),fill=(225,186,93,255))
d.text((60,202),'晴',font=weather,fill=ink,anchor='ls')
d.text((120,204),'24°',font=temp,fill=ink,anchor='ls')
d.text((212,201),'18° / 26°',font=body,fill=secondary,anchor='ls')
# status icons, driven by firmware later
d.arc((287,15,309,35),210,330,fill=secondary,width=2); d.arc((292,20,304,32),210,330,fill=secondary,width=2); d.ellipse((297,22,299,24),fill=secondary)
d.rounded_rectangle((276,15,280,22),radius=2,fill=secondary); d.arc((273,17,283,27),0,180,fill=secondary,width=1); d.line((278,25,278,28),fill=secondary,width=1)
# five-page position indicator
for x in (147,160,173,186): d.ellipse((x-2,227,x+2,231),fill=(193,207,183,255))
d.rounded_rectangle((128,227,138,231),radius=2,fill=ink)
im.save(OUT/'home-v2-320x240.png')
im.resize((960,720),Image.Resampling.NEAREST).save(OUT/'home-v2-preview-3x.png')
print('saved',OUT/'home-v2-320x240.png')
