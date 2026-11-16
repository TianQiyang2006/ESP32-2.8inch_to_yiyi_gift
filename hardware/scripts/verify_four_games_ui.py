"""Verify four production game pages, pointer input, fonts and saved records.

Host-only: builds and runs the LVGL runners sequentially, never opens a port.
Each game saves a score in two isolated stores and restores it in new processes.
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
GAMES = ("breakout", "mines", "pin", "memory")


def run_logged(command, logfile, *, env=None, timeout=120):
    result = subprocess.run(command, cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace", timeout=timeout)
    logfile.write_text(result.stdout, encoding="utf-8")
    print(result.stdout, end="")
    result.check_returncode()
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true",
                        help="Verify existing screenshots and executables without rebuilding")
    parser.add_argument("--obj", default="build/host-lvgl")
    parser.add_argument("--out", default="build/four-games-20261113")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    output = ROOT / args.out
    output.mkdir(parents=True, exist_ok=True)
    import compare_fonts as cf
    from PIL import Image
    fonts = {name: cf.parse_font_file(ROOT / "hardware/arduino/yiyi_desk/src/gift_assets" / (name + ".c"))[name]
             for name in ("fd_sans_bold_14_ui", "fd_sans_bold_16_ui")}
    games, glyphs, persistence, screenshots, preview = {}, {}, [], [], []
    for game in GAMES:
        shots = output / ("ui-" + game)
        executable = ROOT / args.obj / ("lvgl-" + game + ".exe")
        if not args.skip_build:
            run_logged([sys.executable, "hardware/scripts/test_native_lvgl.py", "--" + game,
                        "--obj", args.obj, "--out", str(shots)],
                       output / (game + "-native.log"), timeout=1200)
        assert executable.is_file(), executable
        game_persistence = []
        for writer in ("best-pause", "best-leave"):
            directory = Path(tempfile.mkdtemp(prefix=game + "-" + writer + "-", dir=output))
            env = os.environ.copy()
            env["GIFT_HOST_PREFS_DIR"] = str(directory)
            bests = []
            for mode in (writer, "best-restore"):
                stdout = run_logged([str(executable), "--mode", mode], directory / (mode + ".log"), env=env)
                match = re.search(r"^BEST=(\d+)$", stdout, re.MULTILINE)
                assert match, (game, mode, stdout)
                bests.append(int(match[1]))
            assert bests[0] == bests[1] > 0, (game, writer, bests)
            record = {"game": game, "writer": writer, "restorer": "best-restore", "best": bests[0],
                      "store": os.path.relpath(directory, ROOT).replace(os.sep, "/")}
            persistence.append(record)
            game_persistence.append(record)
        source = ROOT / "hardware/arduino/yiyi_desk/src/pages" / (game + "_page.cpp")
        literals = re.findall(r'"((?:[^"\\]|\\.)*)"', source.read_text(encoding="utf-8"))
        chars = sorted({ch for literal in literals for ch in literal if ord(ch) > 127})
        assert chars, (game, "No non-ASCII UI text found")
        game_glyphs = {}
        for name, font in fonts.items():
            missing = [ch for ch in chars if ord(ch) not in font.cp2gid]
            coverage = {"checked": len(chars), "characters": "".join(chars), "missing": missing}
            game_glyphs[name] = coverage
            glyphs[game + "/" + name] = coverage
            assert not missing, (game, name, missing)
        names = sorted(path.stem for path in shots.glob(game + "-*.png"))
        assert len(names) >= 6, (game, names)
        launchers = sorted(path.stem for path in shots.glob("launcher-*.png"))
        assert (shots / "games-2.png").is_file(), (game, "Missing current Games list screenshot")
        names = launchers + ["games-2"] + names
        for name in names:
            with Image.open(shots / (name + ".png")) as image:
                assert image.size == (320, 240), (game, name, image.size)
        paths = [("ui-" + game + "/" + name) for name in names]
        screenshots.extend(paths)
        preview_name = next((game + suffix for suffix in ("-score", "-opening", "-start")
                             if (shots / (game + suffix + ".png")).is_file()), None)
        assert preview_name, (game, "Missing start/score preview")
        preview.append("ui-" + game + "/" + preview_name)
        games[game] = {"passed": True, "screenshots": paths, "persistence": game_persistence,
                       "glyphs": game_glyphs, "preview": preview[-1],
                       "pointer_input": "Registered LVGL pointer driver; real coordinate taps/drags; positive score earned from a new game"}
    assert len(persistence) == 8
    report = {"passed": True, "physical_device_tested": False,
              "games": games, "glyphs": glyphs, "persistence": persistence,
              "screenshots": screenshots, "preview": preview}
    (output / "ui-verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    review = Image.new("RGB", (640, 480))
    for index, name in enumerate(preview):
        with Image.open(output / (name + ".png")) as image:
            review.paste(image, ((index % 2) * 320, (index // 2) * 240))
    review.save(output / "four-games-review.png")
    print("PASS: Breakout, Mines, Pin and Memory production pointer input, 320x240 screenshots, "
          "14/16 UI glyph coverage and eight cross-process record checks")
    print("Four-game preview:", output / "four-games-review.png")


if __name__ == "__main__":
    main()
