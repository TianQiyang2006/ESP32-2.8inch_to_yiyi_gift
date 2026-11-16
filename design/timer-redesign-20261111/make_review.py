"""Render review-only 320x240 LVGL screens and a clickable layout prototype."""
from pathlib import Path
import hashlib
import html
import json
import os
import re
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = ROOT / "build/timer-redesign-preview-20261111"
CACHE = ROOT / "build/host-lvgl-apps-preview"
ZIG = ROOT / "tools/host-python/ziglang/zig.exe"
BUILD.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("ZIG_GLOBAL_CACHE_DIR", str(ROOT / "build/zig-cache/global"))
os.environ.setdefault("ZIG_LOCAL_CACHE_DIR", str(ROOT / "build/zig-cache/local"))
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

inputs = list((ROOT / "hardware/arduino/yiyi_desk").rglob("*.cpp"))
inputs += list((ROOT / "hardware/arduino/yiyi_desk").rglob("*.h"))
before = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}

include = [ROOT / "hardware/tests/host", ROOT / "hardware/arduino/yiyi_desk",
           Path.home() / "Documents/Arduino/libraries/lvgl",
           Path.home() / "Documents/Arduino/libraries/ArduinoJson/src", ROOT]
command = [str(ZIG), "c++", "-target", "x86_64-windows-gnu", "-std=c++17", "-O1", "-UNDEBUG",
           "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE", "-DGIFT_HOST_TEST",
           "-include", str(include[0] / "posix_time.h")]
for path in include:
    command += ["-I", str(path)]
obj = BUILD / "timer-ui-preview.o"
subprocess.run(command + ["-c", str(HERE / "timer_ui_preview.cpp"), "-o", str(obj)], cwd=ROOT, check=True)
objects = sorted(p for p in CACHE.glob("*.o")
                 if re.search(r"-[0-9a-f]{10}\.o$", p.name)
                 and not any(s in p.name for s in ("runner", "preview")))
assert len(objects) > 100, "Build the native LVGL object cache first"
executable = BUILD / "timer-ui-preview.exe"
args = ["-target", "x86_64-windows-gnu", *map(str, objects), str(obj), "-o", str(executable)]
rsp = BUILD / "link.rsp"
rsp.write_text("\n".join('"' + arg.replace("\\", "/") + '"' for arg in args), encoding="utf-8")
subprocess.run([str(ZIG), "c++", "@" + str(rsp)], cwd=ROOT, check=True)
subprocess.run([str(executable), str(BUILD)], cwd=ROOT, check=True)

names = ["01-home", "02-duration", "03-running", "04-paused", "05-sounds", "06-finished"]
for name in names:
    raw = (BUILD / (name + ".rgb565")).read_bytes()
    assert len(raw) == 320 * 240 * 2
    pixels = []
    for at in range(0, len(raw), 2):
        c = int.from_bytes(raw[at:at + 2], "little")
        pixels.append(((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31))
    image = Image.new("RGB", (320, 240)); image.putdata(pixels)
    image.save(HERE / (name + ".png"))

title_font = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 23)
note_font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)


def comparison(filename, items):
    sheet = Image.new("RGB", (1344, 608), "#EDF1E7")
    draw = ImageDraw.Draw(sheet)
    for i, (name, title, note) in enumerate(items):
        x = 24 + i * 656
        assert draw.textbbox((0, 0), title, font=title_font)[2] <= 640
        assert draw.textbbox((0, 0), note, font=note_font)[2] <= 640
        draw.text((x, 16), title, font=title_font, fill="#213F32")
        image = Image.open(HERE / (name + ".png")).resize((640, 480), Image.Resampling.NEAREST)
        sheet.paste(image, (x, 62))
        draw.rectangle((x - 1, 61, x + 640, 542), outline="#BBC6B8")
        draw.text((x, 562), note, font=note_font, fill="#51685A")
    sheet.save(HERE / filename)


comparison("timer-home-and-settings-preview.png", [
    ("01-home", "01  主页面 · 设置倒计时成为独立入口", "主页面完整展示四个常用计时器；点击上方入口再设置时长。"),
    ("02-duration", "02  设置页 · 时 / 分 / 秒独占一页", "大号滚轮；可以直接开始，也可以把当前时长存为常用。"),
])
comparison("timer-running-and-paused-preview.png", [
    ("03-running", "03  计时中 · 放大时间与操作区", "常用项名称、剩余时间、进度与暂停；返回桌面后继续计时。"),
    ("04-paused", "04  已暂停 · 继续按钮清晰可见", "剩余时间保持不变；点击继续，从同一时刻接着倒计时。"),
])
comparison("timer-sounds-and-finished-preview.png", [
    ("05-sounds", "05  提示音 · 单独入口与独立音量", "沿用闹钟三种铃声，保留试听；音量调整不自动播放。"),
    ("06-finished", "06  到时提醒 · 停止或再来一次", "跨桌面页面显示到时提醒；可停止本次提醒或重新计时。"),
])

