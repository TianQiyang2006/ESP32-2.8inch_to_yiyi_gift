"""Reproduce LVGL scroll ownership poisoning, then verify the proposed guard.

No serial/device access. Both runs use real LVGL pointer hit testing and events.
The legacy run must fail with a stuck scroll owner; the fixed run must pass.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / "hardware/arduino/yiyi_desk"
SOURCE = ROOT / "hardware/tests/host/media_list_input_test.cpp"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="D:/CLion 2026.2.3.1/bin/mingw/bin/g++.exe")
    parser.add_argument("--lvgl", default=str(Path.home() / "Documents/Arduino/libraries/lvgl"))
    parser.add_argument("--obj", default="D:/Tian/codex-media-list-input")
    parser.add_argument("--out", default=str(ROOT / "build/online-fm-20261114/media-list-input"))
    args = parser.parse_args()
    compiler, lvgl, objdir, output = map(Path, (args.compiler, args.lvgl, args.obj, args.out))
    objdir.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    os.environ["PATH"] = str(compiler.parent) + os.pathsep + os.environ.get("PATH", "")
    includes = [SKETCH, lvgl]

    def compile_one(source):
        ident = hashlib.sha1((str(source) + str(compiler)).encode()).hexdigest()[:10]
        obj = objdir / (source.stem + "-" + ident + ".o")
        driver = compiler if source.suffix == ".cpp" else compiler.with_name("gcc.exe")
        command = [str(driver), "-O1", "-UNDEBUG", "-DLV_CONF_INCLUDE_SIMPLE",
                   "-DLV_LVGL_H_INCLUDE_SIMPLE",
                   "-std=c++17" if source.suffix == ".cpp" else "-std=c99"]
        for path in includes:
            command += ["-I", str(path)]
        newest = max(source.stat().st_mtime, (SKETCH / "lv_conf.h").stat().st_mtime)
        if source.suffix == ".cpp":
            newest = max(newest, (SKETCH / "src/touch.h").stat().st_mtime,
                         (SKETCH / "src/touch_transform.h").stat().st_mtime)
        if not obj.exists() or obj.stat().st_mtime < newest:
            result = subprocess.run(command + ["-c", str(source), "-o", str(obj)], capture_output=True,
                                    text=True, encoding="utf-8", errors="replace")
            if result.returncode:
                raise RuntimeError(str(source) + "\n" + result.stderr[-12000:])
        return obj

    sources = [SOURCE] + sorted((lvgl / "src").rglob("*.c"))
    with ThreadPoolExecutor(max_workers=6) as pool:
        objects = list(pool.map(compile_one, sources))
    executable = objdir / "media-list-input.exe"
    subprocess.run([str(compiler), *map(str, objects), "-o", str(executable)], check=True)
    results = {}
    scenarios = ("radio_header", "radio_row", "music_header", "music_row", "non_scroll", "first_render")
    for mode in ("legacy", "fixed"):
        results[mode] = {}
        combined_log = ""
        for scenario in scenarios:
            result = subprocess.run([str(executable), mode, scenario], capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=30)
            log = result.stdout + result.stderr
            combined_log += scenario + ":\n" + log
            results[mode][scenario] = {"exit_code": result.returncode, "log": log}
            print(mode + "/" + scenario + ":\n" + log, end="")
        (output / (mode + ".log")).write_text(combined_log, encoding="utf-8")
    reproduced = all(results["legacy"][name]["exit_code"] != 0 and
                     "stale scroll_obj" in results["legacy"][name]["log"] for name in scenarios[:4])
    fixed_passed = all(result["exit_code"] == 0 for result in results["fixed"].values())
    legacy_control_passed = all(results["legacy"][name]["exit_code"] == 0 for name in scenarios[4:])
    report = {"passed": reproduced and fixed_passed and legacy_control_passed, "legacy_failure_reproduced": reproduced,
              "proposed_guard_passed": fixed_passed, "real_lvgl_pointer_events": True,
              "legacy_non_scroll_and_first_render_controls_passed": legacy_control_passed,
              "device_opened": False, "lvgl_version": (lvgl / "lvgl.h").read_text(encoding="utf-8").split("#define LVGL_VERSION_MAJOR", 1)[-1][:180],
              "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              "production_touch_header_sha256": hashlib.sha256((SKETCH / "src/touch.h").read_bytes()).hexdigest(),
              "production_helper": "Touch::suppressSwipeClick", "results": results}
    (output / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not report["passed"]:
        raise SystemExit("Expected legacy failure and fixed-policy pass were not both observed")
    print("PASS: legacy input freeze reproduced; preserving native scroll release fixes all pointer scenarios")


if __name__ == "__main__":
    main()
