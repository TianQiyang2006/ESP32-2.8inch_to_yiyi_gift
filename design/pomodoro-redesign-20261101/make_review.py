"""Render pomodoro review screens without changing the approved alarm or firmware."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = ROOT / "build/pomodoro-redesign-preview"
BUILD.mkdir(parents=True, exist_ok=True)
ZIG = ROOT / "tools/host-python/ziglang/zig.exe"
CACHE = ROOT / "build/host-lvgl-apps-preview"
os.environ.setdefault("ZIG_GLOBAL_CACHE_DIR", str(ROOT / "build/zig-cache/global"))
os.environ.setdefault("ZIG_LOCAL_CACHE_DIR", str(ROOT / "build/zig-cache/local"))

def check_approved_assets():
    alarm = ROOT / "design/alarm-redesign-20261101"
    approval = json.loads((alarm / "approval-manifest.json").read_text(encoding="utf-8"))
    for filename, digest in approval["files_sha256"].items():
        assert hashlib.sha256((alarm / filename).read_bytes()).hexdigest() == digest, filename
    launcher = ROOT / "design/app-ui-review-20261101"
    manifest = json.loads((launcher / "design-manifest.json").read_text(encoding="utf-8"))
    for filename in ("launcher-approved.png", "launcher-page-1.png", "launcher-page-2.png"):
        assert hashlib.sha256((launcher / filename).read_bytes()).hexdigest() == manifest["assets_sha256"][filename], filename

check_approved_assets()
includes = [ROOT / "hardware/tests/host", ROOT / "hardware/arduino/yiyi_desk", Path.home() / "Documents/Arduino/libraries/lvgl", Path.home() / "Documents/Arduino/libraries/ArduinoJson/src", ROOT]
obj = BUILD / "pomodoro_ui_preview.o"
cmd = [str(ZIG), "c++", "-target", "x86_64-windows-gnu", "-O1", "-UNDEBUG", "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE", "-DGIFT_HOST_TEST", "-std=c++17", "-include", str(includes[0] / "posix_time.h")]
for path in includes:
    cmd += ["-I", str(path)]
subprocess.run(cmd + ["-c", str(HERE / "pomodoro_ui_preview.cpp"), "-o", str(obj)], cwd=ROOT, check=True)
args = [line.strip('"') for line in (CACHE / "preview-link.rsp").read_text(encoding="utf-8").splitlines()]
args = [arg for arg in args[:args.index("-o")] if not Path(arg).name.startswith("pages_runner-")]
exe = BUILD / "pomodoro-ui-preview.exe"
args += [str(obj), "-o", str(exe)]
rsp = BUILD / "pomodoro-ui-link.rsp"
rsp.write_text("\n".join('"' + arg.replace("\\", "/") + '"' for arg in args), encoding="utf-8")
subprocess.run([str(ZIG), "c++", "@" + str(rsp)], cwd=ROOT, check=True)
subprocess.run([str(exe), str(BUILD)], cwd=ROOT, check=True)

names = ["01-ready", "02-focusing", "03-paused", "04-focus-complete", "05-resting", "06-duration-settings", "07-reminder-settings", "08-cycle-complete"]
for name in names:
    raw = (BUILD / (name + ".rgb565")).read_bytes()
    assert len(raw) == 320 * 240 * 2
    pixels = []
    for at in range(0, len(raw), 2):
        value = raw[at] | (raw[at+1] << 8)
        pixels.append((((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
    image = Image.new("RGB", (320, 240))
    image.putdata(pixels)
    image.save(HERE / (name + ".png"))

font = ImageFont.truetype(r"C:\Windows\Fonts\msyhbd.ttc", 23)
note_font = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 16)

def comparison(filename, screens):
    canvas = Image.new("RGB", (1320, 600), "#F1F4EE")
    draw = ImageDraw.Draw(canvas)
    for x, (name, title, note) in zip((20, 660), screens):
        assert draw.textbbox((0, 0), title, font=font)[2] <= 620
        assert draw.textbbox((0, 0), note, font=note_font)[2] <= 620
        draw.text((x, 16), title, font=font, fill="#213F32")
        image = Image.open(HERE / (name + ".png"))
        canvas.paste(image.resize((640, 480), Image.Resampling.NEAREST), (x, 62))
        draw.rectangle((x-1, 61, x+640, 542), outline="#BBC6B8", width=1)
        draw.text((x, 559), note, font=note_font, fill="#51685A")
    canvas.save(HERE / filename)

comparison("pomodoro-ready-and-running-preview.png", [
    ("01-ready", "准备开始 · 清楚的预设与完成记录", "默认25分钟专注、5分钟短休；4轮后长休15分钟。"),
    ("02-focusing", "专注中 · 大字倒计时", "显示轮次和进度；返回桌面后继续计时。"),
])
comparison("pomodoro-paused-and-complete-preview.png", [
    ("03-paused", "暂停 · 保留准确剩余时间", "暂停不计时；继续从17:42接着倒计时，避免跳时。"),
    ("04-focus-complete", "专注完成 · 清楚地进入休息", "完成后默认等待手动开始休息，也可以跳过休息。"),
])
comparison("pomodoro-settings-preview.png", [
    ("06-duration-settings", "时长与轮次 · 独立设置页", "专注、短休、长休和轮数分别设置；运行中的本轮不变。"),
    ("07-reminder-settings", "提醒与衔接 · 自动开始可分别设置", "默认温柔钟声、40%音量；自动休息和自动专注默认关闭。"),
])
comparison("pomodoro-rest-and-cycle-preview.png", [
    ("05-resting", "休息中 · 绿色阶段提示", "休息也可暂停或跳过；休息不会增加已完成番茄数。"),
    ("08-cycle-complete", "本组完成 · 看清累计成果", "四轮完整专注共100分钟；长休后手动开启新一组。"),
])

check_approved_assets()
verification = {
    "date": "2026-11-01",
    "status": "proposal-for-user-review",
    "approved_alarm_assets_unchanged": True,
    "approved_launcher_assets_unchanged": True,
    "native_lvgl_frames": len(names),
    "native_resolution": [320, 240],
    "native_text_width_validation_passed": True,
    "preview_data_is_demonstration_only": True,
    "firmware_modified": False,
    "hardware_flashed": False,
    "timer_behaviour_tested_on_hardware": False,
}
(HERE / "review-verification.json").write_text(json.dumps(verification, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("PASS: 8 native LVGL frames, 4 comparison images, no clipped labels")
print("PASS: approved alarm and launcher assets remain unchanged")
print("Review assets:", HERE)