titles = ["主页面", "设置倒计时", "计时中", "已暂停", "提示音", "到时提醒"]
hotspots = [
    [(12, 44, 296, 52, 1, "设置倒计时"), (236, 2, 72, 36, 4, "提示音"),
     (236, 100, 72, 30, 1, "添加常用计时器"), (12, 134, 296, 102, 2, "选择常用计时器")],
    [(0, 0, 40, 40, 0, "返回"), (148, 198, 160, 40, 2, "开始倒计时")],
    [(0, 0, 40, 40, 0, "返回主页面"), (12, 198, 96, 40, 0, "重置"), (116, 198, 192, 40, 3, "暂停")],
    [(0, 0, 40, 40, 0, "返回主页面"), (12, 198, 96, 40, 0, "重置"), (116, 198, 192, 40, 2, "继续")],
    [(0, 0, 40, 40, 0, "返回"), (12, 198, 296, 40, 0, "返回主页面")],
    [(12, 198, 128, 40, 0, "停止提醒"), (148, 198, 160, 40, 2, "再来一次")],
]
panels = []
for index, (name, title, spots) in enumerate(zip(names, titles, hotspots)):
    buttons = []
    for x, y, w, h, target, label in spots:
        style = f"left:{x / 320 * 100}%;top:{y / 240 * 100}%;width:{w / 320 * 100}%;height:{h / 240 * 100}%"
        buttons.append(f'<button class="hotspot" style="{style}" aria-label="{html.escape(label)}" title="{html.escape(label)}" onclick="show({target})"></button>')
    panels.append(f'<section class="frame" id="screen-{index}" aria-label="{title}" {"" if index == 0 else "hidden"}><img src="{name}.png" alt="{title}设计稿">{"".join(buttons)}</section>')
nav = "".join(f'<button onclick="show({i})" id="nav-{i}">{i + 1:02} {title}</button>' for i, title in enumerate(titles))
prototype = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>倒计时布局 · 设计稿</title>
<style>*{box-sizing:border-box}body{margin:0;background:#EDF1E7;color:#213F32;font:15px/1.7 "Microsoft YaHei",sans-serif}.wrap{max-width:1120px;margin:48px auto;padding:0 24px}h1{font-size:28px;margin:0 0 6px}.intro{color:#51685A;margin:0 0 28px}.layout{display:grid;grid-template-columns:200px minmax(0,1fr);gap:28px}nav{display:flex;flex-direction:column;gap:8px}nav button{border:0;border-radius:8px;padding:12px 16px;color:#315C43;background:#FFFFFF;text-align:left;cursor:pointer;font:inherit}nav button.active{background:#315C43;color:white}.screen{width:100%;max-width:768px}.frame{position:relative;width:100%;aspect-ratio:4/3;box-shadow:0 8px 32px #213f3214;overflow:hidden;border-radius:12px}.frame img{width:100%;display:block;image-rendering:pixelated}.hotspot{position:absolute;border:0;background:transparent;cursor:pointer;border-radius:8px}.hotspot:hover,.hotspot:focus-visible{outline:2px solid #6b946a;outline-offset:-3px;background:#315c430c}.note{color:#51685A;margin:18px 0;font-size:13px}.flow{border-top:1px solid #CCD6C7;margin-top:28px;padding-top:18px}.flow strong{font-weight:600}.flow p{margin:4px 0}@media(max-width:720px){.wrap{margin:24px auto;padding:0 16px}.layout{display:block}nav{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-bottom:18px}nav button{font-size:12px;padding:8px}h1{font-size:23px}.frame{border-radius:6px}}</style>
<main class="wrap"><h1>倒计时 · 页面布局重构</h1><p class="intro">设置时长进入独立页面，主页面留给常用计时器。</p><div class="layout"><nav>__NAV__</nav><div class="screen">__PANELS__<p class="note">320 × 240 原生 LVGL 设计稿。点击画面中的入口或左侧页签可预览页面；时间与常用项为演示内容。滚轮调整、存为常用和铃声试听在此原型中仅作展示。</p></div></div><div class="flow"><strong>页面路径</strong><p>设置倒计时 → 选择时 / 分 / 秒 → 开始倒计时</p><p>常用计时器 → 计时中 → 暂停 / 继续 → 到时提醒</p><p>提示音 → 选择已有铃声和音量 → 保存</p></div></main><script>function show(i){document.querySelectorAll('.frame').forEach((e,n)=>e.hidden=n!==i);document.querySelectorAll('nav button').forEach((e,n)=>e.classList.toggle('active',n===i));}show(0);</script></html>'''
(HERE / "prototype.html").write_text(prototype.replace("__NAV__", nav).replace("__PANELS__", "".join(panels)), encoding="utf-8")

assert all(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest for name, digest in before.items())
verification = {"date": "2026-11-11", "status": "design-for-review", "native_resolution": [320, 240],
                "native_lvgl_screens": 6, "font_glyph_and_text_bounds_checked": True,
                "production_sources_unchanged": True, "serial_opened": False, "firmware_flashed": False,
                "demonstration_data": True, "assets_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                                         for p in sorted(HERE.glob("*.png"))}}
(HERE / "review-verification.json").write_text(json.dumps(verification, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("PASS: six 320x240 native LVGL design screens, three comparison boards and clickable prototype")
print("Design files:", HERE)
