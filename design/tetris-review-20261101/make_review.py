"""Produce review images only; do not edit, build, connect, or flash firmware."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1].parent
HERE = Path(__file__).resolve().parent
BUILD = ROOT / "build/tetris-layout-review-20261101"
BUILD.mkdir(parents=True, exist_ok=True)
SKETCH = ROOT / "hardware/arduino/yiyi_desk"
DRAFT = ROOT / "design/tetris-20261101"
CACHE = ROOT / "build/host-lvgl-apps-preview"
ZIG = ROOT / "tools/host-python/ziglang/zig.exe"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot():
    return {p.relative_to(ROOT).as_posix(): sha(p) for p in SKETCH.rglob("*") if p.is_file()}


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    before = snapshot()
    original_draft = json.loads((DRAFT / "design-manifest.json").read_text(encoding="utf-8-sig"))
    for name, expected in original_draft["assets_sha256"].items():
        assert sha(DRAFT / name) == expected, f"Existing draft asset changed: {name}"
    for name, expected in original_draft["sources_sha256"].items():
        assert sha(ROOT / name) == expected, f"Existing draft source changed: {name}"
    os.environ.setdefault("ZIG_GLOBAL_CACHE_DIR", str(ROOT / "build/zig-cache/global"))
    os.environ.setdefault("ZIG_LOCAL_CACHE_DIR", str(ROOT / "build/zig-cache/local"))
    includes = [ROOT / "hardware/tests/host", SKETCH,
                Path.home() / "Documents/Arduino/libraries/lvgl", ROOT]
    common = [str(ZIG), "c++", "-target", "x86_64-windows-gnu", "-O1", "-UNDEBUG",
              "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE", "-DGIFT_HOST_TEST", "-std=c++17"]
    for path in includes:
        common += ["-I", str(path)]
    preview_object = BUILD / "launcher-review.o"
    subprocess.run(common + ["-c", str(HERE / "launcher_preview.cpp"), "-o", str(preview_object)], cwd=ROOT, check=True)
    args = [line.strip('"') for line in (CACHE / "preview-link.rsp").read_text(encoding="utf-8-sig").splitlines()]
    # The overlay needs only LVGL and fonts; avoid linking unrelated app pages.
    native_prefixes = ("lv_", "fd_", "gift_font_", "desk_font-", "saved_fallback-",
                       "gifdec-", "lodepng-", "qrcodegen-", "tjpgd-")
    args = [arg for arg in args[:args.index("-o")]
            if not arg.endswith(".o") or Path(arg).name.startswith(native_prefixes)]
    if not any("tetris_app_icon" in arg for arg in args):
        icon_object = BUILD / "tetris-icon-review.o"
        subprocess.run(common + ["-x", "c++", "-c", str(SKETCH / "src/pages/assets/tetris_app_icon.c"), "-o", str(icon_object)], cwd=ROOT, check=True)
        args.append(str(icon_object))
    exe = BUILD / "launcher-review.exe"
    args += [str(preview_object), "-o", str(exe)]
    response = BUILD / "launcher-review-link.rsp"
    response.write_text("\n".join('"' + arg.replace("\\", "/") + '"' for arg in args), encoding="utf-8")
    subprocess.run([str(ZIG), "c++", "@" + str(response)], cwd=ROOT, check=True)
    raw_original = ROOT / "build/app-master-volume-20261101/verified-ui/launcher-2.rgb565"
    subprocess.run([str(exe), str(raw_original), str(BUILD)], cwd=ROOT, check=True)
    raw = (BUILD / "01-launcher.rgb565").read_bytes()
    assert len(raw) == 320 * 240 * 2
    pixels = []
    for at in range(0, len(raw), 2):
        value = raw[at] | raw[at + 1] << 8
        pixels.append((((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
    launcher = Image.new("RGB", (320, 240))
    launcher.putdata(pixels)
    launcher.save(HERE / "01-launcher.png")
    original = Image.open(raw_original.with_suffix(".png")).convert("RGB")
    difference = ImageChops.difference(original, launcher)
    allowed = (64, 148, 144, 240)
    outside = difference.copy()
    ImageDraw.Draw(outside).rectangle((allowed[0], allowed[1], allowed[2] - 1, allowed[3] - 1), fill=(0, 0, 0))
    assert outside.getbbox() is None, "An existing launcher pixel changed outside the new entry"
    assert difference.getbbox() is not None, "No new launcher entry was rendered"

    mappings = [("tetris-play.png", "02-play.png"), ("tetris-paused.png", "03-pause.png"),
                ("tetris-gameover.png", "04-gameover.png"), ("tetris-start.png", "05-start.png")]
    for source, destination in mappings:
        shutil.copyfile(DRAFT / source, HERE / destination)
        assert Image.open(HERE / destination).size == (320, 240)
    title_font = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 26)
    panel_font = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 22)
    note_font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
    canvas = Image.new("RGB", (1340, 1248), "#F1F4EE")
    draw = ImageDraw.Draw(canvas)
    draw.text((20, 14), "俄罗斯方块 App · 设计预览", font=title_font, fill="#213F32")
    draw.text((20, 52), "320×240 横屏 · 沿用现有配色和字体 · 草案待确认", font=note_font, fill="#51685A")
    panels = [
        (20, 80, "01-launcher.png", "桌面入口 · 原有两个图标不移动", "新增在第二页下方空位；文字透明，不添加遮罩。"),
        (680, 80, "02-play.png", "游戏中 · 棋盘在左，触控在右", "10×16紧凑棋盘，显示落点虚影、下一个方块和得分。"),
        (20, 664, "03-pause.png", "暂停 · 可继续、重开或返回", "离开App先暂停，返回后手动继续。"),
        (680, 664, "04-gameover.png", "结束 · 本局得分与历史最高", "最高分持久保存；默认无声音。"),
    ]
    for x, y, name, title, note in panels:
        assert draw.textbbox((0, 0), title, font=panel_font)[2] <= 640
        assert draw.textbbox((0, 0), note, font=note_font)[2] <= 640
        draw.text((x, y), title, font=panel_font, fill="#213F32")
        canvas.paste(Image.open(HERE / name).convert("RGB").resize((640, 480), Image.Resampling.NEAREST), (x, y + 36))
        draw.rectangle((x - 1, y + 35, x + 640, y + 516), outline="#BBC6B8", width=1)
        draw.text((x, y + 530), note, font=note_font, fill="#51685A")
    draw.text((20, 1222), "设计提案，非开发板实机截图；本轮只出图，未编辑固件、未连接设备、未刷入。", font=note_font, fill="#51685A")
    canvas.save(HERE / "tetris-design-preview.png")
    assert before == snapshot(), "Firmware workspace changed while generating review images"
    manifest = {
        "status": "proposal-for-user-review", "date": "2026-11-01", "native_resolution": [320, 240],
        "firmware_modified_by_this_review": False, "serial_opened": False, "hardware_flashed": False,
        "native_lvgl_launcher_rendered": True, "original_launcher_reference": raw_original.with_suffix(".png").relative_to(ROOT).as_posix(),
        "new_launcher_entry_rectangle": list(allowed), "changed_pixels_outside_new_entry": 0,
        "launcher_label_font": "saved_gift_font_bold_14_0", "launcher_label_background_opacity": 0,
        "board": {"columns": 10, "visible_rows": 16, "cell_pixels": [16, 15], "variant": "compact-small-display-proposal"},
        "game_frame_origin": "existing proposed draft native LVGL renders; draft assets and source hashes verified",
        "example_data_only": True, "firmware_snapshot_sha256": hashlib.sha256(json.dumps(before, sort_keys=True).encode()).hexdigest(),
        "assets_sha256": {p.name: sha(p) for p in HERE.glob("*.png")},
    }
    (HERE / "review-verification.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("PASS: native launcher extension; all pixels outside the new entry unchanged")
    print("PASS: 4 verified native game frames, review montage, firmware untouched")
    print(HERE / "tetris-design-preview.png")


if __name__ == "__main__":
    main()
