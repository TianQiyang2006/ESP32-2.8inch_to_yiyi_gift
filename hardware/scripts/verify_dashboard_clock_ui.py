"""Verify all Clock Dashboard layouts, pointer settings and durable storage.

Runs production LVGL offscreen; exports a continuous same-code animation preview.
The preview sampling rate is not a physical-device performance measurement.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]


def run(command, logfile, timeout=1200):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace", timeout=timeout)
    logfile.write_text(result.stdout, encoding="utf-8")
    print(result.stdout, end="", flush=True)
    result.check_returncode()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-legacy", action="store_true", help="Use only after the old-clock regression passed in this run")
    parser.add_argument("--compiler", help="Optional compiler forwarded to the native LVGL harness")
    parser.add_argument("--obj", default="build/host-lvgl")
    parser.add_argument("--out", default="build/dashboard-clock-20261113")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    os.environ["PYTHONUTF8"] = "1"
    output = (ROOT / args.out).resolve()
    # Preserve an explicitly supplied ASCII junction: MinGW's linker cannot
    # open an executable through a Unicode absolute output path on Windows.
    cache = ROOT / args.obj
    output.mkdir(parents=True, exist_ok=True)
    def runner_path(path):
        try:
            return str(path.relative_to(ROOT))
        except ValueError:
            return str(path)
    native = [sys.executable, str(ROOT / "hardware/scripts/test_native_lvgl.py"), "--obj", str(cache)]
    if args.compiler:
        native += ["--compiler", args.compiler]
        os.environ["PATH"] = str(Path(args.compiler).parent) + os.pathsep + os.environ.get("PATH", "")
    if not args.skip_build:
        run(native + ["--dashboard-clock", "--out", runner_path(output / "host")], output / "native.log")
    runner_report = json.loads((output / "host/runner-verification.json").read_text(encoding="utf-8"))
    assert runner_report["passed"] and runner_report["pointer_driver"]
    assert runner_report["lifecycle_cycles"] >= 30
    assert runner_report["clipping_scenes"] == 26 and runner_report["clipping_glyphs"] >= 100
    assert runner_report["overshoot_scenes"] == 5
    assert runner_report["rounded_crown_glyphs"] == 27
    assert runner_report["compact_information_scenes"] == 5
    compact_info = json.loads((output / "host/compact-information-verification.json").read_text(encoding="utf-8"))
    assert len(compact_info) == 5 and all(item["no_overlap"] and item["no_scrolling"] and item["right_column_inset"] == 8 for item in compact_info)
    assert runner_report["colon_clearance_scenes"] >= 825
    assert runner_report["colon_minimum_clear_pixels"] >= 2
    assert runner_report["motion_performance_checks"] >= 200
    motion_checks = json.loads((output / "host/motion-performance-verification.json").read_text(encoding="utf-8"))
    assert motion_checks["passed"] and motion_checks["checks"] == runner_report["motion_performance_checks"]
    assert len(motion_checks["layout_transitions"]) == 4
    assert len(motion_checks["hhmm_second_tilt"]) == 8
    assert all(item["changed_angles"] > 0 and not item["hidden_seconds_repainted"] for item in motion_checks["hhmm_second_tilt"])
    assert len(motion_checks["static_refreshes"]) == 3
    assert all(item["extra_flushes"] == 0 and item["digit_repaints"] == 0 for item in motion_checks["static_refreshes"])
    assert all(item["icon_invalidations"] == 0 for item in motion_checks["static_icons"])
    colon_checks = json.loads((output / "host/colon-clearance-verification.json").read_text(encoding="utf-8"))
    assert len(colon_checks) == runner_report["colon_clearance_scenes"]
    assert all(p["colon_pixels"] > 0 and p["digit_pixels"] > 0 and p["clear_pixels"] >= 2 for p in colon_checks)
    assert {p["form"] for p in colon_checks} >= {
        "dashboard-hhmm", "dashboard-hhmmss", "minimal-hhmm", "minimal-hhmmss", "compact-hhmm"}
    assert {p["phase"] for p in colon_checks} >= {
        "still", "tilt-disabled", "opposing-tilt", "12hour-hidden", "12hour-roll", "seconds-transition", "compact"}

    # Every process starts fresh, sharing only the production LittleFS files.
    # Failed and short writes must retain the previous committed configuration.
    exe = cache / "lvgl-dashboard-clock.exe"
    process_checks = []
    with tempfile.TemporaryDirectory(prefix="clock-config-", dir=output) as store:
        for i, mode in enumerate(("seed", "restore", "failed-write", "restore",
                                  "partial-write", "restore", "invalid-write", "restore")):
            run([str(exe), "--mode", mode, "--store", runner_path(Path(store)), runner_path(output / "restart")],
                output / f"restart-{i:02d}-{mode}.log", timeout=120)
            process_checks.append(mode)
        committed = Path(store) / "gift.json"
        assert committed.is_file()
        state = json.loads(committed.read_text(encoding="utf-8"))
        assert state["dashboardClock"]["color"].lower() == "#659fe8"
        assert state["clockMode"]["preset"] == "compact"
        (output / "restored-clock-config.json").write_text(json.dumps(state["dashboardClock"], ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    legacy = False
    if not args.skip_legacy:
        run(native + ["--mini-clocks", "--out", runner_path(output / "legacy")], output / "legacy-native.log")
        legacy = True
    elif (output / "legacy-native.log").is_file():
        legacy = "PASS:" in (output / "legacy-native.log").read_text(encoding="utf-8")

    shots = output / "host"
    expected = [
        ("dashboard-picker", "六款钟面入口"),
        ("dashboard-hhmm", "天气大字钟 · 时分"),
        ("dashboard-hhmmss", "天气大字钟 · 时分秒"),
        ("minimal-hhmm", "纯时钟 · 时分"),
        ("minimal-hhmmss", "纯时钟 · 时分秒"),
        ("compact-hhmm", "紧凑看板 · 时分"),
        ("settings-colors", "预设配色"),
        ("settings-custom-color", "自定义颜色"),
        ("settings-fonts", "字重、透明度与间距"),
        ("settings-time", "时间显示与倾斜开关"),
        ("settings-weather", "农历、节日及天气动效"),
        ("dashboard-tilt", "随机倾斜与滚动"),
    ]
    font_path = Path("C:/Windows/Fonts/msyh.ttc")
    font = ImageFont.truetype(str(font_path), 16) if font_path.is_file() else ImageFont.load_default()
    review = Image.new("RGB", (1008, 4 * 284 + 12), "#EDF1E8")
    draw = ImageDraw.Draw(review)
    for i, (name, caption) in enumerate(expected):
        image = Image.open(shots / (name + ".png")).convert("RGB")
        assert image.size == (320, 240)
        x, y = 12 + (i % 3) * 332, 12 + (i // 3) * 284
        draw.text((x, y), caption, fill="#294C3D", font=font)
        review.paste(image, (x, y + 28))
    review.save(output / "dashboard-clock-review.png")
    review.crop((0, 0, 1008, 580)).save(output / "dashboard-clock-faces.png")
    sequence = [Image.open(shots / f"motion-{i:03d}.png").convert("RGB") for i in range(60)]
    sequence[0].save(output / "dashboard-clock-motion.gif", save_all=True, append_images=sequence[1:],
                     duration=40, loop=0, disposal=2)
    weights = Image.new("RGB", (1008, 864), "#EDF1E8")
    draw = ImageDraw.Draw(weights)
    for i, weight in enumerate(range(100, 901, 100)):
        x, y = 12 + i % 3 * 332, 12 + i // 3 * 284
        draw.text((x, y), f"字重 {weight}", fill="#294C3D", font=font)
        weights.paste(Image.open(shots / f"dashboard-weight-{weight}.png"), (x, y + 28))
    weights.save(output / "dashboard-clock-weights.png")
    calendar = Image.new("RGB", (1008, 580), "#EDF1E8")
    draw = ImageDraw.Draw(calendar)
    calendar_names = [("dashboard-spring-festival", "春节 · 标准布局"), ("compact-spring-festival", "春节 · 紧凑布局"),
                      ("dashboard-qixi", "七夕"), ("dashboard-winter-solstice", "冬至"),
                      ("dashboard-lichun", "立春 · 干支与生肖"), ("dashboard-calendar-hidden", "农历和节日关闭")]
    for i, (name, caption) in enumerate(calendar_names):
        x, y = 12 + i % 3 * 332, 12 + i // 3 * 284
        draw.text((x, y), caption, fill="#294C3D", font=font)
        calendar.paste(Image.open(shots / (name + ".png")), (x, y + 28))
    calendar.save(output / "dashboard-clock-calendar.png")

    reference = Image.new("RGB", (1008, 296), "#EDF1E8")
    draw = ImageDraw.Draw(reference)
    reference_names = [("reference-dashboard", "天气钟 · 日期／时间／天气三组"),
                       ("reference-compact", "紧凑看板 · 左时间／右日历天气"),
                       ("reference-minimal", "纯时钟 · 居中圆润大字")]
    for i, (name, caption) in enumerate(reference_names):
        x = 12 + i * 332
        draw.text((x, 12), caption, fill="#294C3D", font=font)
        reference.paste(Image.open(shots / (name + ".png")), (x, 40))
    reference.save(output / "dashboard-clock-reference-layouts.png")
    compact_board = Image.new("RGB", (1008, 580), "#EDF1E8")
    draw = ImageDraw.Draw(compact_board)
    compact_captions = [("normal", "紧凑看板 · 整齐分层"), ("long-festival-weather", "最长节日／长城市／-99°／100%"),
                        ("lunar-month", "农历月份与干支"), ("calendar-notes-hidden", "隐藏农历与节日"),
                        ("unavailable", "无天气 · 统一状态提示")]
    for i, (name, caption) in enumerate(compact_captions):
        x, y = 12 + i % 3 * 332, 12 + i // 3 * 284
        draw.text((x, y), caption, fill="#294C3D", font=font)
        compact_board.paste(Image.open(shots / f"compact-info-{name}.png"), (x, y + 28))
    compact_board.save(output / "dashboard-clock-compact-information.png")

    clipping = Image.new("RGB", (1008, 864), "#EDF1E8")
    draw = ImageDraw.Draw(clipping)
    clipping_names = [
        ("clip-dashboard-hhmm-spacing18-angle-60", "天气钟 · 最粗字重／−6°"),
        ("clip-dashboard-hhmm-spacing18-angle60", "天气钟 · 最粗字重／+6°"),
        ("clip-dashboard-hhmmss-spacing8-angle60", "天气钟 · 时分秒／最大字距"),
        ("clip-minimal-hhmm-spacing18-angle-60", "纯时钟 · 最粗字重／−6°"),
        ("clip-minimal-hhmm-spacing18-angle60", "纯时钟 · 最粗字重／+6°"),
        ("clip-minimal-hhmmss-spacing8-angle60", "纯时钟 · 时分秒／最大字距"),
        ("clip-compact-hhmm-spacing18-angle0", "紧凑看板 · 完整字形"),
        ("settings-fonts", "设置页 · 加粗文字"),
        ("dashboard-picker", "钟面入口 · 加粗文字"),
    ]
    for i, (name, caption) in enumerate(clipping_names):
        x, y = 12 + i % 3 * 332, 12 + i // 3 * 284
        draw.text((x, y), caption, fill="#294C3D", font=font)
        clipping.paste(Image.open(shots / (name + ".png")), (x, y + 28))
    clipping.save(output / "dashboard-clock-clipping.png")

    overshoot = json.loads((shots / "overshoot-verification.json").read_text(encoding="utf-8"))
    assert len(overshoot) == 5 and all(p["complete_target_raster"] and p["target_shift_px"] < 0 for p in overshoot)
    overshoot_board = Image.new("RGB", (1008, 580), "#EDF1E8")
    draw = ImageDraw.Draw(overshoot_board)
    overshoot_captions = ["天气钟 · 时分", "天气钟 · 时分秒", "纯时钟 · 时分", "纯时钟 · 时分秒", "紧凑看板 · 时分"]
    for i, entry in enumerate(overshoot):
        x, y = 12 + i % 3 * 332, 12 + i // 3 * 284
        draw.text((x, y), f"{overshoot_captions[i]} · 回弹{entry['target_shift_px']}px", fill="#294C3D", font=font)
        overshoot_board.paste(Image.open(shots / ("overshoot-" + entry["form"] + ".png")), (x, y + 28))
    source_mask = Image.open(shots / "mask-rounded-700-8.pgm").convert("L")
    source_tile = Image.new("RGB", source_mask.size, "black")
    source_tile.paste("white", (0, 0), source_mask)
    draw.text((676, 296), "修正后的圆润数字8 · 原始透明蒙版", fill="#294C3D", font=font)
    source_tile = source_tile.resize((160, 208), Image.Resampling.NEAREST)
    overshoot_board.paste(source_tile, (756, 328))
    overshoot_board.save(output / "dashboard-clock-overshoot.png")
    crowns = json.loads((shots / "crown-verification.json").read_text(encoding="utf-8"))
    assert len(crowns) == 27 and all(p["rounded_crown"] for p in crowns)

    # The old panel is an archived real LVGL screenshot, not a reconstruction.
    # Blue rectangles are verification annotations only, never product widgets.
    previous_shots = ROOT / "build/dashboard-clock-readability-20261113/host"
    previous_preview = previous_shots / "minimal-hhmm.png"
    if not previous_preview.is_file():
        previous_shots = ROOT / "build/dashboard-clock-20261113/host"
        previous_preview = previous_shots / "minimal-hhmm.png"
    new_preview = Image.open(shots / "minimal-hhmm.png").convert("RGB")
    has_archive = previous_preview.is_file() and previous_preview.resolve() != (shots / "minimal-hhmm.png").resolve()
    annotated = new_preview.copy()
    bounds = json.loads((shots / "minimal-container-bounds.json").read_text(encoding="utf-8"))
    annotated_draw = ImageDraw.Draw(annotated)
    for rectangle in bounds:
        annotated_draw.rectangle(rectangle, outline="#3689ff", width=1)
    container_board = Image.new("RGB", (1328, 1044), "#EDF1E8")
    draw = ImageDraw.Draw(container_board)
    panels = [
        (Image.open(previous_preview).convert("RGB") if has_archive else new_preview,
         "修复前 · 已归档的真实LVGL预览 · 08:28" if has_archive else "当前真实LVGL预览 · 旧版归档不可用"),
        (new_preview, "修复后 · 真实LVGL预览 · 08:28"),
        (annotated, "验证辅助 · 蓝框为含透明留白的实际数字容器"),
        (Image.open(shots / "overshoot-minimal-hhmm.png").convert("RGB"), "回弹实帧 · 完整目标字形保留"),
    ]
    for i, (panel, caption) in enumerate(panels):
        x, y = 12 + i % 2 * 664, 12 + i // 2 * 516
        draw.text((x, y), caption, fill="#294C3D", font=font)
        container_board.paste(panel.resize((640, 480), Image.Resampling.NEAREST), (x, y + 28))
    container_board.save(output / "dashboard-clock-container-detail.png")

    # Close-ups use actual archived/new LVGL pixels for the four user-marked
    # forms. Locate each white digit crown inside its time area, excluding the
    # calendar header, and preserve the pixel raster when enlarging it.
    crown_board = Image.new("RGB", (1328, 4 * 136 + 12), "#EDF1E8")
    draw = ImageDraw.Draw(crown_board)
    crown_forms = [("dashboard-hhmm", "天气钟 · 时分"), ("dashboard-hhmmss", "天气钟 · 时分秒"),
                   ("minimal-hhmm", "纯时钟 · 时分"), ("minimal-hhmmss", "纯时钟 · 时分秒")]
    for i, (name, caption) in enumerate(crown_forms):
        for column, directory in enumerate((previous_shots, shots)):
            path = directory / (name + ".png")
            if not path.is_file():
                continue
            panel = Image.open(path).convert("RGB")
            start, end = (60, 180) if name.startswith("dashboard") else (35, 205)
            top = next(y for y in range(start, end) if sum(
                min(panel.getpixel((x, y))) >= 64 and max(panel.getpixel((x, y))) - min(panel.getpixel((x, y))) < 35
                for x in range(8, 312)) >= 24)
            crop = panel.crop((0, max(0, top - 7), 320, min(240, top + 41)))
            x, y = 12 + column * 664, 12 + i * 136
            draw.text((x, y), f"{'修复前 · 归档LVGL' if column == 0 else '修复后 · 真实LVGL'} · {caption}",
                      fill="#294C3D", font=font)
            crown_board.paste(crop.resize((640, 96), Image.Resampling.NEAREST), (x, y + 28))
    crown_board.save(output / "dashboard-clock-digit-top-detail.png")

    # The before panels are actual archived LVGL renders of the same scenes.
    # The third column uses the current maximum-weight, opposing-angle stress
    # scenes; the JSON measures production colon pixels against independently
    # rendered digit pixels rather than repeating the layout's gap formula.
    colon_archive = ROOT / "build/dashboard-clock-reference-20261113/host"
    colon_board = Image.new("RGB", (1008, 4 * 284 + 12), "#EDF1E8")
    draw = ImageDraw.Draw(colon_board)
    colon_archive_hashes = {}
    for i, (name, caption) in enumerate(crown_forms):
        archived = colon_archive / (name + ".png")
        current = shots / (name + ".png")
        if archived.is_file():
            colon_archive_hashes[name] = hashlib.sha256(archived.read_bytes()).hexdigest()
        panels = [(archived if archived.is_file() else current, "修复前 · " + caption if archived.is_file() else "当前 · " + caption),
                  (current, "修复后 · " + caption),
                  (shots / ("colon-" + name + "-tilt.png"), "最粗字重 · 相向倾斜 ±6°")]
        for column, (path, title) in enumerate(panels):
            x, y = 12 + column * 332, 12 + i * 284
            draw.text((x, y), title, fill="#294C3D", font=font)
            colon_board.paste(Image.open(path).convert("RGB"), (x, y + 28))
    colon_board.save(output / "dashboard-clock-colon-clearance.png")

    files = sorted((ROOT / "hardware/arduino/yiyi_desk/src/pages").glob("dashboard_clock*.*"))
    files += sorted((ROOT / "hardware/arduino/yiyi_desk/src/pages/assets").glob("dashboard_clock*.*"))
    files += [ROOT / "hardware/arduino/yiyi_desk/src/gift_ui.cpp", ROOT / "hardware/arduino/yiyi_desk/src/gift_data.cpp",
              ROOT / "hardware/arduino/yiyi_desk/board_config.h",
              ROOT / "hardware/scripts/generate_dashboard_clock_assets.py",
              ROOT / "hardware/scripts/generate_dashboard_calendar.cjs",
              ROOT / "hardware/tests/host/dashboard_clock_runner.cpp", Path(__file__)]
    report = {
        "passed": True, "native_runner": runner_report, "separate_process_checks": process_checks,
        "legacy_clock_regression": legacy, "screenshots": sorted(p.name for p in shots.glob("*.png")),
        "preview": "dashboard-clock-review.png", "animation_preview": "dashboard-clock-motion.gif",
        "clipping_preview": "dashboard-clock-clipping.png",
        "overshoot_preview": "dashboard-clock-overshoot.png", "overshoot_reference_checks": overshoot,
        "container_detail_preview": "dashboard-clock-container-detail.png",
        "reference_layout_preview": "dashboard-clock-reference-layouts.png",
        "rounded_top_detail_preview": "dashboard-clock-digit-top-detail.png",
        "rounded_crown_checks": crowns,
        "colon_clearance_preview": "dashboard-clock-colon-clearance.png",
        "colon_clearance_checks": colon_checks,
        "motion_performance_checks": motion_checks,
        "compact_information_checks": compact_info,
        "compact_information_preview": "dashboard-clock-compact-information.png",
        "colon_clearance_oracle": "Chebyshev distance between isolated production colon pixels and independently rendered production digit descriptors; all visible RGB565 pixels included",
        "colon_archived_before_sha256": colon_archive_hashes,
        "archived_before_preview_sha256": hashlib.sha256(previous_preview.read_bytes()).hexdigest() if has_archive else None,
        "animation_preview_sampling_ms": 40, "animation_preview_is_device_fps": False,
        "source_sha256": {str(p.relative_to(ROOT)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        "firmware_flashed": False, "physical_device_tested": False,
    }
    (output / "ui-verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: Clock Dashboard production LVGL, pointer settings, {len(process_checks)} restart checks; preview {output / 'dashboard-clock-review.png'}")


if __name__ == "__main__":
    main()
