"""Check LCD frame submission ordering without hardware or LVGL."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=ROOT / 'tools/host-python/ziglang/zig.exe')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/display-frame-commit')
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    env = os.environ.copy()
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    env.setdefault('ZIG_GLOBAL_CACHE_DIR', str(ROOT / 'build/zig-cache/global'))
    env.setdefault('ZIG_LOCAL_CACHE_DIR', str(ROOT / 'build/zig-cache/local'))
    args.out.mkdir(parents=True, exist_ok=True)
    # An ASCII temporary output also works with this Windows MinGW linker when
    # the checkout itself has a Unicode path.
    with tempfile.TemporaryDirectory(prefix='display-frame-') as temporary:
        exe = Path(temporary) / 'display-frame-commit.exe'
        command = [str(compiler)]
        if compiler.stem.lower() == 'zig':
            command += ['c++', '-target', 'x86_64-windows-gnu']
        command += ['-std=c++11', '-O1', '-UNDEBUG', '-Wall', '-Wextra', '-I', str(ROOT),
                    str(ROOT / 'hardware/tests/host/display_frame_commit_test.cpp'), '-o', str(exe)]
        subprocess.run(command, check=True, env=env, cwd=ROOT)
        result = subprocess.run([str(exe)], check=True, env=env, cwd=ROOT,
                                capture_output=True, text=True, encoding='utf-8')
    print(result.stdout, end='')
    (args.out / 'verification.json').write_text(json.dumps({
        'passed': True, 'cxx_standard': 'c++11', 'hardware_accessed': False,
        'result': result.stdout.strip(),
    }, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
