"""Verify the production Barbar clock and picker using actual LVGL pointers.

Compiles into a private C++ cache, exports same-code RGB565/PNG frames and runs
another process against the saved preferences. Does not open a serial port.
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
SKETCH = ROOT / "hardware/arduino/yiyi_desk"
STUB = ROOT / "hardware/tests/host"
LVGL = Path.home() / "Documents/Arduino/libraries/lvgl"
JSON = Path.home() / "Documents/Arduino/libraries/ArduinoJson/src"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="D:/CLion 2026.2.3.1/bin/mingw/bin/g++.exe")
    parser.add_argument("--cache", default="D:/Tian/codex-dashboard-host")
    parser.add_argument("--out", default=str(ROOT / "build/barbar-clock-20261115/host"))
    args = parser.parse_args()
    compiler = Path(args.compiler).resolve()
    cache, out = Path(args.cache), Path(args.out)
    # Preserve the existing ASCII junction: MinGW cannot link through the
    # workspace's Unicode absolute path on this installation.
    objects = cache / "barbar-objects"
    objects.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    runner_out = os.path.relpath(out, ROOT)
    env["GIFT_HOST_PREFS_DIR"] = os.path.join(runner_out, "preferences")
    include = [STUB, SKETCH, LVGL, JSON, ROOT]
    c = list((LVGL / "src").rglob("*.c")) + [SKETCH / "desk_font.c"]
    c += list((SKETCH / "src/gift_assets").glob("*.c"))
    c += list((SKETCH / "src/saved_design").rglob("*.c"))
    c += list((SKETCH / "src/pages/assets").rglob("*.c"))
    cpp = [SKETCH / ("src/" + name + ".cpp") for name in [
        "app_state", "ui_styles", "ui_widgets", "gift_data", "power_status",
        "gift_ui", "gift_lunar", "schedule_data", "schedule_store", "music_player",
        "radio_player", "radio_catalog"]]
    cpp += list((SKETCH / "src/saved_design").rglob("*.cpp"))
    cpp += sorted((SKETCH / "src/pages").glob("*.cpp"))
    cpp += [STUB / "barbar_clock_support.cpp", STUB / "barbar_clock_runner.cpp"]
    headers = list(STUB.glob("*.h")) + list((SKETCH / "src").rglob("*.h"))
    newest_header = max(p.stat().st_mtime for p in headers)

    def object_name(source):
        identity = hashlib.sha1((str(source) + str(compiler)).encode()).hexdigest()[:10]
        return source.stem + "-" + identity + ".o"

    def compile_source(source):
        cpp_source = source.suffix == ".cpp"
        newest = max(source.stat().st_mtime, (SKETCH / "lv_conf.h").stat().st_mtime)
        cached = cache / object_name(source)
        if not cpp_source and cached.is_file() and cached.stat().st_mtime >= newest:
            return cached
        obj = objects / object_name(source)
        if cpp_source:
            newest = max(newest, newest_header)
        if source.name == "barbar_clock_support.cpp":
            newest = max(newest, (STUB / "support.cpp").stat().st_mtime)
        if not obj.is_file() or obj.stat().st_mtime < newest:
            cc = compiler if cpp_source else compiler.with_name("gcc.exe")
            command = [str(cc), "-O1", "-UNDEBUG", "-DGIFT_HOST_TEST",
                       "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE",
                       "-std=c++17" if cpp_source else "-std=c99"]
            if cpp_source:
                command += ["-include", str(STUB / "posix_time.h")]
            elif source.name == "lv_ime_pinyin.c":
                command += ["-Wno-error=incompatible-pointer-types"]
            for path in include:
                command += ["-I", str(path)]
            result = subprocess.run(command + ["-c", str(source), "-o", str(obj)],
                                    capture_output=True, text=True, encoding="utf-8", errors="replace")
            if result.returncode:
                raise RuntimeError(str(source) + "\n" + result.stderr[-10000:])
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        linked = list(pool.map(compile_source, cpp + c))
    executable = objects / "lvgl-barbar-clock.exe"
    result = subprocess.run([str(compiler), *map(str, linked), "-o", str(executable)],
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(result.stderr[-10000:])
    for mode in ([], ["reload"]):
        run = subprocess.run([str(executable), runner_out, *mode], cwd=ROOT, env=env,
                             capture_output=True, text=True, encoding="utf-8", errors="replace")
        logfile = out / ("reload.log" if mode else "main.log")
        logfile.write_text(run.stdout + run.stderr, encoding="utf-8")
        print(run.stdout + run.stderr, end="", flush=True)
        run.check_returncode()
    shots = {}
    for raw in out.glob("*.rgb565"):
        data = raw.read_bytes()
        assert len(data) == 320 * 240 * 2, raw
        pixels = []
        for offset in range(0, len(data), 2):
            value = int.from_bytes(data[offset:offset+2], "little")
            pixels.append(((value >> 11) * 255 // 31, ((value >> 5) & 63) * 255 // 63,
                           (value & 31) * 255 // 31))
        picture = Image.new("RGB", (320, 240)); picture.putdata(pixels)
        picture.save(raw.with_suffix(".png")); shots[raw.stem] = picture
    order = ["clock-picker-first", "clock-picker-barbar", "barbar-seconds",
             "barbar-hidden", "barbar-restored", "barbar-before-midnight",
             "barbar-midnight-moving", "barbar-midnight", "barbar-invalid-time",
             "barbar-time-restored", "barbar-hidden-static", "barbar-next-second",
             "barbar-hidden-final", "barbar-hidden-after-restart"]
    weather_order = ["cloudy", "foggy", "rainy", "snowy", "clear-day", "clear-night",
                     "thunderstorm", "windy", "no-data"]
    weather_hashes = {}
    weather_sheet = Image.new("RGB", (540, 140 * 3), "#303641")
    weather_drawing = ImageDraw.Draw(weather_sheet)
    for i, name in enumerate(weather_order):
        crop = shots["barbar-weather-" + name].crop((10, 206, 37, 232))
        weather_hashes[name] = hashlib.sha256(crop.tobytes()).hexdigest()
        weather_sheet.paste(crop.resize((108, 104), Image.Resampling.NEAREST),
                            ((i % 3) * 180 + 36, (i // 3) * 140 + 4))
        weather_drawing.text(((i % 3) * 180 + 20, (i // 3) * 140 + 112), name, fill="white")
    assert len(set(weather_hashes.values())) == 9, weather_hashes
    weather_sheet.save(out / "weather-contact.png")
    preview = Image.new("RGB", (640, 240))
    preview.paste(shots["barbar-seconds"], (0, 0))
    preview.paste(shots["barbar-hidden"], (320, 0))
    preview.save(out.parent / "barbar-preview.png")
    sheet = Image.new("RGB", (960, 264 * ((len(order)+2)//3)), "#303641")
    drawing = ImageDraw.Draw(sheet)
    for i, name in enumerate(order):
        sheet.paste(shots[name], ((i % 3) * 320, (i // 3) * 264))
        drawing.text(((i % 3) * 320 + 5, (i // 3) * 264 + 244), name, fill="white")
    sheet.save(out / "barbar-contact.png")
    main_report = json.loads((out / "barbar-main.json").read_text())
    reload_report = json.loads((out / "barbar-reload.json").read_text())
    assert main_report["passed"] and reload_report["passed"]
    assert main_report["pointer_driver"] and reload_report["pointer_driver"]
    assert not main_report["seconds"] and not reload_report["seconds"]
    assert main_report["weather_scenes"] == 9
    files = [SKETCH / "src/pages/barbar_clock_page.cpp", SKETCH / "src/gift_data.cpp",
             SKETCH / "src/gift_ui.cpp", SKETCH / "src/pages/mini_apps.h",
             STUB / "barbar_clock_runner.cpp", STUB / "barbar_clock_support.cpp", Path(__file__)]
    files += list((SKETCH / "src/pages/assets").glob("barbar*"))
    report = {"passed": True, "checks": main_report["checks"] + reload_report["checks"],
              "pointer_driver": True, "separate_process_reload": True,
              "lifecycle_cycles": 30, "atomic_save_failure_modes": 3,
              "frames": len(order) + len(weather_order), "preview": str(out.parent / "barbar-preview.png"),
              "weather_scenes": 9, "original_weather_font": "barbar_clock_weather16",
              "weather_crop_hashes": weather_hashes,
              "source_hashes": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    (out / "verification.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "source_hashes"}, indent=2))


if __name__ == "__main__":
    main()
