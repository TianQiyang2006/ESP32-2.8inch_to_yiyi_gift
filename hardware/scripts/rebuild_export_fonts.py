"""Rebuild the 47 exported design fonts in place, without changing a single pixel.

The exported pages use 47 fonts under
``hardware/arduino/yiyi_desk/src/saved_design/fonts``.  Each .c file stores the
exact lv_font_conv command line that produced it in its ``Opts:`` header, so a
font can be regenerated deterministically from that line.

What this script changes
------------------------
1. Re-runs lv_font_conv with the recorded ``--font/--size/--bpp/--range/
   --symbols`` and **drops ``--no-compress``** so the 8 bpp glyph bitmaps use
   LVGL's RLE compression (LVGL 8.4 supports compressed 8 bpp fonts,
   src/font/lv_font_fmt_txt.c).  bpp is kept at 8: 8 -> 4 would change the
   antialiasing values and therefore the rendered pixels.
2. Any font whose RLE output is not smaller than the plain bitmap is kept
   uncompressed (decision is made per font, measured).
3. Fonts sharing the same (TTF, --size) are de-duplicated: one union font is
   generated and the second public ``lv_font_t`` symbol is appended to the same
   .c file pointing at the same static arrays.  The second .c file is reduced to
   a comment stub (the symbol still exists, defined in the merged file).
4. Every regenerated font is verified glyph-by-glyph against the pre-change
   snapshot with compare_fonts.py; a font is only overwritten when the
   comparison PASSes.  Failing fonts are left untouched and reported.

The pre-change .c snapshot is kept in ``build/font-baseline/`` and the mapping
used by compare_fonts.py is written to ``build/font-rebuild/manifest.json``.

Usage
-----
    python hardware/scripts/rebuild_export_fonts.py --dry-run
    python hardware/scripts/rebuild_export_fonts.py --only gift_yahei_b_16_1
    python hardware/scripts/rebuild_export_fonts.py            # all 47
    python hardware/scripts/compare_fonts.py                   # prove it
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONT_DIR = ROOT / "hardware/arduino/yiyi_desk/src/saved_design/fonts"
CONV = ROOT / "tools/font-tools/node_modules/lv_font_conv/lv_font_conv.js"
TTF_DIR = ROOT / "lvgl_ui_project/fonts"
STAGE = ROOT / "build/font-rebuild"
BASELINE = ROOT / "build/font-baseline"
MANIFEST = STAGE / "manifest.json"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_fonts  # noqa: E402  (local sibling module)

REL_FONT_DIR = FONT_DIR.relative_to(ROOT).as_posix()


# ---------------------------------------------------------------------------
# Opts parsing
# ---------------------------------------------------------------------------
@dataclass
class FontSpec:
    name: str                 # file stem, e.g. gift_yahei_b_15_96
    path: Path
    ttf: str                  # basename of the TTF in lvgl_ui_project/fonts
    size: int
    bpp: int
    ranges: list[str]
    symbols: str
    lv_name: str
    no_kerning: bool
    out: str
    old_opts: str


_OPT_RE = {
    "ttf": re.compile(r"--font\s+(\S+?)(?:\s+--|$)"),
    "size": re.compile(r"--size\s+(\d+)"),
    "bpp": re.compile(r"--bpp\s+(\d+)"),
    "ranges": re.compile(r"(?:--range|-r)\s+([0-9A-Fa-fx,\-]+)"),
    "symbols": re.compile(r"--symbols\s+(\S*?)(?:\s+--|$)"),
    "lv_name": re.compile(r"--lv-font-name\s+(\S+)"),
    "out": re.compile(r"-o\s+(\S+)"),
}


def parse_opts(path: Path) -> FontSpec:
    """Parse the ``Opts:`` line lv_font_conv embeds in the generated .c file."""
    text = path.read_text(encoding="utf-8")
    m = re.search(r"^\s*\*\s*Opts:\s*(.+)$", text, re.M)
    if not m:
        raise ValueError(f"{path.name}: no Opts line")
    opts = m.group(1).strip()
    tokens = opts.split()

    def flag_value(flag: str) -> str:
        for i, tok in enumerate(tokens):
            if tok == flag and i + 1 < len(tokens) and not tokens[i + 1].startswith("--"):
                return tokens[i + 1]
        return ""

    ttf_path = flag_value("--font")
    if not ttf_path:
        raise ValueError(f"{path.name}: no --font")
    ranges = [tokens[i + 1] for i, tok in enumerate(tokens)
              if tok in ("-r", "--range") and i + 1 < len(tokens)]
    return FontSpec(
        name=path.stem,
        path=path,
        ttf=Path(ttf_path).name,
        size=int(flag_value("--size")),
        bpp=int(flag_value("--bpp")),
        ranges=ranges,
        symbols=flag_value("--symbols"),
        lv_name=flag_value("--lv-font-name") or path.stem,
        no_kerning="--no-kerning" in tokens,
        out=flag_value("-o") or "",
        old_opts=opts,
    )


def expand_ranges(tokens: list[str]) -> set[int]:
    out: set[int] = set()
    for token in tokens:
        for part in token.split(","):
            part = part.strip()
            if not part:
                continue
            if "-" in part:
                lo, hi = part.split("-", 1)
                out.update(range(int(lo, 0), int(hi, 0) + 1))
            else:
                out.add(int(part, 0))
    return out


def ranges_to_str(cps: set[int]) -> str:
    """Compact "0x20-0x7E,0xA0" representation, uppercase like lv_font_conv."""
    out: list[str] = []
    for cp in sorted(cps):
        if out and out[-1][1] == cp - 1:
            start, _ = out[-1]
            out[-1] = (start, cp)
        else:
            out.append((cp, cp))
    return ",".join(f"0x{a:X}" if a == b else f"0x{a:X}-0x{b:X}" for a, b in out)


def quoted_symbols(chars: str) -> str:
    return "".join(sorted(set(chars)))


# ---------------------------------------------------------------------------
# lv_font_conv invocation / post-processing
# ---------------------------------------------------------------------------
def run_converter(spec: FontSpec, out_path: Path, compress: bool, *, ranges: str | None = None,
                  symbols: str | None = None, lv_name: str | None = None) -> list[str]:
    ttf = TTF_DIR / spec.ttf
    if not ttf.exists():
        raise FileNotFoundError(f"{spec.name}: TTF not found: {ttf}")
    cmd = [
        "node", "--max-old-space-size=3072", str(CONV),
        "--font", str(ttf), "--size", str(spec.size), "--bpp", str(spec.bpp),
        "--format", "lvgl", "--lv-font-name", lv_name or spec.lv_name,
    ]
    if spec.no_kerning:
        cmd.append("--no-kerning")
    if ranges is not None or spec.ranges:
        cmd += ["--range", ranges if ranges is not None else ",".join(spec.ranges)]
    sym = symbols if symbols is not None else spec.symbols
    if sym:
        cmd += ["--symbols", sym]
    if not compress:
        cmd.append("--no-compress")
    cmd += ["-o", str(out_path)]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return cmd


def postprocess(text: str, old_text: str, target: Path, out_path: Path, old_guard: str,
                compress: bool = True, merged_second: str | None = None,
                opts_override: str | None = None) -> str:
    """Apply the transformations export_saved_design.py used to apply."""
    text = text.replace("\r\n", "\n")
    # Keep the exact include prologue the exported file had.
    old_first = old_text.replace("\r\n", "\n").split("\n", 1)[0]
    if old_first.startswith("#include"):
        if not text.startswith(old_first):
            text = old_first + "\n" + text
    # Restore the original include guard name (cosmetic, keeps diffs readable).
    gm = re.search(r"#ifndef\s+(\w+)", text)
    if gm:
        text = text.replace(gm.group(1), old_guard)
    # Restore the exported fallback chain pointer.
    old_fallback = re.search(r"\.fallback\s*=\s*([^,\n]+)", old_text)
    if old_fallback:
        fb = old_fallback.group(1).strip()
        text = re.sub(r"\.fallback\s*=\s*[^,\n]+", f".fallback = {fb}", text)
    # The Opts header must be the one-line recipe of what actually produced this
    # file: baseline recipe, compression flag adjusted, output path in place.
    old_opts_m = re.search(r"^\s*\*\s*Opts:\s*(.+)$", old_text, re.M)
    if opts_override:
        new_opts = opts_override
    elif old_opts_m:
        new_opts = old_opts_m.group(1).strip()
        if compress:
            new_opts = re.sub(r"\s+--no-compress\b", "", new_opts)
        elif "--no-compress" not in new_opts:
            new_opts = re.sub(r"(--bpp\s+\d+)", r"\1 --no-compress", new_opts, count=1)
        new_opts = re.sub(r"(-o\s+)\S+", lambda mm: mm.group(1) + str(target), new_opts)
    else:
        new_opts = None
    if new_opts:
        text = re.sub(r"^(\s*\*\s*Opts:\s*).+$", lambda mm: mm.group(1) + new_opts, text, count=1, flags=re.M)
    if merged_second:
        struct = _font_struct_for(merged_second, text)
        text = text.replace("\n#endif /*#if", struct + "\n#endif /*#if", 1)
    return text.replace("\n", "\r\n")


def _field(text: str, name: str) -> str:
    m = re.search(r"\." + name + r"\s*=\s*([^,\n]+)", text)
    if not m:
        raise ValueError(f"missing .{name}")
    return m.group(1).strip()


def _font_struct_for(symbol: str, text: str) -> str:
    """Build a second lv_font_t pointing at the same static arrays (de-dup twins)."""
    fields = {k: _field(text, k) for k in (
        "line_height", "base_line", "subpx", "underline_position", "underline_thickness",
        "fallback", "user_data")}
    dsc = _field(text, "dsc")
    return (
        "\n/*---------------------------------------------------------------------------\n"
        f" * De-duplicated twin symbol: identical glyph arrays, no extra flash cost.\n"
        " *--------------------------------------------------------------------------*/\n"
        f"const lv_font_t {symbol} = {{\n"
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
        "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
        f"    .line_height = {fields['line_height']},\n"
        f"    .base_line = {fields['base_line']},\n"
        f"    .subpx = {fields['subpx']},\n"
        f"    .underline_position = {fields['underline_position']},\n"
        f"    .underline_thickness = {fields['underline_thickness']},\n"
        f"    .dsc = {dsc},\n"
        f"    .fallback = {fields['fallback']},\n"
        f"    .user_data = {fields['user_data']},\n"
        "};\n"
    )


def write_text(path: Path, text: str) -> None:
    path.write_bytes(text.encode("utf-8"))


def old_guard_of(old_text: str) -> str:
    m = re.search(r"#ifndef\s+(\w+)", old_text)
    return m.group(1) if m else "FONT"


# ---------------------------------------------------------------------------
# Baseline snapshot + manifest
# ---------------------------------------------------------------------------
def snapshot_baseline(path: Path) -> Path:
    """Keep one pristine copy of the pre-change .c (first run wins)."""
    rel = path.relative_to(ROOT)
    dest = BASELINE / rel
    if not dest.exists() and path.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, dest)
    return dest


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def _merged_opts(spec: FontSpec, ranges: str, symbols: str, compress: bool, target: Path) -> str:
    """One-line Opts recipe for a de-duplicated union font."""
    tokens = spec.old_opts.split()
    font_path = tokens[tokens.index("--font") + 1] if "--font" in tokens else str(TTF_DIR / spec.ttf)
    parts = ["--font", font_path, "--size", str(spec.size), "--bpp", str(spec.bpp)]
    if not compress:
        parts.append("--no-compress")
    if ranges:
        parts += ["--range", ranges]
    if symbols:
        parts += ["--symbols", symbols]
    parts += ["--lv-font-name", spec.lv_name, "--format", "lvgl", "-o", str(target)]
    return " ".join(parts)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", nargs="*", help="font stems to rebuild (default: all)")
    ap.add_argument("--dry-run", action="store_true", help="parse Opts and print the plan only")
    ap.add_argument("--no-dedup", action="store_true", help="skip the duplicate-font merge")
    ap.add_argument("--no-compress", action="store_true", help="keep --no-compress (debug)")
    ap.add_argument("--jobs", type=int, default=1, help="parallel conversions (default 1, deterministic output)")
    args = ap.parse_args(argv)

    STAGE.mkdir(parents=True, exist_ok=True)
    # Specs always come from the pristine baseline copy (the live files are the
    # output of this script; the baseline keeps the one true Opts recipe so the
    # script is idempotent and can be re-run).
    names = {p.stem for p in FONT_DIR.glob("*.c")}
    baseline_dir = BASELINE / REL_FONT_DIR
    if baseline_dir.exists():
        names |= {p.stem for p in baseline_dir.glob("*.c")}
    specs: list[FontSpec] = []
    parse_errors: list[str] = []
    for name in sorted(names):
        live = FONT_DIR / (name + ".c")
        baseline = BASELINE / REL_FONT_DIR / (name + ".c")
        if not baseline.exists():
            if not live.exists():
                continue
            snapshot_baseline(live)
        if not baseline.exists():
            continue
        try:
            spec = parse_opts(baseline)
            spec.path = live
            specs.append(spec)
        except Exception as exc:  # noqa: BLE001
            parse_errors.append(f"{name}.c: {exc}")
    if parse_errors:
        print("Opts parse errors:")
        for e in parse_errors:
            print("  " + e)
        return 2

    # Fonts that are not referenced by the final ELF are removed instead of rebuilt.
    dead = {"gift_yahei_b_15_96"}
    if args.only:
        specs = [s for s in specs if s.name in args.only]
        dead = {d for d in dead if d in args.only}

    # Group by (TTF, size, bpp): these are byte-identical conversions of the
    # same source if the codepoint sets are merged.
    groups: dict[tuple[str, int, int], list[FontSpec]] = {}
    for spec in specs:
        groups.setdefault((spec.ttf, spec.size, spec.bpp), []).append(spec)

    manifest_entries: list[dict] = []
    report_lines: list[str] = []
    failures: list[str] = []
    started = time.time()

    def emit(line: str) -> None:
        print(line, flush=True)
        report_lines.append(line)

    def verify(old_path: Path, new_path: Path, old_sym: str, new_sym: str) -> tuple[bool, list[str], dict]:
        old_fonts = compare_fonts.parse_font_file(old_path, old_sym)
        new_fonts = compare_fonts.parse_font_file(new_path, new_sym)
        return compare_fonts.compare_fonts(old_fonts[old_sym], new_fonts[new_sym], old_path.name)

    def rebuild_one(spec: FontSpec, *, ranges: str | None = None, symbols: str | None = None,
                    lv_name: str | None = None) -> tuple[Path, str, bool] | None:
        """Generate + verify one font. Returns (target, text, compressed) or None."""
        old_path = spec.path
        baseline = snapshot_baseline(old_path)
        old_text = baseline.read_text(encoding="utf-8")
        old_fonts = compare_fonts.parse_font_file(baseline)
        if len(old_fonts) != 1:
            raise ValueError(f"{spec.name}: baseline must define exactly one font")
        old_font = next(iter(old_fonts.values()))
        old_guard = old_guard_of(old_text)
        staging = STAGE / (spec.name + ".new.c")
        staging_final = STAGE / (spec.name + ".final.c")
        attempts = [False] if args.no_compress else [True, False]
        last_problems: list[str] = []
        compressed_choice: tuple[Path, str, bool] | None = None
        for compress in attempts:
            try:
                cmd = run_converter(spec, staging, compress, ranges=ranges, symbols=symbols, lv_name=lv_name)
            except subprocess.CalledProcessError as exc:
                failures.append(f"{spec.name}: converter failed: {exc.stderr.decode('utf-8', 'replace')[-400:]}")
                return None
            if args.dry_run:
                emit(f"[dry-run] {spec.name}: " + " ".join(cmd))
                return None
            # Recreate the exported-file transformations first: the fallback chain
            # and include prologue are part of what compare_fonts checks.
            text = postprocess(staging.read_text(encoding="utf-8"), old_text, old_path, staging, old_guard,
                               compress=compress, merged_second=None)
            staging_final.write_bytes(text.encode("utf-8"))
            try:
                new_fonts = compare_fonts.parse_font_file(staging_final)
            except Exception as exc:  # noqa: BLE001
                failures.append(f"{spec.name}: cannot parse generated file: {exc}")
                return None
            target_sym = lv_name or spec.lv_name
            if target_sym not in new_fonts and len(new_fonts) == 1:
                target_sym = next(iter(new_fonts))
            new_font = new_fonts[target_sym]
            ok, problems, stats = compare_fonts.compare_fonts(old_font, new_font, spec.name)
            if ok:
                saved = old_font.bitmap_bytes - new_font.bitmap_bytes
                if compress and saved <= 0:
                    # RLE would not help this font; keep the plain bitmap.
                    continue
                emit(f"{spec.name}: bitmap {old_font.bitmap_bytes:,} -> {new_font.bitmap_bytes:,} B "
                     f"({100.0 * new_font.bitmap_bytes / max(1, old_font.bitmap_bytes):.1f}%, "
                     f"{'RLE' if compress else 'PLAIN'}), compare PASS")
                return old_path, text, compress
            last_problems = problems
        failures.append(f"{spec.name}: pixel comparison FAILED (tried {'RLE then plain' if not args.no_compress else 'plain'}): "
                        + "; ".join(last_problems[:3]))
        return None

    # ---- 1. rebuild individual fonts (skip de-dup group members and dead ones)
    merge_members: dict[str, list[FontSpec]] = {}
    if not args.no_dedup and not args.only:
        for key, members in groups.items():
            alive = [m for m in members if m.name not in dead]
            if len(alive) > 1:
                for m in alive:
                    merge_members[m.name] = alive
    done: set[str] = set()
    for spec in specs:
        if spec.name in dead:
            continue
        if spec.name in merge_members:
            continue
        result = rebuild_one(spec)
        if result:
            target, text, compress = result
            write_text(target, text)
            manifest_entries.append({
                "label": spec.name,
                "baseline": rel(snapshot_baseline(spec.path)),
                "new": rel(target),
                "old_symbol": spec.lv_name,
                "new_symbol": spec.lv_name,
            })
            done.add(spec.name)

    # ---- 2. rebuild duplicate groups as one union font with two symbols
    handled_groups: set[tuple[str, ...]] = set()
    for spec in specs:
        members = merge_members.get(spec.name)
        if not members:
            continue
        key = tuple(m.name for m in members)
        if key in handled_groups:
            continue
        handled_groups.add(key)
        primary, secondary = members[0], members[1]
        if primary.name not in done and primary.name not in dead:
            pass  # primary is rebuilt through this merge path
        cp_union: set[int] = set()
        symbols_union = ""
        for m in members:
            cp_union |= expand_ranges(m.ranges)
            symbols_union += m.symbols
        syms = "".join(sorted(set(symbols_union) - {chr(cp) for cp in cp_union}))
        old_fonts = compare_fonts.parse_font_file(snapshot_baseline(primary.path))
        old1 = next(iter(old_fonts.values()))
        old_fonts2 = compare_fonts.parse_font_file(snapshot_baseline(secondary.path))
        old2 = next(iter(old_fonts2.values()))
        if (old1.line_height, old1.base_line, old1.fallback) != (old2.line_height, old2.base_line, old2.fallback):
            failures.append(f"merge {key}: struct fields differ, skipped")
            for m in members:
                result = rebuild_one(m)
                if result:
                    write_text(result[0], result[1])
            continue
        staging = STAGE / (primary.name + ".merged.c")
        ok = False
        old_text = snapshot_baseline(primary.path).read_text(encoding="utf-8")
        for compress in [False] if args.no_compress else [True, False]:
            merged_opts = _merged_opts(primary, ranges_to_str(cp_union), syms, compress, primary.path)
            cmd = run_converter(primary, staging, compress, ranges=ranges_to_str(cp_union),
                                symbols=syms, lv_name=primary.lv_name)
            if args.dry_run:
                emit(f"[dry-run] merge {key}: " + " ".join(cmd))
                break
            try:
                new_fonts = compare_fonts.parse_font_file(staging)
            except Exception as exc:  # noqa: BLE001
                failures.append(f"merge {key}: generated file unparsable: {exc}")
                break
            # Append the twin symbol with identical arrays, then verify both.
            text = staging.read_text(encoding="utf-8")
            text = postprocess(text, old_text, primary.path, staging, old_guard_of(old_text),
                               compress=compress, opts_override=merged_opts)
            twin_text = text.replace("\r\n", "\n")
            twin_struct = _font_struct_for(secondary.lv_name, twin_text)
            twin_text = twin_text.replace("\n#endif /*#if", twin_struct + "\n#endif /*#if", 1)
            staging.write_bytes(twin_text.encode("utf-8"))
            try:
                new_fonts = compare_fonts.parse_font_file(staging)
                ok1, p1, s1 = compare_fonts.compare_fonts(old1, new_fonts[primary.lv_name], primary.name)
                ok2, p2, s2 = compare_fonts.compare_fonts(old2, new_fonts[secondary.lv_name], secondary.name)
            except Exception as exc:  # noqa: BLE001
                ok1, ok2, p1, p2, s1, s2 = False, False, [str(exc)], [], {}, {}
            if ok1 and ok2:
                ok = True
                break
            failures.append(f"merge {key} {'RLE' if compress else 'PLAIN'}: verify failed: "
                            + "; ".join((p1 + p2)[:4]))
        if not ok:
            failures.append(f"merge {key}: all attempts failed, keeping individual fonts")
            for m in members:
                result = rebuild_one(m)
                if result:
                    write_text(result[0], result[1])
            continue
        text = staging.read_text(encoding="utf-8")
        write_text(primary.path, text)
        stub = (f"/* De-duplicated into {primary.path.name}: this file is intentionally empty.\n"
                f" * Both public symbols ({primary.lv_name}, {secondary.lv_name}) are defined\n"
                f" * in {primary.path.name} and share one set of static glyph arrays, so the\n"
                f" * two design fonts render pixel-identically for zero extra flash.\n"
                f" * Regenerate with hardware/scripts/rebuild_export_fonts.py. */\n")
        write_text(secondary.path, stub)
        emit(f"merge {key}: one union font ({len(cp_union):,} codepoints) -> {primary.path.name}, "
             f"{secondary.path.name} -> stub")
        for m in members:
            manifest_entries.append({
                "label": m.name,
                "baseline": rel(snapshot_baseline(m.path)),
                "new": rel(primary.path),
                "old_symbol": m.lv_name,
                "new_symbol": m.lv_name,
            })
        # ---- 3. dead fonts: remove source, they are not linked
    for spec in specs:
        if spec.name in dead:
            baseline = snapshot_baseline(spec.path)
            spec.path.unlink(missing_ok=True)
            emit(f"{spec.name}: dead font (absent from final ELF) -> deleted")
            manifest_entries.append({"label": spec.name, "baseline": rel(baseline), "status": "deleted"})

    # ---- CJK master entry (regenerated separately by generate_design_fonts.py)
    cjk = ROOT / "hardware/arduino/yiyi_desk/src/gift_assets/gift_font_cjk.c"
    if cjk.exists() and not args.only:
        manifest_entries.append({
            "label": "gift_font_cjk (CJK subset)",
            "baseline": rel(snapshot_baseline(cjk)),
            "new": rel(cjk),
            "old_symbol": "gift_font_cjk",
            "new_symbol": "gift_font_cjk",
            "mode": "subset",
        })

    if not args.dry_run:
        MANIFEST.write_text(json.dumps({"fonts": manifest_entries, "notes": report_lines},
                                       ensure_ascii=False, indent=1), encoding="utf-8")

    emit("")
    emit(f"done in {time.time() - started:.1f}s: {len(manifest_entries)} manifest entries, "
         f"{len(failures)} failures")
    for f in failures:
        emit("  FAIL " + f)
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
