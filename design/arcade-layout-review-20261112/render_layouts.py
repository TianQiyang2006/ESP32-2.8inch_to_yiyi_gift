"""Review-only 320x240 game layouts. Does not alter firmware or game rules."""
from pathlib import Path
import base64
import html
import json
import random

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent
S = 3
BG, INK, MUTED = '#F6F7F3', '#243C34', '#68776E'
LINE, SOFT, GREEN = '#CED7CE', '#E6EBE3', '#397255'
GOLD, RED, WHITE = '#D6AC58', '#BF645B', '#FFFFFF'
FONT = 'C:/Windows/Fonts/msyh.ttc'
BOLD = 'C:/Windows/Fonts/msyhbd.ttc'


def font(size, bold=False):
    return ImageFont.truetype(BOLD if bold else FONT, int(size * S))


class Screen:
    def __init__(self, key, name, board, note):
        self.key, self.name, self.board, self.note = key, name, board, note
        self.image = Image.new('RGB', (320 * S, 240 * S), BG)
        self.d = ImageDraw.Draw(self.image)
        self.hotspots = []

    def rect(self, x, y, w, h, fill, radius=0, stroke=None, width=1):
        assert x >= 0 and y >= 0 and x + w <= 320 and y + h <= 240, (self.key, x, y, w, h)
        box = (int(x*S), int(y*S), int((x+w)*S-1), int((y+h)*S-1))
        self.d.rounded_rectangle(box, radius=int(radius*S), fill=fill, outline=stroke, width=int(width*S))

    def line(self, points, fill=LINE, width=1):
        self.d.line([(int(x*S), int(y*S)) for x, y in points], fill=fill, width=max(1, int(width*S)))

    def circle(self, x, y, r, fill=None, stroke=None, width=1):
        self.d.ellipse(((x-r)*S, (y-r)*S, (x+r)*S, (y+r)*S), fill=fill, outline=stroke, width=int(width*S))

    def text(self, value, x, y, size=14, color=INK, bold=False, anchor='mm'):
        self.d.text((x*S, y*S), str(value), font=font(size, bold), fill=color, anchor=anchor)

    def button(self, title, x, y, w, h, strong=False, action=None, size=13, inset=0):
        self.rect(x+inset, y+inset, w-2*inset, h-2*inset, GREEN if strong else SOFT, 4)
        self.text(title, x+w/2, y+h/2, size, WHITE if strong else INK, bold=strong)
        if action:
            self.hotspots.append(dict(x=x, y=y, w=w, h=h, action=action, label=title))

    def top(self, title, detail='', extra=None):
        self.rect(0, 0, 320, 32, BG)
        self.button('‹', 2, 0, 36, 32, action='menu', size=25)
        self.button('Ⅱ', 282, 0, 36, 32, action='menu', size=17)
        if detail:
            self.text(title, 46, 16, 14, bold=True, anchor='lm')
            self.text(detail, 270 if not extra else 214, 16, 12, MUTED, anchor='rm')
        else:
            self.text(title, 160, 16, 14, bold=True)
        if extra:
            self.button(extra, 224, 0, 52, 32, action='extra', size=12)

    def save(self):
        self.image.save(OUT / f'{self.key}@3x.png')
        self.image.resize((320, 240), Image.Resampling.LANCZOS).save(OUT / f'{self.key}.png')


screens = []

# Dense board: move navigation beside the board and use the entire screen height.
c = Screen('gomoku', '五子棋', [0, 0, 240, 240], '240×240棋盘；点选预览，右侧微调并确认落子。')
c.rect(0, 0, 240, 240, '#EEE4CC')
for i in range(15):
    c.line([(8, 8+16*i), (232, 8+16*i)], '#A89E83')
    c.line([(8+16*i, 8), (8+16*i, 232)], '#A89E83')
for r in (3, 7, 11):
    for col in (3, 7, 11):
        c.circle(8+16*col, 8+16*r, 2, '#9B8B65')
