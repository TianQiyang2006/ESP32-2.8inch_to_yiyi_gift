"""Exercise the firmware's actual CTP mapping/read callback with I2C input samples."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
STUB = ROOT / 'hardware/tests/panel'
OUT = ROOT / 'build/panel-driver-test'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / 'panel-driver-test.exe'
    zig = ROOT / 'tools/host-python/ziglang/zig.exe'
    subprocess.run([str(zig), 'c++', '-target', 'x86_64-windows-gnu', '-std=c++17',
                    '-O1', '-UNDEBUG', '-I', str(STUB), '-I', str(ROOT),
                    str(STUB / 'panel_driver_test.cpp'),
                    str(ROOT / 'hardware/arduino/yiyi_desk/src/touch.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
