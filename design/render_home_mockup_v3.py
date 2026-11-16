from PIL import Image, ImageDraw, ImageFont
from pathlib import Path
p=Path('design/mockups/rohei-generated-artwork-v2.jpg'); out=Path('design/mockups')
im=Image.open(p).convert('RGB').resize((320,240),Image.Resampling.LANCZOS); im.save(out/'home-v3-artwork-320x240.jpg',quality=95)
d=ImageDraw.Draw(im,'RGBA'); ink=(40,76,61,255); sec=(99,127,104,255)
d.rectangle((0,0,188,166),fill=(247,247,237,105)); d.text((18,108),'10:08',font=ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf',66),fill=ink,anchor='ls'); d.text((22,134),'10月6日  星期二',font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',13),fill=sec,anchor='ls'); d.rounded_rectangle((16,174,304,218),12,fill=(227,236,217,235)); cx,cy=39,196
for dx,dy in [(0,-13),(9,-9),(13,0),(9,9),(0,13),(-9,9),(-13,0),(-9,-9)]: d.line((cx+dx*.72,cy+dy*.72,cx+dx,cy+dy),fill=(207,167,79,255),width=2)
d.ellipse((32,189,46,203),fill=(225,186,93,255)); d.text((60,202),'晴',font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',15),fill=ink,anchor='ls'); d.text((120,204),'24°',font=ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf',29),fill=ink,anchor='ls'); d.text((212,201),'18° / 26°',font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',13),fill=sec,anchor='ls'); d.arc((287,15,309,35),210,330,fill=sec,width=2); d.arc((292,20,304,32),210,330,fill=sec,width=2); d.ellipse((297,22,299,24),fill=sec); d.rounded_rectangle((276,15,280,22),2,fill=sec); d.arc((273,17,283,27),0,180,fill=sec,width=1); d.line((278,25,278,28),fill=sec,width=1); d.rounded_rectangle((128,227,138,231),2,fill=ink)
for x in (147,160,173,186): d.ellipse((x-2,227,x+2,231),fill=(193,207,183,255))
im.save(out/'home-v3-320x240.png'); im.resize((960,720),Image.Resampling.NEAREST).save(out/'home-v3-preview-3x.png')
