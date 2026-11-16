"""Compile the production countdown-day store and test real persistence failures."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='D:/CLion 2026.2.3.1/bin/mingw/bin/g++.exe')
    parser.add_argument('--out',default='build/countdown-days-20261114/backend')
    args=parser.parse_args()
    out=(ROOT/args.out).resolve();out.mkdir(parents=True,exist_ok=True)
    compiler=Path(args.compiler)
    sketch=ROOT/'hardware/arduino/yiyi_desk';host=ROOT/'hardware/tests/host'
    includes=[host,sketch,ROOT,Path.home()/'Documents/Arduino/libraries/lvgl',Path.home()/'Documents/Arduino/libraries/ArduinoJson/src']
    flags=['-O1','-UNDEBUG','-DLV_CONF_INCLUDE_SIMPLE','-DLV_LVGL_H_INCLUDE_SIMPLE','-DGIFT_LOGIC_TEST']
    for include in includes:flags+=['-I',str(include)]
    env=os.environ.copy();env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    # GCC's Windows linker works reliably with an ASCII temporary output path.
    with tempfile.TemporaryDirectory(prefix='yiyi-days-') as temp:
        temporary=Path(temp);coverage=temporary/'coverage.o';exe=temporary/'days-backend.exe'
        subprocess.run([str(compiler.with_name('gcc.exe')),*flags,'-std=c99','-c',str(sketch/'src/gift_assets/gift_glyph_coverage.c'),'-o',str(coverage)],check=True,cwd=ROOT,env=env)
        sources=[host/'days_store_test.cpp',host/'support.cpp',sketch/'src/app_state.cpp',sketch/'src/gift_data.cpp',sketch/'src/pages/days_store.cpp']
        subprocess.run([str(compiler),*flags,'-std=c++17','-include',str(host/'posix_time.h'),*[str(p) for p in sources],str(coverage),'-o',str(exe)],check=True,cwd=ROOT,env=env)
        results=[]
        for mode,directory in [('test',temporary/'test-store'),('seed',temporary/'restart-store'),('restore',temporary/'restart-store')]:
            result=subprocess.run([str(exe),str(directory),mode],cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf-8')
            if result.returncode:
                raise RuntimeError(mode+'\n'+result.stdout+result.stderr)
            print(result.stdout.strip());results.append({'mode':mode,'output':result.stdout.strip()})
        (out/'verification.json').write_text(json.dumps({'passed':True,'separate_process_persistence':True,'results':results},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
