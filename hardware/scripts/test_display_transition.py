"""Exercise actual LVGL/display flush ordering using a pixel-recording SPI sink."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT=Path(__file__).resolve().parents[2]
LVGL=Path.home()/'Documents/Arduino/libraries/lvgl'
STUB=ROOT/'hardware/tests/display'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,default=ROOT/'tools/host-python/ziglang/zig.exe')
    parser.add_argument('--obj',type=Path,default=ROOT/'build/host-lvgl')
    parser.add_argument('--out',type=Path,default=ROOT/'build/display-transition-test')
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    compiler=args.compiler.resolve()
    env=os.environ.copy()
    env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    objects=[]
    for source in sorted((LVGL/'src').rglob('*.c')):
        identity=hashlib.sha1((str(source)+str(compiler)).encode()).hexdigest()[:10]
        obj=args.obj/(source.stem+'-'+identity+'.o')
        if not obj.exists():
            raise RuntimeError('Run test_native_lvgl.py with the same --compiler/--obj first; missing '+str(obj))
        objects.append(str(obj))
    # Keep this path unresolved: an ASCII junction lets MinGW link a checkout
    # whose physical path contains Chinese characters.
    exe=args.obj/'display-transition-integration-test.exe'
    command=[str(compiler)]
    if compiler.stem.lower()=='zig':command+=['c++','-target','x86_64-windows-gnu']
    command+=['-std=c++17','-O1','-UNDEBUG','-DLV_CONF_INCLUDE_SIMPLE','-DLV_LVGL_H_INCLUDE_SIMPLE',
              '-I',str(STUB),'-I',str(ROOT),'-I',str(ROOT/'hardware/arduino/yiyi_desk'),'-I',str(LVGL),
              str(STUB/'display_transition_test.cpp'),str(ROOT/'hardware/arduino/yiyi_desk/src/display.cpp'),
              *objects,'-o',str(exe)]
    subprocess.run(command,check=True,env=env,cwd=ROOT)
    results=[]
    for extra in ([],['--fail-internal']):
        result=subprocess.run([str(exe),*extra],check=True,env=env,cwd=ROOT,
                              capture_output=True,text=True,encoding='utf-8')
        print(result.stdout,end='');results.append(result.stdout.strip())
    sources=[STUB/'display_transition_test.cpp',*sorted(STUB.glob('*.h')),
             ROOT/'hardware/arduino/yiyi_desk/src/display.cpp',ROOT/'hardware/arduino/yiyi_desk/src/display.h',
             ROOT/'hardware/arduino/yiyi_desk/src/display_frame_damage.h',ROOT/'hardware/arduino/yiyi_desk/src/display_frame_diff.h',
             ROOT/'hardware/arduino/yiyi_desk/lv_conf.h',ROOT/'hardware/arduino/yiyi_desk/board_config.h',
             Path(__file__).resolve()]
    report={'passed':True,'hardware_accessed':False,'actual_lvgl':True,'actual_display_driver':True,
            'result':results[0],'allocation_fallback_result':results[1],
            'source_sha256':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    (args.out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
