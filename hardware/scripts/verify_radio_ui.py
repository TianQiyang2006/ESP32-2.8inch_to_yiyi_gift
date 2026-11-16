"""Render and exercise the production LVGL radio page without a device."""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import os
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[2]
SKETCH=ROOT/'hardware/arduino/yiyi_desk'
STUB=ROOT/'hardware/tests/host'
LVGL=Path.home()/'Documents/Arduino/libraries/lvgl'
JSON=Path.home()/'Documents/Arduino/libraries/ArduinoJson/src'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='D:/CLion 2026.2.3.1/bin/mingw/bin/g++.exe')
    parser.add_argument('--obj',default='D:/Tian/codex-radio-host')
    parser.add_argument('--out',default=str(ROOT/'build/online-fm-20261114/ui'))
    args=parser.parse_args()
    compiler=Path(args.compiler);objdir=Path(args.obj);output=Path(args.out).resolve()
    objdir.mkdir(parents=True,exist_ok=True);output.mkdir(parents=True,exist_ok=True)
    os.environ['PATH']=str(compiler.parent)+os.pathsep+os.environ.get('PATH','')
    includes=[STUB,SKETCH,LVGL,JSON,ROOT]
    cpp=[SKETCH/'src/app_state.cpp',SKETCH/'src/pages/radio_page.cpp',STUB/'radio_runner.cpp']
    fonts=['fd_sans_16','fd_sans_bold_12_ui','fd_sans_bold_14_ui','fd_sans_bold_16_ui','fd_sans_bold_16','fd_sans_bold_22','fd_mont_bold_9','fd_mont_bold_13','fd_mont_bold_20']
    c=list((LVGL/'src').rglob('*.c'))+[SKETCH/'src/gift_assets'/f'{name}.c' for name in fonts]
    def compile_one(source):
        cpp_source=source.suffix=='.cpp'
        ident=hashlib.sha1((str(source)+str(compiler)).encode()).hexdigest()[:10]
        obj=objdir/(source.stem+'-'+ident+'.o')
        driver=compiler if cpp_source else compiler.with_name('gcc.exe')
        command=[str(driver),'-O1','-UNDEBUG','-DLV_CONF_INCLUDE_SIMPLE','-DLV_LVGL_H_INCLUDE_SIMPLE','-DGIFT_HOST_TEST','-std=c++17' if cpp_source else '-std=c99']
        for path in includes:command+=['-I',str(path)]
        if cpp_source:command+=['-include',str(STUB/'posix_time.h')]
        newest=max(source.stat().st_mtime,(SKETCH/'lv_conf.h').stat().st_mtime)
        if cpp_source:newest=max(newest,*[p.stat().st_mtime for p in (SKETCH/'src').rglob('*.h')],*[p.stat().st_mtime for p in STUB.glob('*.h')])
        if not obj.exists() or obj.stat().st_mtime<newest:
            result=subprocess.run(command+['-c',str(source),'-o',str(obj)],capture_output=True,text=True,encoding='utf-8',errors='replace')
            if result.returncode:raise RuntimeError(str(source)+'\n'+result.stderr[-12000:])
        return obj
    objects=[compile_one(source) for source in cpp]
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:objects+=list(pool.map(compile_one,c))
    executable=objdir/'radio-ui.exe'
    subprocess.run([str(compiler),*[str(obj) for obj in objects],'-o',str(executable)],check=True)
    # MinGW's narrow fstream/filesystem API does not accept this workspace's
    # Chinese absolute path; give the renderer a checked ASCII directory alias.
    alias=objdir/'radio-screens'
    if not alias.exists():subprocess.run(['cmd','/c','mklink','/J',str(alias),str(output)],check=True,capture_output=True)
    if alias.resolve()!=output.resolve():raise RuntimeError('Radio screenshot alias points to another directory')
    result=subprocess.run([str(executable),str(alias)],capture_output=True,text=True,encoding='utf-8',errors='replace')
    (output/'runner.log').write_text(result.stdout+'\n'+result.stderr,encoding='utf-8')
    print(result.stdout,end='')
    if result.returncode:raise RuntimeError(result.stderr[-12000:])
    shots=[]
    for raw in sorted(output.glob('radio-*.rgb565')):
        data=raw.read_bytes();pixels=[]
        for i in range(0,len(data),2):
            value=int.from_bytes(data[i:i+2],'little');pixels.append(((value>>11)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
        image=Image.new('RGB',(320,240));image.putdata(pixels);path=raw.with_suffix('.png');image.save(path);shots.append(path)
    labels={'radio-01-ready':'准备收听','radio-02-playing':'正在直播','radio-04-volume':'音量抽屉','radio-06-stations':'全部电台','radio-08-favorites':'我的收藏','radio-09-empty-favorites':'收藏为空','radio-10-offline':'离线提示','radio-12-error':'失败重试','radio-19-twenty-player':'20 台 · 收听页','radio-20-twenty-list':'20 台 · 列表','radio-21-twenty-list-bottom':'滚动到第 20 台','radio-22-twenty-last-selected':'选中第 20 台','radio-23-renamed-player':'同数量更新 · 收听页','radio-24-renamed-list':'同数量更新 · 列表','radio-25-empty-player':'零电台 · 收听页','radio-26-empty-list':'零电台 · 列表','radio-27-reordered-after-held-touch':'按住期间变更目录 · 重选'}
    font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',15)
    selected=[p for p in shots if p.stem in labels]
    sheet=Image.new('RGB',(320*4+48,274*((len(selected)+3)//4)+16),(226,231,215));draw=ImageDraw.Draw(sheet)
    for i,path in enumerate(selected):
        x=12+(i%4)*332;y=12+(i//4)*274
        draw.text((x,y),labels[path.stem],fill=(41,75,59),font=font)
        sheet.paste(Image.open(path),(x,y+25))
    sheet.save(output/'radio-contact-sheet.png')
    report=json.loads((output/'runner-verification.json').read_text())
    report['source_sha256']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in cpp+[SKETCH/'src/pages/radio_page.h',SKETCH/'src/radio_station_data.h']}
    report['default_stations']='shared production radio_station_data.h (6 stations)'
    report['program_metadata']='host state demonstration; actual text comes from station'
    report['screenshots']=[str(p.relative_to(ROOT)) for p in shots]
    (output/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Contact sheet:',output/'radio-contact-sheet.png')

if __name__=='__main__':main()
