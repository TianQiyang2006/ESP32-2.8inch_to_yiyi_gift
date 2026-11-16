"""Test real music persistence and shuffle/LRC logic, including process restarts."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    out = ROOT / 'build/music-playback-test'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'music-playback-test.exe'
    os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR', str(ROOT / 'build/zig-cache/global'))
    os.environ.setdefault('ZIG_LOCAL_CACHE_DIR', str(ROOT / 'build/zig-cache/local'))
    subprocess.run([str(ROOT / 'tools/host-python/ziglang/zig.exe'), 'c++',
                    '-target', 'x86_64-windows-gnu', '-std=c++17', '-O1', '-UNDEBUG',
                    '-I', str(ROOT / 'hardware/tests/host'),
                    str(ROOT / 'hardware/tests/host/music_playback_test.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    with tempfile.TemporaryDirectory(prefix='restart-', dir=out) as store:
        env = dict(os.environ, GIFT_HOST_PREFS_DIR=store)
        subprocess.run([str(exe), 'write'], env=env, check=True)
        subprocess.run([str(exe), 'read'], env=env, check=True)


if __name__ == '__main__':
    main()
