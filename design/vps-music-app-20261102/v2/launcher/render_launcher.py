"""Render the user-selected app order without changing firmware sources."""
from pathlib import Path
import importlib.util
import json
import os
import re
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw


HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
BUILD=ROOT/'build/music-launcher-final-20261102'
SKETCH=ROOT/'hardware/arduino/yiyi_desk'
ZIG=ROOT/'tools/host-python/ziglang/zig.exe'


def music_art():
    # Retain the music icon shown in the first launcher proposal.
    source=HERE.parents[1]/'render_mockup.py'
    spec=importlib.util.spec_from_file_location('music_first_proposal',source)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    image=Image.new('RGBA',(56*3,56*3),(0,0,0,0))
    d=ImageDraw.Draw(image)
    cx=cy=28
    for radius,color in [(27,'#F8EAC9'),(23,'#E7AD43'),(18,'#315D4A')]:
        module.circle(d,(cx-radius,cy-radius,cx+radius,cy+radius),color)
    for radius in (11,15):
        module.circle(d,(cx-radius,cy-radius,cx+radius,cy+radius),None,'#6E926F')
    module.circle(d,(cx-5,cy-5,cx+5,cy+5),'#F2D68F')
    module.circle(d,(cx-1,cy-1,cx+1,cy+1),'#315D4A')
    module.line(d,(cx+7,cy+6,cx+7,cy-14,cx+19,cy-17,cx+19,cy+3),'#FFF8E6',3)
    module.circle(d,(cx+1,cy+3,cx+9,cy+10),'#E9775B')
    module.circle(d,(cx+13,cy,cx+21,cy+7),'#E9775B')
    return image.resize((56,56),Image.Resampling.LANCZOS)


def native_icon(image):
    data=bytearray()
    for r,g,b,a in image.get_flattened_data():
        value=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
        data.extend((value&255,value>>8,a))
    return data


def decode(raw):
    result=Image.new('RGB',(320,240))
    values=[]
    for i in range(0,len(raw),2):
        value=raw[i]|(raw[i+1]<<8)
        values.append((((value>>11)&31)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
    result.putdata(values)
    return result


def main():
    sys.stdout.reconfigure(encoding='utf-8',errors='replace')
    BUILD.mkdir(parents=True,exist_ok=True)
    raw_source=(SKETCH/'src/gift_assets/gift_apps_bg.c').read_text(encoding='utf-8')
    array=re.search(r'=\s*\{(.*?)\};',raw_source,re.S).group(1)
    background=bytes(int(v) for v in re.findall(r'\d+',array))
    assert len(background)==320*240*2
    (BUILD/'background.rgb565').write_bytes(background)
    icon=music_art()
    icon.save(HERE/'music-app-icon.png')
    (BUILD/'music.rgba565').write_bytes(native_icon(icon))
    os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR',str(ROOT/'build/zig-cache/global'))
    os.environ.setdefault('ZIG_LOCAL_CACHE_DIR',str(ROOT/'build/zig-cache/local'))
    common=[str(ZIG),'c++','-target','x86_64-windows-gnu','-O1','-UNDEBUG',
            '-DLV_CONF_INCLUDE_SIMPLE','-DLV_LVGL_H_INCLUDE_SIMPLE','-DGIFT_HOST_TEST','-std=c++17',
            '-include',str(SKETCH/'src/pages/assets/focus_app_icons.h')]
    for directory in [ROOT/'hardware/tests/host',SKETCH,Path.home()/'Documents/Arduino/libraries/lvgl',ROOT]:
        common+=['-I',str(directory)]
    sources=[HERE/'launcher_preview.cpp',
             SKETCH/'src/pages/assets/focus_app_alarm.c',
             SKETCH/'src/pages/assets/focus_app_pomodoro.c',
             SKETCH/'src/pages/assets/tetris_app_icon.c']
    objects=[]
    for source in sources:
        obj=BUILD/(source.stem+'.o')
        subprocess.run(common+['-x','c++','-c',str(source),'-o',str(obj)],cwd=ROOT,check=True)
        objects.append(str(obj))
    args=[line.strip('"') for line in (ROOT/'build/host-lvgl-apps-preview/preview-link.rsp').read_text(encoding='utf-8-sig').splitlines()]
    prefixes=('lv_','fd_','gift_font_','desk_font-','saved_fallback-','gifdec-','lodepng-','qrcodegen-','tjpgd-')
    args=[arg for arg in args[:args.index('-o')]
          if not arg.endswith('.o') or Path(arg).name.startswith(prefixes)]
    executable=BUILD/'launcher-final.exe'
    args+=objects+['-o',str(executable)]
    response=BUILD/'link.rsp'
    response.write_text('\n'.join('"'+arg.replace('\\','/')+'"' for arg in args),encoding='utf-8')
    subprocess.run([str(ZIG),'c++','@'+str(response)],cwd=ROOT,check=True)
    subprocess.run([str(executable),str(BUILD/'background.rgb565'),str(BUILD/'music.rgba565'),str(BUILD)],cwd=ROOT,check=True)
    native=decode((BUILD/'launcher-page-2-final.rgb565').read_bytes())
    native.save(HERE/'launcher-page-2-final-320x240.png')
    native.resize((960,720),Image.Resampling.NEAREST).save(HERE/'launcher-page-2-final-preview-3x.png')
    # The four native rendered tiles must not alter the wallpaper elsewhere.
    original=decode(background)
    outside=ImageChops.difference(native,original)
    mask=Image.new('L',(320,240),0)
    draw=ImageDraw.Draw(mask)
    for x,y in ((64,16),(176,16),(64,125),(176,125)):
        draw.rectangle((x,y,x+79,y+94),fill=255)
    outside.paste((0,0,0),(0,0,320,240),mask)
    assert outside.getbbox() is None,'Launcher pixels outside the four tiles changed.'
    tiles=[]
    for i,name in enumerate(['闹钟','番茄时钟','音乐','俄罗斯方块']):
        x=64+(i%2)*112
        y=16+(i//2)*109
        tiles.append({'name':name,'position':[x,y],'size':[80,95],
                      'icon_position':[12,8],'icon_size':[56,56],
                      'caption_position':[0,71],'caption_size':[80,24]})
    (HERE/'layout.json').write_text(json.dumps({
        'screen_size':[320,240], 'status':'user-approved',
        'order':['闹钟','番茄时钟','音乐','俄罗斯方块'],
        'grid':[2,2],
        'rows':[['闹钟','番茄时钟'],['音乐','俄罗斯方块']],
        'tiles':tiles,'caption_font':'saved_gift_font_bold_14_0',
        'caption_color':'#FBF6E9','caption_background_opacity':0,
        'background_outside_tiles_unchanged':True,
        'preview_renderer':'native-lvgl','firmware_modified':False,'flashed':False,
    },ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PASS: approved 2x2 grid rendered; background outside tiles unchanged.')


if __name__=='__main__':
    main()
