"""Host regression checks using the production persistence and validation code."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',required=True,help='Path to clang++')
    args=parser.parse_args()
    compiler=Path(args.compiler).resolve()
    os.environ['PATH']=str(compiler.parent)+os.pathsep+os.environ.get('PATH','')
    out=ROOT/'build/host-regression';out.mkdir(parents=True,exist_ok=True)
    host=ROOT/'hardware/tests/host';sketch=ROOT/'hardware/arduino/yiyi_desk'
    common=[str(compiler),'-std=c++17','-O1','-UNDEBUG','-DLV_CONF_INCLUDE_SIMPLE','-DLV_LVGL_H_INCLUDE_SIMPLE','-include',str(host/'posix_time.h')]
    for path in [host,sketch,ROOT,Path.home()/'Documents/Arduino/libraries/lvgl',Path.home()/'Documents/Arduino/libraries/ArduinoJson/src']:
        common+=['-I',str(path)]
    sources=[host/'optimization_regression.cpp',host/'support.cpp']+[sketch/'src'/name for name in ['app_state.cpp','gift_data.cpp','schedule_store.cpp','schedule_data.cpp','pages/days_store.cpp','gift_assets/gift_glyph_coverage.c']]
    executable=out/'optimization-regression.exe'
    subprocess.run(common+[str(p) for p in sources]+['-o',str(executable)],cwd=ROOT,check=True)
    subprocess.run([str(executable)],cwd=ROOT,check=True)
    weather=out/'weather-portal.exe'
    subprocess.run(common+[str(ROOT/'hardware/tests/weather_portal_test.cpp'),'-o',str(weather)],cwd=ROOT,check=True)
    subprocess.run([str(weather)],cwd=ROOT,check=True)
    panel=out/'panel.exe'
    subprocess.run([str(compiler),'-std=c++17','-O1','-UNDEBUG','-I',str(ROOT/'hardware/tests/panel'),'-I',str(ROOT),str(ROOT/'hardware/tests/panel/panel_driver_test.cpp'),str(sketch/'src/touch.cpp'),'-o',str(panel)],cwd=ROOT,check=True)
    subprocess.run([str(panel)],cwd=ROOT,check=True)

if __name__=='__main__':main()
