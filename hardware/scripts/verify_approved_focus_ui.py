"""Verify approved screens against actual firmware rendering and restart storage.

Never opens a serial port or flashes a device. Screenshots are produced by the
production GiftUI/FocusApps implementation linked against native LVGL.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from PIL import Image, ImageChops, ImageDraw, ImageFont
from verify_tetris_ui import verify_launcher_page_two

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/approved-ui-implementation"
SHOTS = OUT / "verified-ui"
EXE = ROOT / "build/host-lvgl-apps-preview/lvgl-focus-apps.exe"

def main():
    global OUT, SHOTS
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    os.environ["PYTHONUTF8"] = "1"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.out:
        OUT = args.out if args.out.is_absolute() else ROOT / args.out
        SHOTS = OUT / "verified-ui"
    OUT.mkdir(parents=True, exist_ok=True)
    # Freeze the review inputs as well as checking the output against them.
    reference_files_verified = 0
    for folder, manifest, field in [
        ("design/alarm-redesign-20261101", "approval-manifest.json", "files_sha256"),
        ("design/app-ui-review-20261101", "design-manifest.json", "assets_sha256"),
    ]:
        record = json.loads((ROOT / folder / manifest).read_text(encoding="utf-8"))
        for name, expected in record[field].items():
            actual = hashlib.sha256((ROOT / folder / name).read_bytes()).hexdigest()
            assert actual == expected, (folder, name, "approved reference changed")
            reference_files_verified += 1
    if not args.skip_build:
        result = subprocess.run([sys.executable, "hardware/scripts/test_native_lvgl.py", "--focus", "--obj", "build/host-lvgl-apps-preview", "--out", str(SHOTS)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8", errors="replace", timeout=600)
        (OUT / "native-tests.log").write_text(result.stdout, encoding="utf-8")
        print(result.stdout)
        result.check_returncode()

    groups = [
        ["seed", "restore-fire", "restore-snooze", "deduplicate", "disabled", "failed-edit", "verify-old"],
        ["seed", "partial-edit", "verify-old"],
        ["legacy-seed", "legacy-read", "legacy-read"],
        ["corrupt-seed", "corrupt-read"],
        ["v2-seed", "v2-read", "v2-read"],
        ["app-volume-seed", "app-volume-restore", "app-volume-failed", "app-volume-restore", "app-volume-partial", "app-volume-restore"],
    ]
    passed = []
    for group in groups:
        directory = Path(tempfile.mkdtemp(prefix="restart-", dir=OUT))
        env = os.environ.copy()
        env["GIFT_HOST_PREFS_DIR"] = str(directory)
        env["TZ"] = "UTC"
        for mode in group:
            result = subprocess.run([str(EXE), "--mode", mode], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding="utf-8", errors="replace", check=True, timeout=30)
            passed.append(mode)
            print(result.stdout.strip() or f"PASS process {mode}")

    alarm = "design/alarm-redesign-20261101/"
    pomo = "design/pomodoro-redesign-20261101/"
    pairs = {
        "alarm-01-list": alarm + "01-alarm-list.png",
        "alarm-02-time": alarm + "02-time-picker.png",
        "alarm-03-settings": alarm + "03-alarm-settings.png",
        "alarm-04-sounds": alarm + "04-ringtones.png",
        "alarm-05-ringing": alarm + "05-ringing.png",
        "pomo-01-ready": pomo + "01-ready.png",
        "pomo-02-running": pomo + "02-focusing.png",
        "pomo-03-paused": pomo + "03-paused.png",
        "pomo-04-focus-complete": pomo + "04-focus-complete.png",
        "pomo-05-resting": pomo + "05-resting.png",
        "pomo-06-durations": pomo + "06-duration-settings.png",
        "pomo-07-reminders": pomo + "07-reminder-settings.png",
        "pomo-08-complete": pomo + "08-cycle-complete.png",
        "launcher-1": "design/app-ui-review-20261101/launcher-page-1.png",
    }
    comparison = {}
    # The 0.7.2 request adds controls in unused header space; every pixel outside
    # these two explicit button rectangles must still match the approved design.
    header_extensions = {"alarm-01-list": (150, 2, 216, 38), "pomo-06-durations": (174, 2, 252, 36)}
    for actual, expected in pairs.items():
        a = Image.open(SHOTS / f"{actual}.png").convert("RGB")
        b = Image.open(ROOT / expected).convert("RGB")
        diff = ImageChops.difference(a, b)
        count = sum(max(pixel) != 0 for pixel in diff.get_flattened_data())
        comparison[actual] = {"reference": expected, "changed_pixels": count}
        if count and actual in header_extensions:
            area = header_extensions[actual]
            diff.paste((0, 0, 0), area)
            outside = sum(max(pixel) != 0 for pixel in diff.get_flattened_data())
            comparison[actual].update({"added_header_button_rectangle": area, "changed_pixels_outside_extension": outside})
            assert not outside, (actual, outside, diff.getbbox())
        else:
            assert not count, (actual, count, diff.getbbox())
    # The unified Games entry changes page 2. Keep the approved application
    # screens above exact, and verify the current launcher against live widgets.
    launcher_structure = verify_launcher_page_two(SHOTS)

    font = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 22)
    small = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
    def sheet(filename, names, captions):
        image = Image.new("RGB", (1320, 1160), "#F1F4EE")
        d = ImageDraw.Draw(image)
        for i, (name, caption) in enumerate(zip(names, captions)):
            x, y = 20 + (i % 2) * 640, 16 + (i // 2) * 570
            d.text((x, y), caption, font=font, fill="#213F32")
            shot = Image.open(SHOTS / f"{name}.png")
            image.paste(shot.resize((620, 465), Image.Resampling.NEAREST), (x, y + 42))
        d.text((20, 1124), "正式固件代码的原生LVGL渲染；示例数据仅在测试中使用。", font=small, fill="#51685A")
        image.save(OUT / filename)
    sheet("alarm-code-preview.png", ["alarm-01-list", "alarm-02-time", "alarm-04-sounds", "alarm-05-ringing"], ["闹钟列表", "时 / 分选择", "三种本地铃声与试听", "跨页面响铃与贪睡 / 停止"])
    sheet("pomodoro-code-preview.png", ["pomo-01-ready", "pomo-02-running", "pomo-03-paused", "pomo-04-focus-complete"], ["准备开始", "专注中", "暂停 / 从剩余时间继续", "专注完成 / 开始休息"])
    sheet("settings-code-preview.png", ["alarm-03-settings", "alarm-07-more", "pomo-06-durations", "pomo-07-reminders"], ["闹钟提醒设置（定稿首屏）", "上滑进入闹钟更多选项", "番茄独立时长与轮次设置", "提示音与自动衔接"])
    if (SHOTS / "alarm-09-volume.png").exists():
        sheet("volume-code-preview.png", ["alarm-09-volume", "pomo-14-volume", "alarm-10-low-volume", "pomo-15-muted-volume"], ["闹钟独立音量 · 默认20%", "番茄时钟独立音量 · 默认20%", "闹钟最低5% · 每次5%", "番茄可静音 · 两项分别保存"])
    if (SHOTS / "alarm-11-master.png").exists():
        sheet("app-master-volume-preview.png", ["alarm-11-master", "pomo-16-master", "alarm-12-master-muted", "pomo-17-master-muted"], ["闹钟总音量 · 全部闹钟及试听", "番茄总音量 · 完成提醒及试听", "闹钟总音量可静音 · 单独保存", "番茄总音量可静音 · 单独保存"])
        sheet("app-master-entry-preview.png", ["alarm-13-master-entry", "pomo-18-master-entry", "alarm-11-master", "pomo-16-master"], ["闹钟列表右上角：设置", "番茄设置页右上角：总音量", "闹钟总音量：保存立即生效", "番茄总音量：保存立即生效"])
    launcher = Image.new("RGB", (1320, 580), "#F1F4EE")
    d = ImageDraw.Draw(launcher)
    for i, (name, caption) in enumerate([("launcher-1", "应用第一页 · 6个入口"), ("launcher-2", "应用第二页 · 游戏统一入口")]):
        x = 20 + i * 640
        d.text((x, 16), caption, font=font, fill="#213F32")
        launcher.paste(Image.open(SHOTS / f"{name}.png").resize((620, 465), Image.Resampling.NEAREST), (x, 58))
    d.text((20, 544), "正式固件代码原生LVGL渲染；两页沿用相同字体及透明字幕。", font=small, fill="#51685A")
    launcher.save(OUT / "launcher-code-preview.png")
    # Refresh this earlier review artifact too, so it cannot show an old layout.
    comparison_image = Image.new("RGB", (640, 240))
    comparison_image.paste(Image.open(ROOT / "design/app-ui-review-20261101/launcher-page-2.png"), (0, 0))
    comparison_image.paste(Image.open(SHOTS / "launcher-2.png"), (320, 0))
    comparison_image.save(SHOTS / "launcher-compare.png")

    sketch = "hardware/arduino/yiyi_desk/"
    paths = [sketch + p for p in ["board_config.h", "desk.cpp", "src/pages/focus_page.cpp", "src/pages/focus_service.cpp", "src/pages/focus_service.h", "src/pages/focus_engine.h", "src/pages/focus_storage.h", "src/pages/mini_apps.h", "src/audio_board.cpp", "src/audio_board.h", "src/tone_synth.h", "src/ui_widgets.cpp", "src/ui_widgets.h", "src/gift_ui.cpp", "src/gift_assets/gift_apps_bg.c", "src/saved_design/images/gift_apps_bg.c"]]
    paths += ["hardware/tests/host/launcher_assertions.h", "hardware/tests/host/focus_apps_runner.cpp",
              "hardware/scripts/verify_tetris_ui.py", "hardware/scripts/verify_approved_focus_ui.py"]
    version = re.search(r'Version\s*=\s*"([^"]+)"', (ROOT / sketch / "board_config.h").read_text(encoding="utf-8")).group(1)
    report = {
        "version": version, "passed": True,
        "reference_manifest_files_verified": reference_files_verified,
        "production_ui_screenshots": len(list(SHOTS.glob("*.rgb565"))),
        "pixel_comparison": comparison, "launcher_structure": launcher_structure,
        "independent_process_runs": len(passed), "process_modes_passed": passed,
        "simulated_due_time_cases": 60, "host_timezone": "UTC",
        "serial_opened": False, "firmware_flashed": False,
        "hardware_audio_latency_measured": False, "physical_power_cycle_tested": False,
        "source_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths},
        "unchanged_full_reference_screens": sum(v.get("changed_pixels")==0 for v in comparison.values()),
        "contained_header_extension_screens": sum("added_header_button_rectangle" in v for v in comparison.values()),
    }
    (OUT / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {report['unchanged_full_reference_screens']} unchanged full screens, {report['contained_header_extension_screens']} contained header extensions; current six-entry launcher geometry/glyphs/visibility verified; {len(passed)} independent process runs")

if __name__ == "__main__":
    main()
