"""Compile the actual vendored decoders and exercise real MP3/AAC and FM framing.

Native software validation only: no serial port or device access. The optional
FFmpeg reference comparison needs imageio-ffmpeg in build/radio-host-python.
"""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = Path("build/online-fm-20261114/audio-test")
VENDOR = Path("hardware/arduino/yiyi_desk/src/radio_vendor")
COMPILER = Path("D:/CLion 2026.2.3.1/bin/mingw/bin")


def run(args):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        print(result.stdout, result.stderr)
        result.check_returncode()
    return result


def main():
    os.chdir(ROOT)
    os.environ["PATH"] = str(COMPILER) + os.pathsep + os.environ.get("PATH", "")
    OUT.mkdir(parents=True, exist_ok=True)
    include = OUT / "include"
    include.mkdir(exist_ok=True)
    (include / "Arduino.h").write_text("#pragma once\n#include <stdint.h>\n#include <stdlib.h>\n", encoding="utf-8")
    (include / "pgmspace.h").write_text("#pragma once\n#include <stdint.h>\n#define PROGMEM\n#define pgm_read_byte(p) (*(const uint8_t*)(p))\n#define pgm_read_word(p) (*(const uint16_t*)(p))\n#define pgm_read_dword(p) (*(const uint32_t*)(p))\n", encoding="utf-8")
    sources = sorted(VENDOR.glob("libhelix-*/*.c"))

    def compile_c(source):
        obj = OUT / (source.parent.name + "-" + source.stem + ".o")
        run([COMPILER / "gcc.exe", "-std=c11", "-O2", "-DARDUINO=10819", "-DRADIO_NATIVE_TEST", "-I", include, "-c", source, "-o", obj])
        return obj

    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_c, sources))
    exe = OUT / "radio-audio-test.exe"
    run([COMPILER / "g++.exe", "-std=c++17", "-O2", "-DARDUINO=10819", "-I", include,
         "hardware/tests/host/radio_core_test.cpp", *objects, "-o", exe])
    results = {"core": json.loads(run([exe]).stdout), "decoded": {}}
    for name in ("station-332", "station-1260"):
        path = Path("build/online-fm-20261114") / (name + ".mp3")
        if path.exists():
            results["decoded"][name] = json.loads(run([exe, "mp3", path, OUT / name]).stdout)
    sys.path.insert(0, str(ROOT / "build/radio-host-python"))
    try:
        import imageio_ffmpeg
        import numpy as np
    except ImportError:
        print("FFmpeg reference and AAC fixture generation skipped: optional dependencies unavailable")
    else:
        ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
        for rate in (16000, 44100, 48000):
            aac = OUT / (f"tone-{rate}.aac")
            run([ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i", f"sine=frequency=997:sample_rate={rate}:duration=3", "-ac", "2", "-c:a", "aac", "-b:a", "64k", "-f", "adts", aac])
            name = f"aac-{rate}"
            results["decoded"][name] = json.loads(run([exe, "aac", aac, OUT / name]).stdout)
            reference = OUT / (name + ".reference.raw")
            run([ffmpeg, "-v", "error", "-y", "-i", aac, "-f", "s16le", "-acodec", "pcm_s16le", reference])
            actual = np.fromfile(OUT / (name + ".raw"), dtype="<i2").astype(float)
            expected = np.fromfile(reference, dtype="<i2").astype(float)
            assert len(actual) == len(expected), (name, len(actual), len(expected))
            # Both are independently implemented fixed/float AAC decoders.
            signal = expected[4096:]
            error = actual[4096:] - signal
            snr = float(10 * np.log10(np.sum(signal * signal) / max(1., np.sum(error * error))))
            assert snr > 45, (name, snr)
            results["decoded"][name]["ffmpeg_snr_db"] = snr
    results["passed"] = True
    results["limits"] = "Native protocol/codec/PCM checks; device heap, TLS and speaker playback remain untested"
    (OUT / "verification.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
