"""Exercise the production battery sampler/state using controlled ADC samples."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def main():
    out = ROOT / 'build/power-status-test'
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.setdefault('ZIG_GLOBAL_CACHE_DIR', str(ROOT / 'build/zig-cache/global'))
    env.setdefault('ZIG_LOCAL_CACHE_DIR', str(ROOT / 'build/zig-cache/local'))
    exe = out / 'power-status-test.exe'
    subprocess.run([
        str(ROOT / 'tools/host-python/ziglang/zig.exe'), 'c++',
        '-target', 'x86_64-windows-gnu', '-std=c++17', '-O1', '-UNDEBUG',
        '-I', str(ROOT / 'hardware/tests/host'), '-I', str(ROOT),
        '-I', str(Path.home() / 'Documents/Arduino/libraries/ArduinoJson/src'),
        str(ROOT / 'hardware/tests/host/power_status_test.cpp'),
        str(ROOT / 'hardware/arduino/yiyi_desk/src/power_status.cpp'),
        '-o', str(exe),
    ], check=True, env=env)
    subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
