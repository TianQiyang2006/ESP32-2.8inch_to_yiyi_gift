"""Per-callsite text coverage checker for the F-d font scheme.

F-d criterion: every string that actually reaches a label must be renderable by
the font that label uses **at that font's size**.  Missing glyphs fall back to
the full GB2312 ``fd_sans_16`` face, so a character that is only present in the
16 px fallback but missing from an 8/10/14/22 px face renders 16 px glyphs inside
a smaller line (the cross-size overlap bug seen on the device).

This script
    1. extracts every ``label(...)`` / ``centred(...)`` / ``text(...)`` /
       ``Ui::label(...)`` / ``Ui::button(...)`` / ``header(...)`` callsite (plus
       direct ``lv_label_set_text`` + ``lv_obj_set_style_text_font`` and the
       generated saved_design pages), keeping the literal text and the *actual*
       canonical font each site renders with;
    2. checks the literal characters against that font (own cmap + fallback
       chain) and classifies misses as ``same-size fallback`` (bold16/serif16 ->
       sans16, harmless) or ``cross-size fallback`` (would overlap);
    3. lists all callsites that receive dynamic text (network/user/schedule data)
       per canonical font, and estimates the extra bytes needed to make those
       fonts GB2312-full so the Lead can decide.

``literal_supplements()`` is imported by rebuild_fd_fonts.py: every literal that
appears at a callsite is added to that callsite font's charset, so hand-written
UI strings (desk.cpp / gift_ui.cpp / ui_widgets.cpp / screen_schedule.cpp /
gift_portal.h ...) are covered by construction.

Usage
    python hardware/scripts/check_text_coverage.py
    python hardware/scripts/check_text_coverage.py --json build/fd-fonts/callsites.json
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "hardware/arduino/yiyi_desk"
GA = FW / "src/gift_assets"
FD = FW / "src/saved_design/fonts"
MATRIX = ROOT / "build/fd-fonts/fd-matrix.json"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_fonts  # noqa: E402

# helper name -> (font arg index, text arg index, default font symbol)
HELPERS = {
    "label": (None, 1, None),          # default depends on the file, see below
    "centred": (None, 1, None),
    "text": (6, 1, None),              # screen_schedule.cpp signature
    "Ui::label": (None, 1, "desk_font"),
    "Ui::button": (None, 1, "desk_font"),
    "button": (None, 1, "desk_font"),
    "header": (None, 0, "desk_font"),
}
# per-file override of the unqualified helper's default font (local wrappers)
FILE_HELPER_DEFAULTS = {
    "src/gift_ui.cpp": {"label": "gift_font_small", "button": "gift_font_small",
                        "nav": "desk_font", "centred": None},
    "src/ui_widgets.cpp": {"label": "desk_font", "button": "desk_font"},
    "desk.cpp": {"label": "desk_font"},
    "src/saved_design/screen_schedule.cpp": {"text": None},  # explicit font arg
}
# dynamic text sources: labels with these expressions get network/user data
DYNAMIC_HINTS = (
    "GiftData", "readingValue", "readingText", "weather.", "XiaozhiVoice",
    "settings.", "course.", "value", "label", "title", "text", "String",
    "message", "toastText", "date", "target", "weekday", "countdownError",
    "condition", "number", "suffix", "unit", "name", "room", "author", "verse",
)
SAVED_PAGES = ["components", "weather", "holiday", "quotes", "daily", "letter", "apps"]


# ---------------------------------------------------------------------------
# source discovery / reading
# ---------------------------------------------------------------------------
def source_files() -> list[Path]:
    files = [FW / "desk.cpp", FW / "gift_portal.h",
             FW / "src/gift_ui.cpp", FW / "src/ui_widgets.cpp", FW / "src/ui_styles.cpp"]
    files += sorted((FW / "src").glob("*.cpp"))
    files += sorted((FW / "src").glob("*.h"))
    files.append(FW / "src/saved_design/screen_schedule.cpp")
    files += [FD / f"screen_{page}.c" for page in SAVED_PAGES]
    out = []
    seen = set()
    for p in files:
        if p.exists() and p.resolve() not in seen:
            seen.add(p.resolve())
            out.append(p)
    return out


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


# ---------------------------------------------------------------------------
# lightweight C/C++ call parser
# ---------------------------------------------------------------------------
def split_args(body: str) -> list[str]:
    args, depth, cur, in_str, esc = [], 0, [], False, False
    for ch in body:
        if in_str:
            cur.append(ch)
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
            cur.append(ch)
        elif ch in "([{":
            depth += 1
            cur.append(ch)
        elif ch in ")]}":
            depth -= 1
            cur.append(ch)
        elif ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        args.append("".join(cur).strip())
    return args


CALL_RE = re.compile(r"\b(Ui::)?(label|centred|text|button|header)\s*\(")
ASSIGN_RE = re.compile(r"(\w+)\s*=\s*$")
STYLE_FONT_RE = re.compile(r"lv_obj_set_style_text_font\s*\(\s*(\w+)\s*,\s*&?\s*(\w+)\s*,")
SET_TEXT_RE = re.compile(r"lv_label_set_text\s*\(\s*(\w+)\s*,\s*")
TEXT2_RE = re.compile(r"(?<![\w:>.])text\s*\(\s*(\w+)\s*,\s*")


def extract_strings(expr: str) -> tuple[list[str], bool]:
    """Return (Chinese-or-all literals, has non-literal dynamic expression)."""
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', expr)
    lits = [s.encode("utf-8").decode("unicode_escape").encode("latin-1", "ignore").decode("utf-8", "ignore")
            for s in lits]
    # strip the literals out and see whether something remains that can carry text
    rest = re.sub(r'"(?:[^"\\]|\\.)*"', "", expr).strip()
    dynamic = bool(rest) and not re.fullmatch(r"[\s(),.]*", rest)
    return lits, dynamic


def parse_file(path: Path) -> list[dict]:
    """Extract (pos, kind, args, assigned_var) events from one source file."""
    text = path.read_text(encoding="utf-8", errors="replace")
    events: list[dict] = []
    # helper calls
    for m in CALL_RE.finditer(text):
        depth, i, in_str, esc = 1, m.end(), False, False
        while i < len(text) and depth:
            ch = text[i]
            if in_str:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    in_str = False
            else:
                if ch == '"':
                    in_str = True
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
            i += 1
        name = (m.group(1) or "") + m.group(2)
        after = text[i:i + 4].lstrip()
        if after.startswith("{"):        # function definition, not a call
            continue
        line_start = text.rfind("\n", 0, m.start()) + 1
        prefix = text[line_start:m.start()]
        # skip declarations / definitions ("lv_obj_t *label(...)", "void text(...)")
        if prefix.strip() and not re.search(r"[=;{},()]", prefix):
            continue
        var_m = ASSIGN_RE.search(prefix)
        events.append({
            "pos": m.start(), "line": text.count("\n", 0, m.start()) + 1,
            "kind": "call", "helper": name, "args": split_args(text[m.end():i - 1]),
            "var": var_m.group(1) if var_m else None,
        })
    for rx, kind in ((STYLE_FONT_RE, "style_font"), (SET_TEXT_RE, "set_text"), (TEXT2_RE, "text2")):
        for m in rx.finditer(text):
            events.append({
                "pos": m.start(), "line": text.count("\n", 0, m.start()) + 1,
                "kind": kind, "obj": m.group(1),
                "font": m.group(2) if kind == "style_font" else None,
                "arg": text[m.end():m.end() + 400].split(");")[0],
            })
    events.sort(key=lambda e: e["pos"])
    return events


def saved_page_callsites() -> list[dict]:
    """Generated pages: label object -> (text, saved_gift_* font)."""
    sites = []
    for page in SAVED_PAGES:
        path = FD / f"screen_{page}.c"
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"lv_obj_t \* (\w+) = lv_label_create\(", text):
            name = m.group(1)
            tt = re.search(r"lv_label_set_text\(\s*" + name + r',\s*"((?:[^"\\]|\\.)*)"\)', text)
            ff = re.search(r"lv_obj_set_style_text_font\(\s*" + name + r"\s*,\s*&?saved_(\w+)\s*,", text)
            if not ff:
                continue
            font = "saved_" + ff.group(1)
            dynamic = not tt
            value = tt.group(1) if tt else None
            if value:
                value = value.encode("utf-8").decode("unicode_escape").encode("latin-1", "ignore").decode("utf-8", "ignore")
            # a later text(obj, var) / lv_label_set_text(obj, var) means dynamic content
            if re.search(r"(?:lv_label_set_text|text)\s*\(\s*" + name + r'\s*,\s*(?!")', text):
                dynamic = True
            sites.append({"file": rel(path), "line": text.count("\n", 0, m.start()) + 1,
                          "helper": "saved_page_label", "font": font,
                          "text": value, "dynamic": dynamic})
    return sites


# ---------------------------------------------------------------------------
# callsite extraction with real font resolution
# ---------------------------------------------------------------------------
def extract_callsites() -> list[dict]:
    sites: list[dict] = saved_page_callsites()
    for path in source_files():
        r = rel(path)
        if r.endswith(tuple(f"screen_{p}.c" for p in SAVED_PAGES)):
            continue
        events = parse_file(path)
        obj_font: dict[str, str] = {}
        local_defaults = FILE_HELPER_DEFAULTS.get(r.split("yiyi_desk/")[-1], {})
        for ev in events:
            if ev["kind"] == "style_font":
                obj_font[ev["obj"]] = ev["font"]
                continue
            if ev["kind"] == "set_text" or ev["kind"] == "text2":
                font = obj_font.get(ev["obj"])
                lits, dynamic = extract_strings(ev["arg"])
                if font and (lits or dynamic):
                    sites.append({"file": r, "line": ev["line"], "helper": "lv_label_set_text",
                                  "font": font, "text": "".join(lits) if lits else None,
                                  "dynamic": dynamic or not lits})
                continue
            helper = ev["helper"]
            args = ev["args"]
            font_idx, text_idx, default_font = HELPERS.get(helper, (None, None, None))
            font = None
            for a in args:
                m = re.search(r"&\s*((?:saved_gift|gift_font|desk_font|fd_|lv_font)\w*)", a)
                if m:
                    font = m.group(1)
                    break
            if font is None:
                font = local_defaults.get(helper, default_font)
            if font is None and ev["var"]:
                font = obj_font.get(ev["var"])
            if ev["var"] and font:
                obj_font[ev["var"]] = font
            if helper == "nav":
                text_idx, font = 0, font or "desk_font"
            if text_idx is None and helper == "text":
                continue
            text_expr = args[text_idx] if text_idx is not None and len(args) > text_idx else ""
            lits, dynamic = extract_strings(text_expr)
            sites.append({"file": r, "line": ev["line"], "helper": helper, "font": font,
                          "text": "".join(lits) if lits else None, "dynamic": dynamic or not lits})
    return sites


# ---------------------------------------------------------------------------
# canonical mapping / coverage
# ---------------------------------------------------------------------------
def aliases() -> dict[str, str]:
    try:
        from rebuild_fd_fonts import ALIASES  # noqa: E402  (lazy: rebuilt script is standalone too)
        return dict(ALIASES)
    except Exception:  # noqa: BLE001
        matrix = json.loads(MATRIX.read_text(encoding="utf-8"))
        return dict(matrix["aliases"])


def canonical_of(symbol: str | None, alias_map: dict[str, str]) -> str | None:
    if not symbol:
        return None
    if symbol.startswith("saved_") and symbol.endswith("_data"):
        stem = symbol[len("saved_"):-len("_data")]
        return alias_map.get(stem)
    if symbol.startswith("saved_"):
        return alias_map.get(symbol[len("saved_"):])
    return alias_map.get(symbol, symbol if symbol.startswith("fd_") else None)


def fallback_of(canon: str) -> str | None:
    try:
        from rebuild_fd_fonts import FALLBACK, DEFAULT_FALLBACK
        return FALLBACK.get(canon, DEFAULT_FALLBACK)
    except Exception:  # noqa: BLE001
        return None if canon == "fd_sans_16" else "fd_sans_16"


def load_fonts(canons: set[str]) -> dict[str, object]:
    fonts = {}
    for canon in canons:
        path = GA / (canon + ".c")
        if path.exists():
            try:
                fonts[canon] = compare_fonts.parse_font_file(path)[canon]
            except Exception:  # noqa: BLE001
                pass
    return fonts


def coverage(fonts: dict[str, object], canon: str) -> tuple[set[int], set[int]]:
    """(same-size coverage, cross-size fallback coverage)."""
    font = fonts.get(canon)
    if font is None:
        return set(), set()
    own = set(font.cp2gid)
    same = set(own)
    cross: set[int] = set()
    fb = fallback_of(canon)
    seen = set()
    while fb and fb not in seen:
        seen.add(fb)
        f2 = fonts.get(fb)
        if f2 is not None:
            # judge by generated glyph size: same pixel size == comparable line height
            if abs(font.line_height - f2.line_height) <= 1 and canon != "fd_sans_16":
                same |= set(f2.cp2gid)
            else:
                cross |= set(f2.cp2gid) - own
        fb = fallback_of(fb)
    return same, cross


def analyse() -> dict:
    alias_map = aliases()
    canons = {c for c in alias_map.values()}
    fonts = load_fonts(canons)
    cov = {c: coverage(fonts, c) for c in canons}
    sites = extract_callsites()
    missing: dict[str, dict] = {}
    cross_size: dict[str, dict] = {}
    dynamic: dict[str, list] = {}
    literal_chars: dict[str, set[int]] = {}
    unresolved = 0
    for site in sites:
        canon = canonical_of(site["font"], alias_map)
        if canon is None:
            unresolved += 1
            continue
        site["canonical"] = canon
        if site["text"]:
            chars = {ord(ch) for ch in site["text"] if ord(ch) >= 0x20}
            literal_chars.setdefault(canon, set()).update(chars)
            same, cross = cov.get(canon, (set(), set()))
            miss = chars - same
            if miss:
                key = canon
                missing.setdefault(key, {"file": site["file"], "line": site["line"],
                                         "helper": site["helper"], "text": site["text"],
                                         "chars": "".join(sorted(chr(c) for c in miss))})
                cross_chars = miss & cross
                if cross_chars:
                    cross_size[key] = {"file": site["file"], "line": site["line"],
                                       "text": site["text"],
                                       "chars": "".join(sorted(chr(c) for c in cross_chars))}
        else:
            dynamic.setdefault(canon, []).append(site)
    return {"sites": sites, "missing": missing, "cross_size": cross_size,
            "dynamic": dynamic, "literal_chars": literal_chars, "unresolved": unresolved,
            "fonts": fonts, "cov": cov}


def literal_supplements() -> dict[str, set[int]]:
    """Per-canonical character set that must be added so every literal callsite
    renders without any fallback at all."""
    try:
        from rebuild_fd_fonts import SPECS
        # only CJK-capable partial faces can meaningfully take extra CJK literals
        partial = {c for c, s in SPECS.items()
                   if s["charset"] == "union" and s["family"] in ("sans", "sans_b", "serif")}
    except Exception:  # noqa: BLE001
        partial = set()
    data = analyse()
    out: dict[str, set[int]] = {}
    file_canons: dict[str, set[str]] = {}
    for site in data["sites"]:
        if site.get("canonical") in partial:
            file_canons.setdefault(site["file"], set()).add(site["canonical"])
    for site in data["sites"]:
        if not site["text"]:
            continue
        chars = {ord(ch) for ch in site["text"]}
        if site.get("canonical") in partial:
            out.setdefault(site["canonical"], set()).update(chars)
        else:
            # font could not be resolved (object created by a helper deep in the
            # file): feed the literal to every partial CJK font used in that file
            for canon in file_canons.get(site["file"], {"fd_sans_14"}):
                out.setdefault(canon, set()).update(chars)
    return {canon: chars for canon, chars in out.items() if chars}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", default=str(ROOT / "build/fd-fonts/callsites.json"))
    args = ap.parse_args(argv)
    data = analyse()
    sites = data["sites"]
    print(f"callsites extracted: {len(sites)}  (unresolved font: {data['unresolved']})")
    by_font: dict[str, int] = {}
    for s in sites:
        by_font[s.get("canonical") or s["font"]] = by_font.get(s.get("canonical") or s["font"], 0) + 1
    print("callsites per canonical:", dict(sorted(by_font.items(), key=lambda kv: str(kv[0]))))
    unresolved_syms = {}
    for s in sites:
        if not s.get("canonical"):
            unresolved_syms[s.get("font")] = unresolved_syms.get(s.get("font"), 0) + 1
    print("unresolved font symbols:", dict(sorted(unresolved_syms.items(), key=lambda kv: str(kv[0]))))

    print(f"\nliteral callsites missing glyphs in own font: {len(data['missing'])}")
    for canon, m in sorted(data["missing"].items()):
        print(f"  {canon:16s} L{m['line']:<5} {m['file']}: missing {m['chars']!r}  text={m['text'][:24]!r}")
    print(f"\ncross-size fallback risks (16px glyph inside another size): {len(data['cross_size'])}")
    for canon, m in sorted(data["cross_size"].items()):
        print(f"  {canon:16s} missing {m['chars']!r}  text={m['text'][:24]!r}")

    print("\ndynamic-text callsites per canonical (need full charset decision):")
    for canon, lst in sorted(data["dynamic"].items()):
        sizes = {canon}
        font = data["fonts"].get(canon)
        print(f"  {canon:16s} {len(lst):3d} sites" + (f" (lh={font.line_height})" if font else ""))
        for s in lst[:4]:
            print(f"      {s['file']}:{s['line']} {s['helper']}")

    # estimate the cost of making the dynamic partial fonts GB2312-full
    matrix = {}
    try:
        matrix = {r["canonical"]: r for r in json.loads(MATRIX.read_text(encoding="utf-8"))["matrix"]}
    except Exception:  # noqa: BLE001
        pass
    full_est = {"fd_sans_8": 272_000, "fd_sans_10": 335_000, "fd_sans_14": 690_000,
                "fd_sans_22": 1_160_000, "fd_sans_bold_12": 660_000,
                "fd_sans_bold_16": 700_000, "fd_sans_bold_22": 1_170_000,
                "fd_serif_10": 340_000, "fd_serif_12": 720_000, "fd_serif_16": 800_000}
    print("\nfull-charset cost estimate for canonical fonts that see dynamic text:")
    for canon in sorted(data["dynamic"]):
        row = matrix.get(canon, {})
        cur = row.get("link_estimate", 0)
        full = full_est.get(canon)
        if not full:
            continue
        if row.get("charset") == "full":
            print(f"  {canon:16s} already GB2312-full ({cur:,} B)")
        else:
            print(f"  {canon:16s} partial {cur:8,d} B -> GB2312-full ~{full:,} B (+{full - cur:,} B)")

    payload = {
        "callsites": [{k: v for k, v in s.items() if k != "canonical"} | {"canonical": s.get("canonical")}
                      for s in sites],
        "missing": data["missing"],
        "cross_size": data["cross_size"],
        "dynamic": {c: [{k: v for k, v in s.items() if k != "canonical"} for s in lst]
                    for c, lst in data["dynamic"].items()},
    }
    Path(args.json).parent.mkdir(parents=True, exist_ok=True)
    Path(args.json).write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\nJSON: {Path(args.json).relative_to(ROOT)}")
    tests = ROOT / "build/fd-fonts/coverage-v2.txt"
    tests.write_text(
        f"callsites={len(sites)} missing={len(data['missing'])} cross_size={len(data['cross_size'])}\n",
        encoding="utf-8")
    return 0 if not data["missing"] and not data["cross_size"] else 1


if __name__ == "__main__":
    sys.exit(main())
