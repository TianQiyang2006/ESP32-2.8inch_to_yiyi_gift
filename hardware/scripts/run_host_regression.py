"""Run the project's host regression suites with the bundled Zig compiler.

`hardware/scripts/test_optimization.py` expects a clang++ executable but this
machine's LLVM install has no C library headers, so it cannot link the host
tests. Bundled Zig ships its own libc headers; this wrapper mirrors that script,
only replacing the compiler driver with `zig c++ -target x86_64-windows-gnu`.
It also links `desk_font.c` (the glyph-coverage table used by gift_data.cpp),
which the original script omits, so the tests link against the current firmware
sources instead of an older dependency set.
"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
ZIG = ROOT / 'tools/host-python/ziglang/zig.exe'
LVGL = Path.home() / 'Documents/Arduino/libraries/lvgl'
JSON = Path.home() / 'Documents/Arduino/libraries/ArduinoJson/src'
TARGET = ['-target', 'x86_64-windows-gnu']
INC = ['-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE']


def main():
    os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR', str(ROOT / 'build/zig-cache/global'))
    os.environ.setdefault('ZIG_LOCAL_CACHE_DIR', str(ROOT / 'build/zig-cache/local'))
    out = ROOT / 'build/host-regression'
    out.mkdir(parents=True, exist_ok=True)
    host = ROOT / 'hardware/tests/host'
    sketch = ROOT / 'hardware/arduino/yiyi_desk'
    include = ['-I', str(host), '-I', str(sketch), '-I', str(ROOT), '-I', str(LVGL), '-I', str(JSON)]
    driver = [str(ZIG), 'c++'] + TARGET
    cc = [str(ZIG), 'cc'] + TARGET
    common = driver + ['-std=c++17', '-O1', '-UNDEBUG', '-DGIFT_LOGIC_TEST'] + INC + include + ['-include', str(host / 'posix_time.h')]

    def run(cmd, **kw):
        print('+', ' '.join(str(c) for c in cmd[:6]), '...', flush=True)
        subprocess.run(cmd, cwd=ROOT, check=True, **kw)

    font_obj = out / 'gift_glyph_coverage.o'
    run(cc + ['-O1'] + INC + include + ['-c', str(sketch / 'src/gift_assets/gift_glyph_coverage.c'), '-o', str(font_obj)])

    sources = [host / 'optimization_regression.cpp', host / 'support.cpp'] + [
        sketch / 'src' / name for name in
        ['app_state.cpp', 'gift_data.cpp', 'schedule_store.cpp', 'schedule_data.cpp', 'pages/days_store.cpp']]
    executable = out / 'optimization-regression.exe'
    run(common + [str(p) for p in sources] + [str(font_obj), '-o', str(executable)])
    run([str(executable)])

    # Pure host logic test: desk_logic.h only, no firmware sources (no Arduino/LVGL).
    router = out / 'gesture-router.exe'
    run(driver + ['-std=c++17', '-O1', '-UNDEBUG', '-I', str(host), '-I', str(sketch),
                  str(host / 'gesture_router_test.cpp'), '-o', str(router)])
    run([str(router)])

    # Pure host logic test: src/nav_logic.h per-App origin table (no firmware sources).
    nav = out / 'nav-origin.exe'
    run(driver + ['-std=c++17', '-O1', '-UNDEBUG', '-I', str(host), '-I', str(sketch),
                  str(host / 'nav_origin_test.cpp'), '-o', str(nav)])
    run([str(nav)])

    # Pure host logic test: src/pages/tetris_engine.h rules (no firmware sources).
    tetris = out / 'tetris-engine.exe'
    run(driver + ['-std=c++17', '-O1', '-UNDEBUG', '-I', str(host), '-I', str(sketch),
                  str(host / 'tetris_engine_test.cpp'), '-o', str(tetris)])
    run([str(tetris)])

    # Pure C++11 Snake rules: buffered touch turns, collisions, timing and food.
    snake = out / 'snake-engine.exe'
    run(driver + ['-std=c++11', '-O1', '-UNDEBUG', '-I', str(host), '-I', str(sketch),
                  str(host / 'snake_engine_test.cpp'), '-o', str(snake)])
    run([str(snake)])

    # Pure C++11 Jump rules: hold/release, landing, scoring and timing.
    jump = out / 'jump-engine.exe'
    run(driver + ['-std=c++11', '-O1', '-UNDEBUG', '-I', str(host), '-I', str(sketch),
                  str(host / 'jump_engine_test.cpp'), '-o', str(jump)])
    run([str(jump)])

    # Pure C++11 Sheep rules: coverage, tray matching, undo and shuffle.
    sheep = out / 'sheep-engine.exe'
    run(driver + ['-std=c++11', '-O1', '-UNDEBUG', '-I', str(host), '-I', str(sketch),
                  str(host / 'sheep_engine_test.cpp'), '-o', str(sheep)])
    run([str(sheep)])

    for name in ('miner', 'plane', 'breakout', 'mines', 'pin', 'memory'):
        game = out / (name + '-engine.exe')
        run(driver + ['-std=c++11', '-O1', '-UNDEBUG', '-I', str(host), '-I', str(sketch),
                      str(host / (name + '_engine_test.cpp')), '-o', str(game)])
        run([str(game)])

    weather = out / 'weather-portal.exe'
    run(common + [str(ROOT / 'hardware/tests/weather_portal_test.cpp'), str(font_obj), '-o', str(weather)])
    run([str(weather)])

    panel = out / 'panel.exe'
    run(driver + ['-std=c++17', '-O1', '-UNDEBUG', '-I', str(ROOT / 'hardware/tests/panel'),
              '-I', str(ROOT), str(ROOT / 'hardware/tests/panel/panel_driver_test.cpp'),
              str(sketch / 'src/touch.cpp'), '-o', str(panel)])
    run([str(panel)])
    run([sys.executable, str(ROOT / 'hardware/scripts/test_audio_output.py')])
    run([sys.executable, str(ROOT / 'hardware/scripts/test_music_playback.py')])
    print('HOST REGRESSION: PASS')


if __name__ == '__main__':
    sys.exit(main())
