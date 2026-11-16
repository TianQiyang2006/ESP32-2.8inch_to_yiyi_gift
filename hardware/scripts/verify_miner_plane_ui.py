"""Verify both production game pages, real pointer scoring and saved records.

Host-only: compiles and runs the two LVGL runners sequentially, never opens a port.
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
    parser.add_argument("--out", default="build/miner-plane-20261113")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    output = ROOT / args.out
    output.mkdir(parents=True, exist_ok=True)
    import compare_fonts as cf
    from PIL import Image
    fonts = {name: cf.parse_font_file(ROOT / "hardware/arduino/yiyi_desk/src/gift_assets" / (name + ".c"))[name]
             for name in ("fd_sans_bold_14_ui", "fd_sans_bold_16_ui")}
    games, glyphs, persistence, screenshots = {}, {}, [], []
    for game in ("miner", "plane"):
        shots = output / ("ui-" + game)
        executable = ROOT / args.obj / ("lvgl-" + game + ".exe")
        if not args.skip_build:
            result = subprocess.run([sys.executable, "hardware/scripts/test_native_lvgl.py", "--" + game,
                                     "--obj", args.obj, "--out", str(shots)], cwd=ROOT,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    encoding="utf-8", errors="replace", timeout=1200)
            (output / (game + "-native.log")).write_text(result.stdout, encoding="utf-8")
            print(result.stdout, end="")
            result.check_returncode()
        game_persistence = []
        for writer in ("best-pause", "best-leave"):
            directory = Path(tempfile.mkdtemp(prefix=game + "-" + writer + "-", dir=output))
            env = os.environ.copy()
            env["GIFT_HOST_PREFS_DIR"] = str(directory)
            bests = []
            for mode in (writer, "best-restore"):
                result = subprocess.run([str(executable), "--mode", mode], cwd=ROOT, env=env,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        encoding="utf-8", errors="replace", timeout=120)
                (directory / (mode + ".log")).write_text(result.stdout, encoding="utf-8")
                print(result.stdout, end="")
                result.check_returncode()
                match = re.search(r"^BEST=(\d+)$", result.stdout, re.MULTILINE)
                assert match, (game, mode, result.stdout)
                bests.append(int(match[1]))
            assert bests[0] == bests[1] > 0, (game, writer, bests)
            record = {"game": game, "writer": writer, "restorer": "best-restore", "best": bests[0],
                      "store": directory.relative_to(ROOT).as_posix()}
            persistence.append(record)
            game_persistence.append(record)
        source = ROOT / "hardware/arduino/yiyi_desk/src/pages" / (game + "_page.cpp")
        literals = re.findall(r'"((?:[^"\\]|\\.)*)"', source.read_text(encoding="utf-8"))
        chars = sorted({ch for literal in literals for ch in literal if ord(ch) > 127})
        game_glyphs = {}
        for name, font in fonts.items():
            missing = [ch for ch in chars if ord(ch) not in font.cp2gid]
            coverage = {"checked": len(chars), "missing": missing}
            game_glyphs[name] = coverage
            glyphs[game + "/" + name] = coverage
            assert not missing, (game, name, missing)
        names = sorted(path.stem for path in shots.glob(game + "-*.png"))
        assert len(names) >= 6, (game, names)
        for name in names:
            assert Image.open(shots / (name + ".png")).size == (320, 240)
        if (shots / "games-1.png").is_file():
            names.insert(0, "games-1")
        paths = [("ui-" + game + "/" + name) for name in names]
        screenshots.extend(paths)
        games[game] = {"passed": True, "screenshots": paths, "persistence": game_persistence,
                       "glyphs": game_glyphs,
                       "pointer_input": "Registered LVGL pointer driver; real coordinate taps/drags; positive score earned from a new game"}
    assert len(screenshots) >= 12
    report = {"passed": True, "physical_device_tested": False,
              "games": games, "glyphs": glyphs, "persistence": persistence, "screenshots": screenshots}
    (output / "ui-verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    review = Image.new("RGB", (640, 480))
    review_names = ("ui-miner/miner-start", "ui-miner/miner-score", "ui-plane/plane-start", "ui-plane/plane-score")
    for index, name in enumerate(review_names):
        review.paste(Image.open(output / (name + ".png")), ((index % 2) * 320, (index // 2) * 240))
    review.save(output / "miner-plane-review.png")
    print("PASS: Miner and Plane production pointer input, screenshots, glyph coverage and four cross-process record checks")


if __name__ == "__main__":
    main()
