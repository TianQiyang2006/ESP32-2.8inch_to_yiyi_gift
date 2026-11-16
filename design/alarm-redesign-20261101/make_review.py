"""Render native LVGL alarm proposals and synthesize offline ringtone samples."""
from pathlib import Path
import array
import json
import math
import os
import subprocess
import sys
import wave

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = ROOT / "build/alarm-redesign-preview"
BUILD.mkdir(parents=True, exist_ok=True)
ZIG = ROOT / "tools/host-python/ziglang/zig.exe"
CACHE = ROOT / "build/host-lvgl-apps-preview"
os.environ.setdefault("ZIG_GLOBAL_CACHE_DIR", str(ROOT / "build/zig-cache/global"))
os.environ.setdefault("ZIG_LOCAL_CACHE_DIR", str(ROOT / "build/zig-cache/local"))

includes = [ROOT / "hardware/tests/host", ROOT / "hardware/arduino/yiyi_desk", Path.home() / "Documents/Arduino/libraries/lvgl", Path.home() / "Documents/Arduino/libraries/ArduinoJson/src", ROOT]
obj = BUILD / "alarm_ui_preview.o"
cmd = [str(ZIG), "c++", "-target", "x86_64-windows-gnu", "-O1", "-UNDEBUG", "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE", "-DGIFT_HOST_TEST", "-std=c++17", "-include", str(includes[0] / "posix_time.h")]
for path in includes:
    cmd += ["-I", str(path)]
subprocess.run(cmd + ["-c", str(HERE / "alarm_ui_preview.cpp"), "-o", str(obj)], cwd=ROOT, check=True)
args = [line.strip('"') for line in (CACHE / "preview-link.rsp").read_text(encoding="utf-8").splitlines()]
args = [arg for arg in args[:args.index("-o")] if not Path(arg).name.startswith("pages_runner-")]
exe = BUILD / "alarm-ui-preview.exe"
args += [str(obj), "-o", str(exe)]
rsp = BUILD / "alarm-ui-link.rsp"
rsp.write_text("\n".join('"' + arg.replace("\\", "/") + '"' for arg in args), encoding="utf-8")
subprocess.run([str(ZIG), "c++", "@" + str(rsp)], cwd=ROOT, check=True)
subprocess.run([str(exe), str(BUILD)], cwd=ROOT, check=True)

for source in sorted(BUILD.glob("*.rgb565")):
    raw = source.read_bytes()
    assert len(raw) == 320 * 240 * 2
    pixels = []
    for at in range(0, len(raw), 2):
        value = raw[at] | (raw[at+1] << 8)
        pixels.append((((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
    image = Image.new("RGB", (320, 240))
    image.putdata(pixels)
    image.save(HERE / (source.stem + ".png"))

font = ImageFont.truetype(r"C:\Windows\Fonts\msyhbd.ttc", 23)
note_font = ImageFont.truetype(r"C:\Windows\Fonts\msyh.ttc", 16)

def comparison(filename, screens):
    canvas = Image.new("RGB", (1320, 600), "#F1F4EE")
    draw = ImageDraw.Draw(canvas)
    for x, (image_file, title, note) in zip((20, 660), screens):
        draw.text((x, 16), title, font=font, fill="#213F32")
        image = Image.open(HERE / image_file)
        canvas.paste(image.resize((640, 480), Image.Resampling.NEAREST), (x, 62))
        draw.rectangle((x-1, 61, x+640, 542), outline="#BBC6B8", width=1)
        draw.text((x, 559), note, font=note_font, fill="#51685A")
    canvas.save(HERE / filename)

comparison("alarm-main-and-time-preview.png", [
    ("01-alarm-list.png", "闹钟列表 · 大字与清晰状态", "最多5组，卡片显示重复规则与铃声；截图条目仅为布局示例。"),
    ("02-time-picker.png", "设置时间 · 时 / 分分别选择", "24小时制，分钟步长1；时间与提醒选项分两步设置。"),
])
comparison("alarm-sounds-and-ringing-preview.png", [
    ("04-ringtones.png", "铃声选择 · 本地保存与试听", "三种离线合成铃声，独立闹铃音量；试听和正式响铃可停止。"),
    ("05-ringing.png", "到点响铃 · 大字与大按钮", "自动亮屏提示；停止本次响铃，或稍后10分钟再次提醒。"),
])

RATE, SECONDS = 16000, 8

def envelope(t, length):
    if not 0 <= t < length:
        return 0.0
    return min(1.0, t / 0.008) * min(1.0, (length - t) / 0.015)

def classic(t):
    phase = t % 1.2
    for start in (0.0, 0.26):
        u = phase - start
        if 0 <= u < 0.16:
            return envelope(u, 0.16) * (math.sin(2*math.pi*880*u) + 0.38*math.sin(2*math.pi*1175*u)) / 1.38
    return 0.0

def gentle(t):
    u = t % 1.6
    attack = min(1.0, u / 0.008)
    return attack * math.exp(-3.3*u) * (math.sin(2*math.pi*659.25*u) + 0.38*math.sin(2*math.pi*988.875*u) + 0.19*math.sin(2*math.pi*1318.5*u)) / 1.57

progressive_pulses = []
start, index = 0.0, 0
while start < SECONDS:
    progressive_pulses.append((start, 660 if index % 2 == 0 else 880))
    start += 0.75 - 0.5 * min(start / SECONDS, 1.0)
    index += 1

def progressive(t):
    for start, frequency in progressive_pulses:
        u = t - start
        if 0 <= u < 0.12:
            return envelope(u, 0.12) * math.sin(2*math.pi*frequency*u)
    return 0.0

audio_metadata = []
for key, title, generator in (
    ("classic", "经典滴滴", classic),
    ("gentle", "温柔钟声", gentle),
    ("progressive", "渐快提醒", progressive),
):
    data = array.array("h")
    for sample in range(RATE * SECONDS):
        t = sample / RATE
        fade = 0.2 + 0.8 * min(t / 10.0, 1.0)
        data.append(round(32767 * 0.36 * fade * generator(t)))
    assert max(abs(value) for value in data) < 32767
    assert any(value != 0 for value in data)
    if sys.byteorder != "little":
        data.byteswap()
    target = HERE / ("ringtone-" + key + ".wav")
    with wave.open(str(target), "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(RATE)
        writer.writeframes(data.tobytes())
    with wave.open(str(target), "rb") as reader:
        assert reader.getnframes() == RATE * SECONDS
    audio_metadata.append({"id": key, "name": title, "sample_rate": RATE, "seconds": SECONDS, "file": target.name})

(HERE / "ringtone-samples.json").write_text(json.dumps(audio_metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("PASS: five native 320x240 preview screens; no text clipping")
print("PASS: three 8-second mono PCM ringtone samples; no clipping or empty output")
print("Review assets:", HERE)
