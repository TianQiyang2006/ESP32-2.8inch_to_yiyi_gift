"""Render review-only 320x240 designs with the project's actual LVGL 8.4/fonts.

No production source is compiled into or changed by this isolated design runner.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUT = HERE.parent / 'screenshots'
CACHE = ROOT / 'build/host-lvgl'
BUILD = HERE / 'build'
SKETCH = ROOT / 'hardware/arduino/yiyi_desk'
LVGL = Path.home() / 'Documents/Arduino/libraries/lvgl'
ZIG = ROOT / 'tools/host-python/ziglang/zig.exe'


def run(cmd):
    r = subprocess.run([str(x) for x in cmd], cwd=ROOT, capture_output=True,
                       text=True, encoding='utf-8', errors='replace')
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return r.stdout


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    BUILD.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    os.environ['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT/'build/zig-cache/global')
    os.environ['ZIG_LOCAL_CACHE_DIR'] = str(BUILD/'zig-cache')
    source = HERE/'design_runner.cpp'
    obj = BUILD/'design_runner.o'
    # Reuse only the already verified real LVGL and font objects. Regenerate
    # missing library objects locally rather than invoking the production build.
    libraries = sorted(CACHE.glob('lv_*.o'))
    assert len(libraries) > 150, 'Build host LVGL first with test_native_lvgl.py'
    for prefix in ['lodepng-', 'tjpgd-', 'qrcodegen-']:
        libraries += list(CACHE.glob(prefix+'*.o'))
    font_names = ['fd_sans_10', 'fd_sans_12', 'fd_sans_14', 'fd_sans_16',
                  'fd_sans_bold_14', 'fd_sans_bold_14_ui',
                  'fd_sans_bold_16', 'fd_sans_bold_16_ui']
    for name in font_names:
        candidates = list(CACHE.glob(name+'-*.o'))
        assert len(candidates) == 1, (name, candidates)
        libraries += candidates
    run([ZIG, 'c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-O1',
         '-UNDEBUG', '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE',
         '-I', SKETCH, '-I', LVGL, '-I', ROOT, '-c', source, '-o', obj])
    exe = BUILD/'design-review.exe'
    run([ZIG, 'c++', '-target', 'x86_64-windows-gnu', obj, *libraries, '-o', exe])
    result = run([exe, OUT])
    print(result)
    for raw in OUT.glob('*.rgb565'):
        b = raw.read_bytes()
        assert len(b) == 153600
        im = Image.new('RGB', (320, 240))
        values = [int.from_bytes(b[i:i+2], 'little') for i in range(0,len(b),2)]
        im.putdata([((v >> 11)*255//31, ((v>>5)&63)*255//63, (v&31)*255//31)
                    for v in values])
        im.save(raw.with_suffix('.png'))
    names = [('lobby','四款游戏入口'),('runner','小黑快跑'),('slide','拼块逃脱'),
             ('fruit','切水果'),('code','猜色密码'),('pause','统一暂停菜单')]
    sheet = Image.new('RGB', (1040, 628), '#E9EDE7')
    d = ImageDraw.Draw(sheet)
    f = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc', 17)
    small = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 13)
    d.text((24,13),'游戏 App 重构 · 原生 320×240 LVGL 设计',font=f,fill='#243C34')
    for i,(name,title) in enumerate(names):
        x=24+(i%3)*344; y=48+(i//3)*286
        d.text((x,y),title,font=f,fill='#243C34')
        sheet.paste(Image.open(OUT/(name+'.png')), (x,y+27))
    d.text((24,606),'设计确认稿 · 真实 LVGL 字体与像素渲染 · 游戏规则在网页预览中单独验证',font=small,fill='#68776E')
    sheet.save(OUT/'overview.png')
    counters = re.search(r'QA labels=(\d+) glyphs=(\d+) controls=(\d+) bounds=(\d+) taps=(\d+)',result)
    assert counters, result
    counts = dict(zip(['labels','glyphs','controls','bounds','pointer_taps'],map(int,counters.groups())))
    frames = []
    for png in sorted(OUT.glob('*.png')):
        if png.name=='overview.png':continue
        frames.append({'name':png.stem,'size':[320,240],
                       'sha256':hashlib.sha256(png.read_bytes()).hexdigest()})
    report={'passed':True,'phase':'design-review','renderer':'actual LVGL 8.4 software renderer',
            'screen':[320,240],'fonts':'project NotoSansSC 10/12/14/16 px including actual fallbacks',
            'checks':counts,'frames':frames,'production_modified':False,'device_flashed':False,
            'physical_touch_validated':False,'native_game_rules_integrated':False,
            'notes':['Native runner checks labels, real glyph coverage, no text clipping, screen bounds, control dimensions and real LVGL pointer dispatch.',
                     'Decorative geometry has no bitmap assets. Native screens are design samples, not a finished game firmware.'],
            'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
    (HERE/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(counts,ensure_ascii=False))


if __name__ == '__main__':
    main()
