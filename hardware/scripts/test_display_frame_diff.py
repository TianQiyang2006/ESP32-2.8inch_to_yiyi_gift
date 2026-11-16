"""Reconstruct complete RGB565 frames using the production bounded tile planner."""
from pathlib import Path
import argparse
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='D:/CLion 2026.2.3.1/bin/mingw/bin/g++.exe')
    parser.add_argument('--out', default='build/display-frame-diff-test')
    args = parser.parse_args()
    output = ROOT / args.out
    output.mkdir(parents=True, exist_ok=True)
    # Relative output/source paths keep MinGW's linker away from Unicode paths.
    executable = output / 'display-frame-diff-test.exe'
    command = [args.compiler, '-std=c++11', '-O2', '-UNDEBUG', '-Wall', '-Wextra',
               '-Ihardware/arduino/yiyi_desk',
               'hardware/tests/host/display_frame_diff_test.cpp',
               '-o', str(executable.relative_to(ROOT))]
    subprocess.run(command, cwd=ROOT, check=True)
    environment = os.environ.copy()
    environment['PATH'] = str(Path(args.compiler).parent) + os.pathsep + environment.get('PATH', '')
    result = subprocess.run([str(executable)], cwd=ROOT, env=environment, check=True,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    report = json.loads(result.stdout)
    assert report['passed'] and report['scenarios'] >= 300
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(result.stdout, end='')


if __name__ == '__main__':
    main()
