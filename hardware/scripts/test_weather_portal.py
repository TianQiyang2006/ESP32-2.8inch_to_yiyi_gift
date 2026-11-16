"""Compile the production weather adapter and captive route policy on Windows."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/weather-portal-tests'
OUT.mkdir(parents=True, exist_ok=True)
args = [str(ROOT/'tools/host-python/ziglang/zig.exe'), 'c++', '-target', 'x86_64-windows-gnu',
        '-std=c++17', '-O1', '-UNDEBUG', '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE']
for folder in [ROOT/'hardware/tests/host', ROOT/'hardware/arduino/yiyi_desk', ROOT,
               Path.home()/'Documents/Arduino/libraries/lvgl', Path.home()/'Documents/Arduino/libraries/ArduinoJson/src']:
    args += ['-I', str(folder)]
executable = OUT / 'weather-portal-test.exe'
result = subprocess.run(args + [str(ROOT/'hardware/tests/weather_portal_test.cpp'), '-o', str(executable)],
                        capture_output=True, text=True, encoding='utf-8', errors='replace')
if result.returncode:
    raise RuntimeError(result.stderr[-8000:])
subprocess.run([str(executable)], check=True, cwd=ROOT)
