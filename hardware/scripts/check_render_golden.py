"""Compare host page-render frames against a checked-in SHA-256 golden file.

Usage:
  python hardware/scripts/check_render_golden.py build/host-pages/verify
  python hardware/scripts/check_render_golden.py build/host-pages/verify --update

The golden file stores one SHA-256 per RGB565 frame (320*240*2 bytes) plus the
frame size. An intentional visual change (approved B-batch item) is recorded with
--update after the new screenshots have been reviewed.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GOLDEN = ROOT / "hardware/tests/golden/render-hashes.json"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames", type=Path)
    ap.add_argument("--golden", type=Path, default=DEFAULT_GOLDEN)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()

    frames = sorted(args.frames.glob("*.rgb565"))
    if not frames:
        print(f"FAIL: no .rgb565 frames under {args.frames}")
        return 1
    current = {
        p.stem: {"sha256": hashlib.sha256(p.read_bytes()).hexdigest(), "bytes": p.stat().st_size}
        for p in frames
    }
    if args.update or not args.golden.exists():
        args.golden.parent.mkdir(parents=True, exist_ok=True)
        args.golden.write_text(json.dumps({"frames": current}, indent=2, sort_keys=True), encoding="utf-8")
        print(f"golden updated: {args.golden} ({len(current)} frames)")
        return 0

    golden = json.loads(args.golden.read_text(encoding="utf-8"))["frames"]
    missing = sorted(set(golden) - set(current))
    extra = sorted(set(current) - set(golden))
    changed = sorted(name for name in set(golden) & set(current)
                     if golden[name]["sha256"] != current[name]["sha256"] or
                     golden[name]["bytes"] != current[name]["bytes"])
    if not missing and not extra and not changed:
        print(f"PASS: all {len(current)} frames match the golden baseline")
        return 0
    for name in changed:
        print(f"CHANGED  {name}")
    for name in missing:
        print(f"MISSING  {name}")
    for name in extra:
        print(f"NEW      {name}")
    print(f"FAIL: {len(changed)} changed / {len(missing)} missing / {len(extra)} new "
          f"(review the new frames, then re-run with --update if intentional)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
