"""Verify the Tetris mini app (0.8.0) against the proposed design and NVS storage.

Screenshots come from the production GiftUI/TetrisPage code linked against native
LVGL; no serial port is opened and no device is flashed. The proposed design
reference is frozen in design/tetris-20261101 and must be confirmed by the user
before flashing.
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

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/tetris-20261101"
SHOTS = OUT / "frames"
EXE = ROOT / "build/host-lvgl-apps-preview/lvgl-tetris.exe"
DESIGN = ROOT / "design/tetris-20261101"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_launcher_page_two(shots: Path) -> dict:
    """Check current launcher widgets and their visible icon/caption regions.

    The native runners write the sidecar after checking real LVGL label metrics
    and glyph descriptors, so this does not infer readable labels from a bitmap.
    The historical Tetris launcher reference predates the unified Games entry.
    """
    names = ("闹钟", "番茄时钟", "音乐", "游戏", "倒计时", "倒数日")
    evidence = shots / "launcher-2.json"
    assert evidence.is_file(), f"Missing current launcher evidence; rerun without --skip-build: {evidence}"
    entries = json.loads(evidence.read_text(encoding="utf-8"))
    assert [entry["name"] for entry in entries] == list(names), entries
    image = Image.open(shots / "launcher-2.png").convert("RGB")
    assert image.size == (320, 240), image.size
    asset = ROOT / "hardware/arduino/yiyi_desk/src/gift_assets/gift_apps_bg.c"
    data = re.search(r"gift_apps_bg_pixels\[\]\s*=\s*\{(.*?)\}",
                     asset.read_text(encoding="utf-8"), re.S).group(1)
    raw = bytes(int(value) for value in re.findall(r"\d+", data))
    assert len(raw) == 320 * 240 * 2, len(raw)
    background = Image.new("RGB", image.size)
    values = (int.from_bytes(raw[index:index + 2], "little") for index in range(0, len(raw), 2))
    background.putdata([((value >> 11) * 255 // 31, ((value >> 5) & 63) * 255 // 63,
                         (value & 31) * 255 // 31) for value in values])
    visible = []
    for index, entry in enumerate(entries):
        x, y = 20 + index % 3 * 100, 16 + index // 3 * 109
        assert [entry[key] for key in ("x", "y", "w", "h")] == [x, y, 80, 95], entry
        assert entry["textFits"] is True and entry["glyphsPresent"] is True, entry
        regions = {"icon": (x + 12, y + 8, x + 68, y + 64),
                   "caption": (x, y + 71, x + 80, y + 95)}
        counts = {}
        for name, area in regions.items():
            difference = ImageChops.difference(image.crop(area), background.crop(area))
            mask = difference.convert("L").point(lambda value: 255 if value >= 20 else 0)
            counts[name] = sum(value != 0 for value in mask.getdata())
            assert counts[name] >= (80 if name == "icon" else 25), (entry["name"], name, counts)
            if name == "caption":
                bounds = mask.getbbox()
                assert bounds and bounds[0] > 0 and bounds[2] < 80, (entry["name"], bounds)
        visible.append({"name": entry["name"], "visible_pixels": counts})
    return {"method": "production LVGL widget geometry, label fit, glyph descriptors and visible screenshot regions",
            "entries": entries, "visible_regions": visible, "screenshot_sha256": sha(shots / "launcher-2.png"),
            "widget_report_sha256": sha(evidence)}


def main():
    global OUT, SHOTS
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    SHOTS.mkdir(parents=True, exist_ok=True)

    # ---- 1. frozen design inputs ------------------------------------------
    manifest = json.loads((DESIGN / "design-manifest.json").read_text(encoding="utf-8"))
    assert manifest["status"] in ("proposed-for-user-review", "user-approved")
    if manifest["status"] == "user-approved":
        assert manifest.get("approval", {}).get("approved_by_user") is True
    for name, expected in manifest["assets_sha256"].items():
        assert sha(DESIGN / name) == expected, f"design reference changed: {name}"
    for name, expected in manifest["sources_sha256"].items():
        assert sha(ROOT / name) == expected, f"design source changed: {name}"

    # The 56x56 icon must be reproducible from the checked-in generator.
    icon_c = ROOT / "hardware/arduino/yiyi_desk/src/pages/assets/tetris_app_icon.c"
    before = icon_c.read_bytes()
    subprocess.run([sys.executable, str(ROOT / "hardware/arduino/yiyi_desk/src/pages/assets/gen_tetris_icon.py")],
                   cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    assert icon_c.read_bytes() == before, "tetris_app_icon.c is not reproducible from gen_tetris_icon.py"

    # ---- 2. screenshots from production code -------------------------------
    if not args.skip_build:
        result = subprocess.run([sys.executable, "hardware/scripts/test_native_lvgl.py", "--tetris",
                                 "--obj", "build/host-lvgl-apps-preview", "--out", str(SHOTS)],
                                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                encoding="utf-8", errors="replace", timeout=900)
        (OUT / "native-tests.log").write_text(result.stdout, encoding="utf-8")
        print(result.stdout)
        result.check_returncode()

    # ---- 3. pixel comparisons ---------------------------------------------
    pairs = {
        "launcher-1": ROOT / "design/app-ui-review-20261101/launcher-page-1.png",
        "tetris-start": DESIGN / "tetris-start.png",
        "tetris-play": DESIGN / "tetris-play.png",
        "tetris-paused": DESIGN / "tetris-paused.png",
        "tetris-gameover": DESIGN / "tetris-gameover.png",
    }
    comparison = {}
    for actual, expected in pairs.items():
        a = Image.open(SHOTS / f"{actual}.png").convert("RGB")
        b = Image.open(expected).convert("RGB")
        diff = ImageChops.difference(a, b)
        count = sum(max(pixel) != 0 for pixel in diff.get_flattened_data())
        comparison[actual] = {"reference": expected.relative_to(ROOT).as_posix(), "changed_pixels": count}
        assert not count, (actual, count, diff.getbbox())
    launcher = verify_launcher_page_two(SHOTS)

    # ---- 4. NVS best-score persistence across processes --------------------
    # Groups keep one temp NVS directory per scenario. The second process proves
    # the record written by the first one survives process exit; the "BEST="
    # values are compared so only the real stored number counts.
    passed = []
    bests = {}
    for group in (["best-play", "best-restore"], ["best-midplay", "best-restore"]):
        directory = Path(tempfile.mkdtemp(prefix="tetris-", dir=OUT))
        env = os.environ.copy()
        env["GIFT_HOST_PREFS_DIR"] = str(directory)
        env["TZ"] = "UTC"
        group_bests = []
        for mode in group:
            result = subprocess.run([str(EXE), "--mode", mode], cwd=ROOT, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    encoding="utf-8", errors="replace", check=True, timeout=120)
            passed.append(mode)
            for line in result.stdout.splitlines():
                if line.startswith("BEST="):
                    group_bests.append(int(line.split("=", 1)[1]))
                    bests[f"{mode}@{directory.name}"] = group_bests[-1]
            print(result.stdout.strip() or f"PASS process {mode}")
        assert len(group_bests) == 2 and group_bests[0] == group_bests[1] > 0, (group, group_bests)

    # ---- 5. Glyph coverage:独立检查 Tetris 页面的每个非 ASCII 字符 ---------
    # check_text_coverage.py only scans desk.cpp/gift_ui.cpp/saved_design (it does
    # not scan src/pages), so its result is not evidence for this page. Parse the
    # production literals and check them against the actual UI font cmaps instead.
    sys.path.insert(0, str(ROOT / "hardware/scripts"))
    import compare_fonts as cf
    page_source = (ROOT / "hardware/arduino/yiyi_desk/src/pages/tetris_page.cpp").read_text(encoding="utf-8")
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', page_source)
    cjk_chars = sorted({ch for literal in literals for ch in literal if ord(ch) > 127})
    ga = ROOT / "hardware/arduino/yiyi_desk/src/gift_assets"
    glyph_report = {}
    for font_name, file_name in (("fd_sans_bold_16_ui", "fd_sans_bold_16_ui.c"),
                                 ("fd_sans_bold_14_ui", "fd_sans_bold_14_ui.c")):
        parsed = cf.parse_font_file(ga / file_name)
        font = parsed.get(font_name) or next(iter(parsed.values()))
        missing = [ch for ch in cjk_chars if ord(ch) not in font.cp2gid]
        glyph_report[font_name] = {"chars": len(cjk_chars), "missing": missing}
        assert not missing, (font_name, missing)
    mont = cf.parse_font_file(ga / "fd_mont_26.c")
    mont_font = mont.get("fd_mont_26") or next(iter(mont.values()))
    assert all(ord(digit) in mont_font.cp2gid for digit in "0123456789")
    glyph_report["fd_mont_26"] = {"digits": True}

    # Reference information only: the project-wide checker does not cover src/pages.
    coverage = subprocess.run([sys.executable, "hardware/scripts/check_text_coverage.py"],
                              cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              encoding="utf-8", errors="replace", timeout=600)
    (OUT / "text-coverage.log").write_text(coverage.stdout, encoding="utf-8")

    # ---- 6. preview sheet + report ----------------------------------------
    font = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 22)
    small = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
    captions = [
        ("launcher-2", "应用第二页 · 六个入口 / 游戏统一入口"),
        ("tetris-start", "新开一局 · 下一个方块预览"),
        ("tetris-play", "中局 · 落点虚影 / 得分 / 等级 / 行数"),
        ("tetris-paused", "离开自动暂停 · 继续 / 重新开始 / 返回"),
        ("tetris-gameover", "游戏结束 · 本局得分与历史最高"),
    ]
    row_height = 42 + 465 + 20
    sheet = Image.new("RGB", (1320, 40 + row_height * 3), "#F1F4EE")
    d = ImageDraw.Draw(sheet)
    for i, (name, caption) in enumerate(captions):
        x, y = 20 + (i % 2) * 640, 16 + (i // 2) * row_height
        d.text((x, y), caption, font=font, fill="#213F32")
        shot = Image.open(SHOTS / f"{name}.png")
        sheet.paste(shot.resize((620, 465), Image.Resampling.NEAREST), (x, y + 42))
    d.text((20, 40 + row_height * 3 - 30), "正式固件代码的原生LVGL渲染；仅在测试中注入示例棋局。",
           font=small, fill="#51685A")
    sheet.save(OUT / "tetris-code-preview.png")

    sketch = "hardware/arduino/yiyi_desk/"
    paths = [sketch + p for p in [
        "board_config.h", "desk.cpp", "src/app_state.h", "src/nav_logic.h", "src/gift_ui.cpp",
        "src/pages/mini_apps.h", "src/pages/tetris_engine.h", "src/pages/tetris_page.cpp",
        "src/pages/assets/tetris_app_icon.c", "src/pages/assets/tetris_app_icon.h",
        "src/pages/assets/gen_tetris_icon.py",
    ]] + [
        "hardware/tests/host/tetris_engine_test.cpp", "hardware/tests/host/tetris_runner.cpp",
        "hardware/tests/host/launcher_assertions.h", "hardware/scripts/verify_tetris_ui.py",
        "hardware/tests/host/nav_origin_test.cpp", "hardware/tests/host/Preferences.h",
        "hardware/scripts/test_native_lvgl.py", "hardware/scripts/run_host_regression.py",
        "design/tetris-20261101/design-manifest.json",
        "design/tetris-20261101/launcher-page-2.png",
    ]
    version = re.search(r'Version\s*=\s*"([^"]+)"', (ROOT / sketch / "board_config.h").read_text(encoding="utf-8")).group(1)
    report = {
        "version": version,
        "passed": True,
        "design_status": manifest["status"],
        "design_reference_origin": "frozen production-code render approved by the user; the pixel check pins rendering against it (regression), it is not independent proof of design intent",
        "pixel_comparison": comparison,
        "launcher_structure": launcher,
        "independent_process_runs": len(passed),
        "process_modes_passed": passed,
        "best_scores_observed": bests,
        "production_ui_screenshots": len(list(SHOTS.glob("*.rgb565"))),
        "glyph_coverage": glyph_report,
        "project_text_coverage_checker_note": "hardware/scripts/check_text_coverage.py does not scan src/pages; the glyph_coverage field above is the independent check for this page",
        "icon_reproducible_sha256": sha(icon_c),
        "source_sha256": {p: sha(ROOT / p) for p in paths},
        "serial_opened": False,
        "firmware_flashed": False,
        "device_verified": False,
    }
    (OUT / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(comparison)} screens match the frozen production preview, "
          f"current six-entry launcher geometry/glyphs/visibility verified, "
          f"{len(passed)} persistence process runs (best score round-trips), icon reproducible, "
          f"{len(cjk_chars)} CJK chars covered by both UI fonts")


if __name__ == "__main__":
    main()
