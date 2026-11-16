"""Verify real desktop audio output against captured I2C and I2S operations."""
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    out = ROOT / 'build/audio-output-test'
    stubs = out / 'include'
    stubs.mkdir(parents=True, exist_ok=True)
    hal = ROOT / 'hardware/tests/audio/hal.h'
    # Every SDK header points at the same small fake hardware interface.
    for name in ['Arduino.h', 'Wire.h', 'driver/i2s.h', 'freertos/FreeRTOS.h',
                 'freertos/task.h', 'freertos/semphr.h', 'esp_timer.h']:
        header = stubs / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "' + hal.as_posix() + '"\n', encoding='utf-8')
    os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR', str(ROOT / 'build/zig-cache/global'))
    os.environ.setdefault('ZIG_LOCAL_CACHE_DIR', str(ROOT / 'build/zig-cache/local'))
    exe = out / 'audio-output-test.exe'
    subprocess.run([str(ROOT / 'tools/host-python/ziglang/zig.exe'), 'c++',
                    '-target', 'x86_64-windows-gnu', '-std=c++17', '-O1', '-UNDEBUG',
                    '-I', str(stubs), '-I', str(hal.parent),
                    str(hal.parent / 'audio_output_test.cpp'),
                    str(ROOT / 'hardware/arduino/yiyi_desk/src/audio_board.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    subprocess.run([str(exe), 'register-failure'], check=True)
    subprocess.run([str(exe), 'write-failure'], check=True)
    subprocess.run([str(exe), 'short-write'], check=True)
    subprocess.run([str(exe), 'reminder-ownership'], check=True)
    subprocess.run([str(exe), 'media-ownership'], check=True)


if __name__ == '__main__':
    main()
