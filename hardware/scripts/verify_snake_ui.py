"""Verify production Snake pointer input, screenshots and persistent best score.

Uses the host-only LVGL runner. Does not open a serial port or write a device.
"""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--obj", default="build/host-lvgl")
    parser.add_argument("--out", default="build/snake-touch-20261112")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    output = ROOT / args.out
    shots = output / "ui"
    output.mkdir(parents=True, exist_ok=True)
    executable = ROOT / args.obj / "lvgl-snake.exe"
    if not args.skip_build:
        result = subprocess.run(
            [sys.executable, "hardware/scripts/test_native_lvgl.py", "--snake", "--obj", args.obj, "--out", str(shots)],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            encoding="utf-8", errors="replace", timeout=900,
        )
        (output / "native-tests.log").write_text(result.stdout, encoding="utf-8")
        print(result.stdout, end="")
        result.check_returncode()
    persistence = []
    for writer in ("best-play", "best-leave"):
        directory = Path(tempfile.mkdtemp(prefix=writer + "-", dir=output))
        env = os.environ.copy()
        env["GIFT_HOST_PREFS_DIR"] = str(directory)
        bests = []
        for mode in (writer, "best-restore"):
            result = subprocess.run(
                [str(executable), "--mode", mode], cwd=ROOT, env=env,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                encoding="utf-8", errors="replace", timeout=90,
            )
            (directory / (mode + ".log")).write_text(result.stdout, encoding="utf-8")
            print(result.stdout, end="")
            result.check_returncode()
            match = re.search(r"^BEST=(\d+)$", result.stdout, re.MULTILINE)
            assert match, (mode, result.stdout)
            bests.append(int(match[1]))
        assert bests[0] == bests[1] >= 10, (writer, bests)
        persistence.append({"writer": writer, "restorer": "best-restore", "best": bests[0],
                            "store": directory.relative_to(ROOT).as_posix()})

    # All Chinese literals on the page are checked against the two actual UI
    # fonts. Launcher Snake also explicitly selects the same 14px font.
    import compare_fonts as cf
    page = ROOT / "hardware/arduino/yiyi_desk/src/pages/snake_page.cpp"
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', page.read_text(encoding="utf-8"))
    chars = sorted({ch for literal in literals for ch in literal if ord(ch) > 127})
    glyphs = {}
    for font_name in ("fd_sans_bold_14_ui", "fd_sans_bold_16_ui"):
        font = cf.parse_font_file(ROOT / "hardware/arduino/yiyi_desk/src/gift_assets" / (font_name + ".c"))[font_name]
        missing = [ch for ch in chars if ord(ch) not in font.cp2gid]
        glyphs[font_name] = {"checked": len(chars), "missing": missing}
        assert not missing, (font_name, missing)
    from PIL import Image
    names = ("launcher-1", "launcher-2", "snake-ready", "snake-start", "snake-eat",
             "snake-play", "snake-paused", "snake-gameover", "snake-won")
    for name in names:
        assert Image.open(shots / (name + ".png")).size == (320, 240)
    report = {"passed": True, "physical_device_tested": False,
              "pointer_input": "Registered LVGL pointer driver; coordinate taps and board drags",
              "controls": "Swipe only; no direction buttons; stationary/sub12px jitter ignored",
              "board": {"x": 0, "y": 32, "width": 320, "height": 208, "columns": 20, "rows": 13},
              "screenshots": list(names), "persistence": persistence, "glyphs": glyphs}
    (output / "ui-verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print("PASS: Snake UI, nine production screenshots, glyph coverage and two cross-process persistence scenarios")


if __name__ == "__main__":
    main()
