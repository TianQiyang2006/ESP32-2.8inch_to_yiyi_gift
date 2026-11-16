"""Compile and exercise the production station store, including restart and faults."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/CLion 2026.2.3.1/bin/mingw/bin/g++.exe')
    parser.add_argument('--out', default='build/radio-manager-20261114/backend')
    args = parser.parse_args()
    out = (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler = Path(args.compiler)
    sketch = ROOT / 'hardware/arduino/yiyi_desk'
    host = ROOT / 'hardware/tests/host'
    includes = [host, sketch, ROOT, Path.home() / 'Documents/Arduino/libraries/lvgl', Path.home() / 'Documents/Arduino/libraries/ArduinoJson/src']
    flags = ['-O1', '-UNDEBUG', '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_LVGL_H_INCLUDE_SIMPLE', '-DGIFT_LOGIC_TEST']
    for include in includes:
        flags += ['-I', str(include)]
    env = os.environ.copy()
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    sources = [host / 'radio_catalog_test.cpp', sketch / 'src/radio_catalog.cpp']
    with tempfile.TemporaryDirectory(prefix='yiyi-radio-') as temp:
        temporary = Path(temp)
        coverage = temporary / 'coverage.o'
        exe = temporary / 'radio-catalog.exe'
        subprocess.run([str(compiler.with_name('gcc.exe')), *flags, '-std=c99', '-c', str(sketch / 'src/gift_assets/gift_glyph_coverage.c'), '-o', str(coverage)], check=True, cwd=ROOT, env=env)
        # The host LittleFS adapter uses std::filesystem, so the host harness
        # needs C++17. Production is separately checked with ESP GNU++11;
        # see backend/esp-syntax.json and its compiler logs.
        subprocess.run([str(compiler), *flags, '-std=c++17', *map(str, sources), str(coverage), '-o', str(exe)], check=True, cwd=ROOT, env=env)
        results = []

        def run(mode, directory):
            result = subprocess.run([str(exe), str(directory), mode], cwd=ROOT, env=env, capture_output=True, text=True, encoding='utf-8')
            if result.returncode:
                raise RuntimeError(mode + '\n' + result.stdout + result.stderr)
            print(result.stdout.strip())
            results.append({'mode': mode, 'output': result.stdout.strip()})

        run('test', temporary / 'test-store')
        run('empty', temporary / 'test-store')
        run('maximum-json', temporary / 'maximum-json-store')
        run('restore-maximum-json', temporary / 'maximum-json-store')
        seed = temporary / 'seed-store'
        run('seed', seed)
        for case in ['restart', 'temp-ignored', 'corrupt-main', 'missing-main', 'corrupt-all', 'crc-mismatch', 'invalid-version', 'commit-fault']:
            directory = temporary / case
            shutil.copytree(seed, directory)
            if case == 'restart':
                run('restore', directory)
            elif case == 'temp-ignored':
                (directory / 'radio.tmp').write_text('{"uncommitted":true}', encoding='utf-8')
                run('restore', directory)
            elif case in ['corrupt-main', 'missing-main', 'crc-mismatch', 'invalid-version']:
                if case == 'corrupt-main':
                    (directory / 'radio.json').write_text('{broken', encoding='utf-8')
                elif case == 'missing-main':
                    (directory / 'radio.json').unlink()
                else:
                    body = json.loads((directory / 'radio.json').read_text(encoding='utf-8'))
                    if case == 'invalid-version':
                        body['version'] = True
                    else:
                        body['items'][7]['name'] = 'CRC corruption'
                    (directory / 'radio.json').write_text(json.dumps(body), encoding='utf-8')
                run('recovery', directory)
            elif case == 'corrupt-all':
                (directory / 'radio.json').write_text('{broken', encoding='utf-8')
                (directory / 'radio.bak').write_text('null', encoding='utf-8')
                run('defaults-error', directory)
            else:
                run('failed-commit', directory)
                run('restore-failed-commit', directory)
        hashes = {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest() for p in [*sources, sketch / 'src/radio_catalog.h']}
        report = {'passed': True, 'host_language_standard': 'c++17 (std::filesystem host adapter)', 'production_language_standard': 'gnu++11 (separate ESP syntax check in esp-syntax.json)', 'separate_process_persistence': True, 'production_sources_sha256': hashes, 'results': results}
        (out / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
