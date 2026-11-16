"""Pixel-diff two host render directories (LVGL offscreen frames).

Usage:
  python build/review/diff_renders.py A B [--json out.json]

Compares every *.rgb565 frame in A with the same-named frame in B and prints a
per-frame verdict. Frames are raw RGB565 (320*240*2 bytes), so equality is exact
and lossless -- no PNG re-encode involved.

Exit code 0 only when every frame is byte-identical and both dirs have the same
frame set (pages-contact.png ignored).
"""
from pathlib import Path
import argparse
import json
import sys


def diff_frames(a: bytes, b: bytes):
    if len(a) != len(b):
        return {"lengths": [len(a), len(b)], "diffPx": None}
    diff = 0
    first = None
    last = None
    minx, miny, maxx, maxy = 10 ** 9, 10 ** 9, -1, -1
    for i in range(0, len(a), 2):
        if a[i:i + 2] != b[i:i + 2]:
            diff += 1
            px = i // 2
            x, y = px % 320, px // 320
            if first is None:
                first = [x, y]
            last = [x, y]
            minx, maxx = min(minx, x), max(maxx, x)
            miny, maxy = min(miny, y), max(maxy, y)
    return {
        "diffPx": diff,
        "ratio": round(diff / (len(a) // 2), 6),
        "bbox": None if first is None else [minx, miny, maxx, maxy],
        "first": first,
        "last": last,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("a", type=Path)
    ap.add_argument("b", type=Path)
    ap.add_argument("--json", type=Path, default=None)
    args = ap.parse_args()
    frames_a = sorted(p for p in args.a.glob("*.rgb565"))
    frames_b = sorted(p for p in args.b.glob("*.rgb565"))
    names_a = {p.stem for p in frames_a}
    names_b = {p.stem for p in frames_b}
    report = {"a": str(args.a), "b": str(args.b), "frames": {}, "passed": True}
    missing = sorted(names_a ^ names_b)
    if missing:
        report["missing"] = missing
        report["passed"] = False
        print(f"FRAME SET MISMATCH: only in one side: {missing}")
    for name in sorted(names_a & names_b):
        fa, fb = args.a / f"{name}.rgb565", args.b / f"{name}.rgb565"
        res = diff_frames(fa.read_bytes(), fb.read_bytes())
        if res.get("diffPx") != 0:
            report["passed"] = False
        report["frames"][name] = res
        if res.get("diffPx") == 0:
            print(f"IDENTICAL  {name}")
        else:
            print(f"DIFF       {name}: {res['diffPx']} px "
                  f"({res.get('ratio')}), bbox={res.get('bbox')}, first={res.get('first')}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print("RESULT:", "PASS (all frames identical)" if report["passed"] else "FAIL (layout changed or frames missing)")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
