"""Build/run actual LVGL split-flap pointer, theme and atomic-save checks.

Uses a private C++ object directory and reads the existing native LVGL/font
cache, allowing the dashboard runner to build independently. Opens no serial
port and flashes no hardware.
"""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import os
import subprocess
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / 'hardware/arduino/yiyi_desk'
STUB = ROOT / 'hardware/tests/host'
LVGL = Path.home() / 'Documents/Arduino/libraries/lvgl'
JSON = Path.home() / 'Documents/Arduino/libraries/ArduinoJson/src'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/CLion 2026.2.3.1/bin/mingw/bin/g++.exe')
    parser.add_argument('--cache', default='D:/Tian/codex-dashboard-host')
    parser.add_argument('--out', default=str(ROOT / 'build/flip-clock-theme-20261113'))
    args = parser.parse_args()
    compiler = Path(args.compiler).resolve()
    cache, out = Path(args.cache), Path(args.out)
    # MinGW cannot write an executable through a Unicode directory on this PC.
    objects = cache / 'flip-theme-objects'
    objects.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    runner_out = os.path.relpath(out, ROOT)
    env['GIFT_HOST_PREFS_DIR'] = os.path.join(runner_out, 'preferences')
    include = [STUB, SKETCH, LVGL, JSON, ROOT]
    c = list((LVGL / 'src').rglob('*.c')) + [SKETCH / 'desk_font.c']
    c += list((SKETCH / 'src/gift_assets').glob('*.c'))
    c += list((SKETCH / 'src/saved_design').rglob('*.c'))
    c += list((SKETCH / 'src/pages/assets').rglob('*.c'))
    cpp = [SKETCH / ('src/' + name + '.cpp') for name in [
        'app_state', 'ui_styles', 'ui_widgets', 'gift_data', 'power_status',
        'gift_ui', 'gift_lunar', 'schedule_data', 'schedule_store', 'music_player']]
    cpp += list((SKETCH / 'src/saved_design').rglob('*.cpp'))
    cpp += sorted(p for p in (SKETCH / 'src/pages').glob('*.cpp') if p.name != 'flip_clock_page.cpp')
    cpp += [STUB / 'flip_clock_theme_support.cpp', STUB / 'flip_clock_theme_runtime.cpp',
            STUB / 'flip_clock_theme_runner.cpp']

    def object_name(source):
        identity = hashlib.sha1((str(source) + str(compiler)).encode()).hexdigest()[:10]
        return source.stem + '-' + identity + '.o'

    def compile_source(source):
        cpp_source = source.suffix == '.cpp'
        cached = cache / object_name(source)
        if not cpp_source and cached.is_file():
            return cached
        obj = objects / object_name(source)
        newest = max(source.stat().st_mtime, (SKETCH / 'lv_conf.h').stat().st_mtime)
        if source.name == 'flip_clock_theme_runtime.cpp':
            newest = max(newest, (SKETCH / 'src/pages/flip_clock_page.cpp').stat().st_mtime)
        if cpp_source:
            newest = max(newest, *[p.stat().st_mtime for p in STUB.glob('*.h')],
                         *[p.stat().st_mtime for p in (SKETCH / 'src').rglob('*.h')])
        if not obj.is_file() or obj.stat().st_mtime < newest:
            cc = compiler if cpp_source else compiler.with_name('gcc.exe')
            command = [str(cc), '-O1', '-UNDEBUG', '-DGIFT_HOST_TEST',
                       '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE',
                       '-std=c++17' if cpp_source else '-std=c99']
            if cpp_source:
                command += ['-include', str(STUB / 'posix_time.h')]
            elif source.name == 'lv_ime_pinyin.c':
                command += ['-Wno-error=incompatible-pointer-types']
            for path in include:
                command += ['-I', str(path)]
            result = subprocess.run(command + ['-c', str(source), '-o', str(obj)],
                                    capture_output=True, text=True, encoding='utf-8', errors='replace')
            if result.returncode:
                raise RuntimeError(str(source) + '\n' + result.stderr[-10000:])
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        linked = list(pool.map(compile_source, cpp + c))
    executable = objects / 'lvgl-flip-clock-theme.exe'
    result = subprocess.run([str(compiler), *map(str, linked), '-o', str(executable)],
                            capture_output=True, text=True, encoding='utf-8', errors='replace')
    if result.returncode:
        raise RuntimeError(result.stderr[-10000:])
    subprocess.run([str(executable), runner_out], cwd=ROOT, env=env, check=True)
    # A separate process reloads the actual JSON left by the interaction tests.
    subprocess.run([str(executable), runner_out, 'reload'], cwd=ROOT, env=env, check=True)
    shots = {}
    for raw in out.glob('*.rgb565'):
        data = raw.read_bytes()
        pixels = []
        for offset in range(0, len(data), 2):
            value = int.from_bytes(data[offset:offset+2], 'little')
            pixels.append(((value >> 11) * 255 // 31, ((value >> 5) & 63) * 255 // 63,
                           (value & 31) * 255 // 31))
        image = Image.new('RGB', (320, 240)); image.putdata(pixels)
        image.save(raw.with_suffix('.png')); shots[raw.stem] = image
    order = ['flip-light', 'flip-dark', 'flip-light-folding', 'flip-dark-mid-fold-switch',
             'flip-dark-folding', 'flip-dark-hinge', 'flip-dark-unfolding', 'flip-dark-settled',
             'flip-dark-midnight', 'flip-dark-invalid-time', 'flip-dark-final', 'flip-dark-after-restart',
             'flip-dark-oom-fallback', 'flip-light-oom-fallback', 'flip-dark-oom-recovered-folding']
    sheet = Image.new('RGB', (1280, 264 * ((len(order)+3)//4)), '#303641')
    drawing = ImageDraw.Draw(sheet)
    for i, name in enumerate(order):
        sheet.paste(shots[name], ((i % 4) * 320, (i // 4) * 264))
        drawing.text(((i % 4) * 320 + 5, (i // 4) * 264 + 244), name, fill='white')
    sheet.save(out / 'flip-theme-contact.png')
    files = [SKETCH / 'src/pages/flip_clock_page.cpp', SKETCH / 'src/gift_data.cpp',
             SKETCH / 'src/pages/mini_apps.h', STUB / 'flip_clock_theme_runner.cpp',
             STUB / 'flip_clock_theme_support.cpp', STUB / 'flip_clock_theme_runtime.cpp', Path(__file__)]
    report = {'passed': True,
              'checks': json.loads((out / 'flip-theme-main.json').read_text())['checks'] +
              json.loads((out / 'flip-theme-reload.json').read_text())['checks'],
              'separate_process_reload': True, 'frames': len(shots),
              'source_hashes': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    (out / 'verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    print('Preview:', out / 'flip-theme-contact.png')


if __name__ == '__main__':
    main()
