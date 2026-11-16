"""Verify timer/alarm changes with production LVGL and separate restart processes."""
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
OUT = ROOT / "build/timer-20261110"
EXE = ROOT / "build/host-lvgl-apps-preview/lvgl-focus-apps.exe"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--out", type=Path, help="Use a separate output directory for this verification run")
    args = parser.parse_args()
    global OUT
    if args.out:
        OUT = args.out.resolve()
    os.environ["PYTHONUTF8"] = "1"
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    OUT.mkdir(parents=True, exist_ok=True)
    if not args.skip_build:
        subprocess.run([sys.executable, "hardware/scripts/test_native_lvgl.py", "--focus",
                        "--obj", "build/host-lvgl-apps-preview", "--out", str(OUT / "ui")],
                       cwd=ROOT, check=True)
    groups = [
        ["timer-seed", "timer-restore", "timer-failed", "timer-restore", "timer-partial",
         "timer-restore", "timer-delete", "timer-deleted"],
        ["seed", "restore-fire", "restore-snooze", "deduplicate", "disabled",
         "failed-edit", "verify-old", "delete-alarm", "verify-deleted"],
        ["seed", "partial-edit", "verify-old"],
        ["legacy-seed", "legacy-read", "legacy-read"],
        ["corrupt-seed", "corrupt-read"],
        ["v2-seed", "v2-read", "v2-read"],
        ["app-volume-seed", "app-volume-restore", "app-volume-failed", "app-volume-restore",
         "app-volume-partial", "app-volume-restore"],
    ]
    passed = []
    for modes in groups:
        with tempfile.TemporaryDirectory(prefix="restart-", dir=OUT) as store:
            env = os.environ.copy()
            env.update({"GIFT_HOST_PREFS_DIR": store, "TZ": "UTC"})
            for mode in modes:
                result = subprocess.run([str(EXE), "--mode", mode], cwd=ROOT, env=env,
                                        capture_output=True, text=True, encoding="utf-8", timeout=30)
                print(result.stdout.strip())
                if result.returncode:
                    raise RuntimeError(mode + "\n" + result.stdout + result.stderr)
                passed.append(mode)
    font = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 18)
    pictures = [("alarm-01-list", "闹钟列表：直接删除"), ("timer-01-home", "倒计时首页：独立设置与常用项"),
                ("timer-02-duration", "独立时长滚轮"), ("timer-04-sounds", "复用三种预设提示音"),
                ("timer-05-running", "运行中：暂停或重置"), ("timer-06-paused", "暂停后从剩余时间继续"),
                ("timer-06-background-finished", "其他页面也能收到到时提醒"), ("timer-07-finished", "完成后再次计时")]
    preview = Image.new("RGB", (680, 1130), "#ECF0E5")
    draw = ImageDraw.Draw(preview)
    for i, (name, caption) in enumerate(pictures):
        x, y = 10 + i % 2 * 340, 10 + i // 2 * 280
        draw.text((x, y), caption, fill="#213F32", font=font)
        preview.paste(Image.open(OUT / "ui" / (name + ".png")), (x, y + 30))
    preview.save(OUT / "feature-preview.png")
    sketch = ROOT / "hardware/arduino/yiyi_desk"
    paths = [sketch / "src/pages" / name for name in
             ["timer_engine.h", "timer_storage.h", "timer_service.h", "timer_service.cpp",
              "timer_page.h", "timer_page.cpp", "focus_service.cpp", "focus_page.cpp"]]
    paths += [sketch / p for p in ["desk.cpp", "board_config.h", "src/gift_ui.cpp",
                                   "src/app_state.h", "src/nav_logic.h", "src/audio_board.cpp", "src/audio_board.h"]]
    report = {"passed": True, "separate_process_checks": len(passed), "process_modes": passed,
              "screenshots": len(list((OUT / "ui").glob("*.png"))),
              "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
              "firmware_flashed": False, "hardware_audio_tested": False}
    (OUT / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(passed)} separate restart processes; preview: {OUT / 'feature-preview.png'}")


if __name__ == "__main__":
    main()