stones = [(7,7,1), (7,8,2), (8,8,1), (6,7,2), (9,9,1), (8,7,2), (10,10,1), (9,8,2)]
for r, col, side in stones:
    c.circle(8+16*col, 8+16*r, 6.5, INK if side == 1 else WHITE, INK if side == 1 else '#AAA99D')
c.circle(8+16*9, 8+16*7, 7, stroke=GREEN, width=2)
c.line([(8+16*9-10, 8+16*7), (8+16*9+10, 8+16*7)], GREEN)
c.line([(8+16*9, 8+16*7-10), (8+16*9, 8+16*7+10)], GREEN)
c.button('返回', 248, 4, 68, 32, action='menu')
c.button('暂停', 248, 42, 68, 32, action='menu')
c.text('黑棋 · J8', 282, 90, 12, bold=True)
for label, x, y in [('↑',248,106), ('↓',284,106), ('←',248,144), ('→',284,144)]:
    c.button(label, x, y, 32, 32, action='nudge', size=18)
c.button('落子', 248, 184, 68, 52, True, 'confirm', 16)
screens.append(c)

# Swipe game: clean top bar and centred square board, no direction controls.
c = Screen('merge', '2048', [58, 34, 204, 204], '204×204居中棋盘；四向滑动，分数归入顶栏。')
c.top('2048', '分数 1,248')
values = [2,0,0,0, 4,8,2,0, 16,32,8,2, 128,64,16,4]
colors = {0:SOFT, 2:'#E3E9DB', 4:'#D5DFC9', 8:'#BCD1AD', 16:'#9DBD8E', 32:'#7BA77C', 64:'#538C67', 128:'#D6AC58'}
c.rect(58, 34, 204, 204, '#CAD3C4', 5)
for i, v in enumerate(values):
    x, y = 62 + (i % 4) * 50, 38 + (i // 4) * 50
    c.rect(x, y, 46, 46, colors[v], 4)
    if v:
        c.text(v, x+23, y+23, 23 if v < 100 else 20, WHITE if 32 <= v <= 64 else INK, True)
screens.append(c)

# Wide action field with real thumb-sized directional controls.
c = Screen('snake', '贪吃蛇', [0, 32, 320, 160], '320×160横向场地；底部4个80×48方向键。')
c.top('贪吃蛇', '分数 120')
c.rect(0, 32, 320, 160, '#EFF3EB')
for i in range(21):
    if i < 20: c.line([(i*16,32),(i*16,192)], '#E1E7DD')
for i in range(11):
    c.line([(0,32+i*16),(320,32+i*16)], '#E1E7DD')
snake = [(6,6),(7,6),(8,6),(9,6),(10,6),(10,5),(10,4),(11,4),(12,4)]
for k, (x, y) in enumerate(snake):
    c.rect(x*16+1, 32+y*16+1, 14, 14, GREEN if k == len(snake)-1 else '#7FA184', 3)
c.circle(12*16+11, 32+4*16+5, 1, WHITE)
c.circle(15*16+8, 32+2*16+8, 5, RED)
for i, title in enumerate(('←','↑','↓','→')):
    c.button(title, i*80, 192, 80, 48, action='direction', size=25)
    if i: c.line([(i*80,198),(i*80,234)], LINE)
screens.append(c)

# Numeric input stays visible; separate visual hierarchy for each kind of cell.
c = Screen('sudoku', '数独', [2, 12, 216, 216], '216×216棋盘；右侧保留数字键盘、清除与笔记。')
c.rect(2, 12, 216, 216, WHITE)
selected_r, selected_col = 4, 4
for i in range(9):
    c.rect(2+i*24, 12+selected_r*24, 24, 24, '#EEF2EA')
    c.rect(2+selected_col*24, 12+i*24, 24, 24, '#EEF2EA')
c.rect(2+selected_col*24,12+selected_r*24,24,24,'#BFD5BA')
board = [
    [5,3,0,0,7,0,0,0,0],[6,0,0,1,9,5,0,0,0],[0,9,8,0,0,0,0,6,0],
    [8,0,0,0,6,0,0,0,3],[4,0,0,8,0,3,0,0,1],[7,0,0,0,2,0,0,0,6],
    [0,6,0,0,0,0,2,8,0],[0,0,0,4,1,9,0,0,5],[0,0,0,0,8,0,0,7,9]]
for r in range(9):
    for col in range(9):
        if board[r][col]: c.text(board[r][col],14+col*24,24+r*24,15,bold=True)
for i in range(10):
    coord=2+i*24
    c.line([(coord,12),(coord,228)], INK if i%3 == 0 else LINE, 2 if i%3 == 0 else 0.7)
    coord=12+i*24
    c.line([(2,coord),(218,coord)], INK if i%3 == 0 else LINE, 2 if i%3 == 0 else 0.7)
c.button('‹',222,4,46,32,action='menu',size=25)
c.button('Ⅱ',272,4,46,32,action='menu',size=17)
c.text('数独 30/81',270,54,12,bold=True)
for n in range(1,10):
    c.button(str(n),222+(n-1)%3*32,76+(n-1)//3*36,32,36,action='number',size=19,inset=1)
c.button('清除',222,194,46,34,action='clear',size=12)
c.button('笔记',272,194,46,34,action='notes',size=12)
screens.append(c)

# Motion game: dedicate a separate track to the finger so it cannot hide the ball.
c = Screen('breakout','打砖块',[0,32,320,176],'320×176运动场；底部32px整条拖动区，不遮球路。')
c.top('打砖块','240分 · 3命')
c.rect(0,32,320,176,WHITE)
for r in range(4):
    for col in range(8):
        if r > 1 and (r,col) in {(2,3),(2,4),(3,2),(3,3),(3,4)}: continue
        c.rect(8+col*38,42+r*15,35,11,[GOLD,'#C2D2B9','#90B28E',GREEN][r],3)
for x,y,rad in [(166,151,2),(173,143,2),(180,135,4)]:
    c.circle(x,y,rad,RED if rad == 4 else '#EDD7D0')
c.rect(126,196,68,7,GREEN,3)
c.rect(0,208,320,32,SOFT)
c.line([(68,224),(252,224)],'#AEBEAD',2)
c.line([(68,224),(75,219)],'#AEBEAD',2)
c.line([(68,224),(75,229)],'#AEBEAD',2)
c.line([(252,224),(245,219)],'#AEBEAD',2)
c.line([(252,224),(245,229)],'#AEBEAD',2)
c.rect(142,213,36,22,GREEN,8)
c.hotspots.append(dict(x=0,y=208,w=320,h=32,action='track',label='左右拖动控制挡板'))
screens.append(c)

# Existing 8x6 rules retained, every tile becomes a 34px touch target.
c = Screen('mines','扫雷',[24,34,272,204],'272×204居中棋盘；34px格子，插旗模式放顶栏。')
c.button('‹',2,0,36,32,action='menu',size=25)
c.text('雷 6',48,16,13,bold=True,anchor='lm')
c.text('00:42',112,16,13,color=MUTED,anchor='lm')
c.button('挖开',182,0,88,32,action='flag',size=13)
c.button('Ⅱ',282,0,36,32,action='menu',size=17)
opened={(r,col) for r in range(6) for col in range(8) if (r<4 and col<4) or (r<2 and col<5)}
mines={(2,4),(4,1),(4,4),(5,6),(3,7),(1,6),(5,3),(2,5)}
numbers={cell:sum((cell[0]+dr,cell[1]+dc) in mines for dr in (-1,0,1) for dc in (-1,0,1) if (dr,dc)!=(0,0)) for cell in opened}
for r in range(6):
    for col in range(8):
        x,y=24+col*34,34+r*34
        c.rect(x+1,y+1,32,32,'#EAF0E6' if (r,col) in opened else '#BACBB5',3)
        if numbers.get((r,col),0):
            c.text(numbers[r,col],x+17,y+17,18,GREEN if numbers[r,col]==1 else '#B7783F',True)
        elif (r,col) in {(2,4),(4,1)}:
            c.line([(x+13,y+10),(x+13,y+25)],INK,2)
            c.d.polygon([(int((x+14)*S),int((y+9)*S)),(int((x+25)*S),int((y+13)*S)),(int((x+14)*S),int((y+18)*S))],fill=RED)
            c.line([(x+8,y+25),(x+19,y+25)],INK,2)
screens.append(c)

OUT.mkdir(parents=True, exist_ok=True)
for item in screens:
    item.save()

# Review sheet uses exact screen aspect ratio; labels stay outside the screens.
sheet = Image.new('RGB',(1536,1010),'#E9EDE7')
d=ImageDraw.Draw(sheet)
titlefont=ImageFont.truetype(BOLD,32)
smallfont=ImageFont.truetype(FONT,20)
label_font=ImageFont.truetype(BOLD,23)
d.text((28,22),'游戏布局重设计 · 320 × 240',font=titlefont,fill=INK)
d.text((28,70),'六款代表布局 / 配色仅作占位 / 布局确认稿，尚未接入固件',font=smallfont,fill=MUTED)
for i,item in enumerate(screens):
    x=24+(i%3)*504
    y=120+(i//3)*440
    d.text((x,y),item.name,font=label_font,fill=INK)
    screen=item.image.resize((480,360),Image.Resampling.LANCZOS)
    sheet.paste(screen,(x,y+38))
    d.text((x,y+402),f'场地区域 {item.board[2]} × {item.board[3]} px',font=smallfont,fill=MUTED)
sheet.save(OUT/'layouts-six.png')

data=[]
for item in screens:
    data.append(dict(id=item.key,name=item.name,board=item.board,note=item.note,hotspots=item.hotspots,
                     image='data:image/png;base64,'+base64.b64encode((OUT/f'{item.key}@3x.png').read_bytes()).decode()))
(OUT/'geometry.json').write_text(json.dumps([{k:v for k,v in row.items() if k!='image'} for row in data],ensure_ascii=False,indent=2),encoding='utf-8')

page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>游戏布局确认稿 · 320×240</title><style>
*{box-sizing:border-box}body{margin:0;background:#e9ede7;color:#243c34;font:15px/1.6 "Microsoft YaHei",sans-serif}main{max-width:1120px;margin:44px auto;padding:0 24px}h1{font-size:28px;margin:0 0 8px}p{margin:8px 0 22px;color:#68776e}.work{display:flex;gap:28px;align-items:flex-start;margin-top:26px}nav{width:146px;display:grid;gap:8px}button{font:inherit;border:0;cursor:pointer;color:inherit;border-radius:8px;background:#f6f7f3;padding:10px 16px;text-align:left}button.active,button.primary{background:#397255;color:white}section{min-width:0;flex:1}.toolbar{display:flex;gap:8px;margin-bottom:16px}.toolbar button{padding:6px 14px}#stage{width:640px;height:480px;position:relative;box-shadow:0 0 0 1px #bac6b7;overflow:hidden;touch-action:none;background:#f6f7f3}#screen{width:100%;height:100%;display:block}#hotspots{position:absolute;inset:0}#hotspots button{position:absolute;border-radius:0;padding:0;background:transparent}#hotspots button:focus-visible{outline:2px solid #397255;outline-offset:-3px}#menu{display:none;position:absolute;inset:0;background:#243c3490;align-items:center;justify-content:center}#menu.open{display:flex}.panel{width:72%;background:#f6f7f3;padding:18px;border-radius:10px;text-align:center}.panel strong{display:block;margin-bottom:12px}.panel button{display:block;width:100%;margin-top:8px;text-align:center;background:#e6ebe3}.panel .primary{background:#397255}#note{max-width:700px;margin-top:20px;color:#243c34}#toast{min-height:28px;color:#397255}.fine{font-size:13px}a{color:#397255}@media(max-width:900px){.work{display:block}nav{width:100%;display:flex;flex-wrap:wrap;margin-bottom:20px}#stage{max-width:100%}main{margin-top:24px}}
</style><main><h1>游戏布局确认稿</h1><p>重点查看棋盘大小、控制位置和触摸区域。这里展示六款代表游戏的布局；画面及菜单供确认，完整游戏逻辑尚未接入此预览。</p><div class="work"><nav id="games"></nav><section><div class="toolbar"><button onclick="setZoom(1)">原尺寸 1×</button><button class="active" onclick="setZoom(2)">放大 2×</button><button onclick="toggleMenu(true)">查看暂停菜单</button></div><div id="stage"><img id="screen" alt="320×240游戏布局"><div id="hotspots"></div><div id="menu"><div class="panel"><strong id="menutitle">暂停</strong><button class="primary" onclick="toggleMenu(false)">继续游戏</button><button onclick="message('重开前显示确认；此处只演示菜单布局')">重新开始</button><button onclick="message('速度、模式、选关和操作说明放在这里')">游戏选项</button><button onclick="toggleMenu(false);message('返回游戏列表；此处只演示布局')">返回游戏厅</button></div></div></div><p id="note"></p><div id="toast" aria-live="polite"></div><p class="fine">可点击画面上的返回、暂停及操作控件查看布局反馈。颜色与字体为示意，屏幕坐标均按320×240绘制。<br><a href="layouts-six.png">查看六款总览</a> · <a href="layout-notes.md">查看其余游戏的布局取舍</a></p></section></div></main><script>
const data=__DATA__;let current=data[0],zoom=2;const byId=id=>document.getElementById(id);
function message(text){byId('toast').textContent=text}
function toggleMenu(open){byId('menu').classList.toggle('open',open);message('')}
function setZoom(n){zoom=n;byId('stage').style.width=320*n+'px';byId('stage').style.height=240*n+'px';document.querySelectorAll('.toolbar button').forEach((b,i)=>b.classList.toggle('active',i===n-1))}
function select(id){current=data.find(x=>x.id===id);byId('screen').src=current.image;byId('screen').alt=current.name+'布局确认稿';byId('note').textContent=current.name+'：'+current.note;toggleMenu(false);document.querySelectorAll('nav button').forEach(b=>b.classList.toggle('active',b.dataset.id===id));byId('hotspots').innerHTML='';for(const h of current.hotspots){const b=document.createElement('button');b.style.cssText=`left:${h.x/3.2}%;top:${h.y/2.4}%;width:${h.w/3.2}%;height:${h.h/2.4}%`;b.setAttribute('aria-label',h.label);b.onclick=()=>{if(h.action==='menu')toggleMenu(true);else message('布局反馈：'+h.label+'。此确认稿不执行完整游戏逻辑。')};byId('hotspots').append(b)}}
for(const item of data){const b=document.createElement('button');b.textContent=item.name;b.dataset.id=item.id;b.onclick=()=>select(item.id);byId('games').append(b)}select(data[0].id);
const track=document.getElementById('hotspots');let dragging=false;track.addEventListener('pointerdown',e=>{const r=track.getBoundingClientRect();if(current.id==='breakout'&&(e.clientY-r.top)/r.height>208/240){dragging=true;track.setPointerCapture(e.pointerId);message('拖动区：手指留在挡板下方，不遮挡球路')}});track.addEventListener('pointermove',e=>{if(dragging)message('拖动区位置：'+Math.round(Math.max(0,Math.min(320,(e.clientX-track.getBoundingClientRect().left)/track.getBoundingClientRect().width*320)))+' / 320')});track.addEventListener('pointerup',()=>dragging=false);
</script></html>'''
page=page.replace('#stage{max-width:100%}', '#stage{max-width:100%;height:auto!important;aspect-ratio:4/3}')
(OUT/'index.html').write_text(page.replace('__DATA__',json.dumps(data,ensure_ascii=False)),encoding='utf-8')
print(f'Created {len(screens)} review layouts, geometry, comparison sheet, and interactive index.html')
