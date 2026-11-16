"""F-d rebuild: every firmware font from OFL sources, converged size steps.

F-b (task-5) was "same pixels, compress harder".  F-d (this task) changes the
typeface itself: all fonts now come from redistributable OFL families

    CJK text ............ Noto Sans SC  (Regular / Bold)
    serif CJK / quotes .. Noto Serif SC
    Latin / digits ...... Montserrat (Regular / Medium / SemiBold / Bold)
    big clock digits .... Lilita One

and the number of distinct CJK sizes converges to 8/10/12/14/16/22 (Bold:
12/16/22, plus the design's special 26/39 display sizes).  Every old public
symbol (the 46 ``saved_gift_*_data`` + the ``gift_font_*`` / ``desk_font``
symbols) is kept as an alias ``const lv_font_t`` in a canonical font .c file:
aliases share the same static glyph arrays, so screen code, saved_resources.h
and gift_ui.cpp keep compiling unchanged with zero extra flash.

Runtime scaling is removed: gift_font_scale.h / saved_fallback.* go away and
every font's .fallback points at a real same-scheme font (Sans 16 full for
CJK coverage, Bold 16 for bold faces).

Charsets
    Sans 12 / Sans 16 : GB2312 6763 hanzi + ASCII + Chinese punctuation
                        (network/user dynamic text path, per task-11)
    Sans 22           : union of the design's 19/20/21px text + common dynamic
                        vocabulary (reading text is rendered by Sans 16 now;
                        anything missing falls back to Sans 16 -> no tofu)
    every other size  : codepoints of the old design fonts mapped onto it
                        + ASCII + Chinese punctuation + common dynamic
                        vocabulary, .fallback = Sans 16 full

Usage
    python hardware/scripts/rebuild_fd_fonts.py            # full rebuild
    python hardware/scripts/rebuild_fd_fonts.py --dry-run  # plan + charset stats
    python hardware/scripts/rebuild_fd_fonts.py --no-generate
                                                           # stubs/header/coverage only
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GA = ROOT / "hardware/arduino/yiyi_desk/src/gift_assets"
FD = ROOT / "hardware/arduino/yiyi_desk/src/saved_design/fonts"
DESK_FONT_C = ROOT / "hardware/arduino/yiyi_desk/desk_font.c"
BACKUP = ROOT / "build/fd-baseline"          # pre-F-d snapshot for width analysis
STAGE = ROOT / "build/fd-fonts"
OPEN = ROOT / "build/font-source/open"
LILITA = ROOT / "design/assets/fonts/LilitaOne-Regular.ttf"
CONV = ROOT / "tools/font-tools/node_modules/lv_font_conv/lv_font_conv.js"
GIFTS_H = GA / "gift_fonts.h"
REPORT_JSON = STAGE / "fd-matrix.json"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_fonts  # noqa: E402  (RLE-aware LVGL font .c parser)

REL_FD = FD.relative_to(ROOT).as_posix()
REL_GA = GA.relative_to(ROOT).as_posix()


# ---------------------------------------------------------------------------
# Character sets
# ---------------------------------------------------------------------------
def gb2312_hanzi() -> str:
    out = []
    for hi in range(0xB0, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                out.append(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return "".join(out)


ASCII = "".join(chr(cp) for cp in range(0x20, 0x7F))
# Chinese punctuation and symbols the design / UI strings use (horizontal forms
# only: the vertical kana repeat marks U+3031..U+303F and vertical brackets
# U+3014/3015 would blow up Noto's line height).
CJK_PUNCT = (
    "\u3000\u3001\u3002\uff0c\uff0e\uff01\uff1f\uff1a\uff1b\u201c\u201d\u2018\u2019"
    "\uff08\uff09\u3010\u3011\u300a\u300b\u3008\u3009\u300c\u300d\u300e\u300f"
    "\u2026\u2014\u2015\u00b7\u00b0\uff5e\u00a5\uff05\uff03\uff06\uff0a\uff0b"
    "\uff0d\uff1d\uff0f\uff1c\uff1e\uff20\uff3b\uff3d\uff3f\u2013\u2019\u00d7\u00f7"
)
# Small vocabulary of Chinese characters that appears in dynamic strings of the
# hand-written pages (dates, weekdays, weather, countdown, toasts).  Added to
# every CJK size so those labels do not depend on the fallback face.
UI_SYMBOLS = "\u2192\u21bb\u21ba\u2039\u203a\u00b7\u00d7\u00f7\u00b1\u00b0\u2103\u2030\u2116"
DYNAMIC_SAFE = (
    "年月日时分秒星期周一二三四五六七八九十今天明天后天昨天上下早中晚"
    "天气晴多云阴雨雪雾霾风温湿度空气质量预报最高最低实时更新等待联网校时"
    "未设置城市距离还有元旦过年放假上班课程表小智助手屏幕音量连手机配网"
    "喝了杯水敲击木鱼心情开心平静疲惫焦虑难过高冷普通温暖抱抱爱你"
)


def gb2312_hanzi_set() -> set[int]:
    return {ord(c) for c in gb2312_hanzi()}


def project_hanzi() -> str:
    """Hanzi used anywhere in project sources / data (static UI strings)."""
    roots = ["hardware", "data", "lvgl_ui_project", "design"]
    exts = {".c", ".cpp", ".h", ".hpp", ".ino", ".py", ".json", ".txt", ".md",
            ".xml", ".csv", ".yaml", ".yml", ".html", ".js", ".ts", ".psi", ".lua"}
    skip = {"build", "node_modules", "__pycache__", ".git", "fonts"}
    chars: set[str] = set()
    for name in roots:
        root = ROOT / name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in exts:
                continue
            if any(part in skip for part in path.parts):
                continue
            try:
                if path.stat().st_size > 8 * 1024 * 1024:
                    continue
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            chars.update(ch for ch in text if "\u4e00" <= ch <= "\u9fff")
    return "".join(sorted(chars))


# ---------------------------------------------------------------------------
# Canonical fonts (the F-d matrix)
# ---------------------------------------------------------------------------
# charset modes:
#   full   = GB2312 hanzi + ASCII + punctuation
#   union  = old fonts mapped onto this canonical + ASCII + punctuation + dynamic vocab
#   single = explicit symbol set (quote marks)
SPECS: dict[str, dict] = {
    # CJK text, Noto Sans SC Regular
    "fd_sans_8":   dict(family="sans", size=8, charset="full"),
    "fd_sans_10":  dict(family="sans", size=10, charset="full"),
    "fd_sans_12":  dict(family="sans", size=12, charset="full"),
    "fd_sans_14":  dict(family="sans", size=14, charset="union"),
    "fd_sans_16":  dict(family="sans", size=16, charset="full"),
    "fd_sans_22":  dict(family="sans", size=22, charset="union"),
    # CJK bold, Noto Sans SC Bold
    "fd_sans_bold_12": dict(family="sans_b", size=12, charset="union"),
    "fd_sans_bold_16": dict(family="sans_b", size=16, charset="union"),
    "fd_sans_bold_22": dict(family="sans_b", size=22, charset="union"),
    "fd_sans_bold_26": dict(family="sans_b", size=26, charset="union"),
    "fd_sans_bold_39": dict(family="sans_b", size=39, charset="union"),
    # serif, Noto Serif SC Regular
    "fd_serif_10":  dict(family="serif", size=10, charset="union"),
    "fd_serif_12":  dict(family="serif", size=12, charset="union"),
    "fd_serif_16":  dict(family="serif", size=16, charset="union"),
    "fd_serif_64":  dict(family="serif", size=64, charset="single", chars="\u201c"),
    "fd_serif_155": dict(family="serif", size=155, charset="single", chars="\u201c"),
    # Latin / digits, Montserrat
    "fd_mont_8":  dict(family="mont_r", size=8, charset="latin"),
    "fd_mont_9":  dict(family="mont_r", size=9, charset="latin"),
    "fd_mont_11": dict(family="mont_r", size=11, charset="latin"),
    "fd_mont_26": dict(family="mont_r", size=26, charset="latin"),
    "fd_mont_28": dict(family="mont_r", size=28, charset="latin"),
    "fd_mont_sb_21": dict(family="mont_sb", size=21, charset="latin"),
    "fd_mont_sb_34": dict(family="mont_sb", size=34, charset="latin"),
    "fd_mont_bold_9":  dict(family="mont_b", size=9, charset="latin"),
    "fd_mont_bold_13": dict(family="mont_b", size=13, charset="latin"),
    "fd_mont_bold_20": dict(family="mont_b", size=20, charset="latin"),
    "fd_mont_bold_57": dict(family="mont_b", size=57, charset="latin"),
    # display digits
    "fd_lilita_56": dict(family="lilita", size=56, charset="digits"),
    "fd_lilita_60": dict(family="lilita", size=60, charset="digits", tabular=True),
}

FAMILY_TTF = {
    "sans": OPEN / "NotoSansSC-Regular.ttf",
    "sans_b": OPEN / "NotoSansSC-Bold.ttf",
    "serif": OPEN / "NotoSerifSC-Regular.ttf",
    "mont_r": ROOT / "lvgl_ui_project/fonts/Montserrat-Regular.ttf",
    "mont_m": ROOT / "lvgl_ui_project/fonts/Montserrat-Medium.ttf",
    "mont_sb": ROOT / "lvgl_ui_project/fonts/Montserrat-SemiBold.ttf",
    "mont_b": ROOT / "lvgl_ui_project/fonts/Montserrat-Bold.ttf",
    "lilita": LILITA,
}

# Fallback chain: everything falls back to the full-coverage Sans 16.
FALLBACK = {"fd_sans_16": None, "fd_sans_bold_16": "fd_sans_16"}
DEFAULT_FALLBACK = "fd_sans_16"
# the old 7130-codepoint fallback master must not seed a canonical charset
EXCLUDE_FROM_CHARSET = {"gift_font_cjk"}
# literal characters found at real UI callsites (check_text_coverage.py);
# added to the partial fonts so hand-written strings never hit the fallback.
LITERAL_SUPPLEMENTS: dict[str, set[int]] = {}

# old public symbol -> canonical font (46 saved_gift_* + gift_assets symbols)
ALIASES: dict[str, str] = {
    # --- saved_design exported fonts: old (family,size) -> canonical ----------
    "gift_yahei_r_6_0": "fd_sans_8",
    "gift_yahei_r_7_0": "fd_sans_8",
    "gift_yahei_r_7_2": "fd_sans_8",
    "gift_yahei_r_8_0": "fd_sans_8",
    "gift_yahei_b_6_0": "fd_sans_8",
    "gift_yahei_b_8_0": "fd_sans_8",
    "gift_yahei_r_8_5": "fd_sans_10",
    "gift_yahei_r_9_0": "fd_sans_10",
    "gift_yahei_r_10_0": "fd_sans_10",
    "gift_yahei_r_10_2": "fd_sans_10",
    "gift_yahei_r_11_0": "fd_sans_12",
    "gift_yahei_r_11_2": "fd_sans_12",
    "gift_yahei_r_12_0": "fd_sans_12",
    "gift_yahei_r_15_4": "fd_sans_14",
    "gift_yahei_r_16_0": "fd_sans_16",
    "gift_yahei_r_17_0": "fd_sans_16",
    "gift_yahei_r_18_4": "fd_sans_16",
    "gift_yahei_r_19_0": "fd_sans_22",
    "gift_yahei_r_19_55": "fd_sans_22",
    "gift_yahei_r_21_0": "fd_sans_22",
    "gift_yahei_b_10_0": "fd_sans_bold_12",
    "gift_yahei_b_11_0": "fd_sans_bold_12",
    "gift_yahei_b_12_75": "fd_sans_bold_12",
    "gift_yahei_b_14_0": "fd_sans_bold_16",
    "gift_yahei_b_16_1": "fd_sans_bold_16",
    "gift_yahei_b_17_0": "fd_sans_bold_16",
    "gift_yahei_b_22_0": "fd_sans_bold_22",
    "gift_yahei_bi_25_9": "fd_sans_bold_26",
    "gift_yahei_b_39_0": "fd_sans_bold_39",
    "gift_kai_r_9_0": "fd_serif_10",
    "gift_kai_r_10_0": "fd_serif_10",
    "gift_kai_r_11_0": "fd_serif_12",
    "gift_kai_r_12_0": "fd_serif_12",
    "gift_kai_r_18_0": "fd_serif_16",
    "gift_segoe_r_6_0": "fd_sans_8",   # old font carried weekday hanzi
    "gift_segoe_r_8_0": "fd_mont_8",
    "gift_segoe_r_9_0": "fd_mont_9",
    "gift_segoe_ri_11_48": "fd_mont_11",
    "gift_segoe_r_26_4": "fd_mont_26",
    "gift_segoe_r_28_05": "fd_mont_28",
    "gift_segoe_b_8_5": "fd_mont_bold_9",
    "gift_segoe_b_10_0": "fd_sans_bold_12",  # old font carried 月/日
    "gift_segoe_b_13_0": "fd_mont_bold_13",
    "gift_segoe_b_20_0": "fd_mont_bold_20",
    "gift_segoe_black_57_0": "fd_mont_bold_57",
    "gift_georgia_b_21_0": "fd_mont_sb_21",
    # --- hand-written UI fonts ------------------------------------------------
    "gift_font_cjk": "fd_sans_22",
    "gift_font_small": "fd_sans_10",
    "gift_font_tiny": "fd_sans_8",
    "desk_font": "fd_sans_14",
    "gift_font_apps": "fd_sans_12",
    "gift_font_schedule_6": "fd_sans_8",
    "gift_font_schedule_8": "fd_sans_8",
    "gift_font_schedule_10": "fd_sans_10",
    "gift_font_schedule_12": "fd_sans_12",
    "gift_font_schedule_14": "fd_sans_14",
    "gift_font_clock": "fd_lilita_60",
    "gift_font_clock_date": "fd_sans_bold_16",
    "gift_font_flip": "fd_lilita_56",
    "gift_font_marks": "fd_serif_155",
    "gift_font_poem_marks": "fd_serif_64",
    "gift_font_poem": "fd_serif_16",
    "gift_font_quote": "fd_serif_16",
    "gift_font_reading_date": "fd_mont_sb_21",
    "gift_font_weather": "fd_mont_sb_34",
}

# old saved_design/fonts files are replaced by stubs; symbols live in canonical .c
OLD_SAVED_FILES = sorted({f"{stem}.c" for stem in ALIASES if stem.startswith("gift_") and not stem.startswith("gift_font_")})
OLD_GIFT_FILES = sorted(f"{stem}.c" for stem in ALIASES if stem.startswith("gift_font_")) + ["desk_font.c"]


# ---------------------------------------------------------------------------
# old font codepoint sets (the design's real character usage)
# ---------------------------------------------------------------------------
def old_font_codepoints() -> dict[str, set[int]]:
    sets: dict[str, set[int]] = {}
    baseline_dir = BACKUP / REL_FD
    for path in sorted(FD.glob("*.c")):
        snap = baseline_dir / path.name
        source = snap if snap.exists() else path
        try:
            fonts = compare_fonts.parse_font_file(source)
        except Exception:  # noqa: BLE001 - stub files are not fonts
            continue
        cps: set[int] = set()
        for font in fonts.values():
            cps |= set(font.cp2gid)
        sets[path.stem] = cps
    for path in sorted(GA.glob("gift_font_*.c")):
        try:
            fonts = compare_fonts.parse_font_file(path)
        except Exception:  # noqa: BLE001
            continue
        cps = set()
        for font in fonts.values():
            cps |= set(font.cp2gid)
        sets[path.stem] = cps
    return sets


def charset_for(canon: str, spec: dict, old_cps: dict[str, set[int]], project: str) -> set[int]:
    mode = spec["charset"]
    if mode == "full":
        return {ord(c) for c in gb2312_hanzi()} | {ord(c) for c in ASCII + CJK_PUNCT + UI_SYMBOLS}
    if mode == "digits":
        return {ord(c) for c in "0123456789:.-"}
    if mode == "single":
        return {ord(c) for c in spec["chars"]}
    # union of every old font that maps onto this canonical
    merged: set[int] = set()
    for old, target in ALIASES.items():
        if target == canon and old not in EXCLUDE_FROM_CHARSET:
            merged |= old_cps.get(old, set())
    if mode == "union":
        merged |= LITERAL_SUPPLEMENTS.get(canon, set())
        # CJK faces also carry the common punctuation + dynamic vocabulary so the
        # hand-written pages keep their date/weather/toast glyphs without fallback.
        merged |= {ord(c) for c in ASCII + CJK_PUNCT + DYNAMIC_SAFE + UI_SYMBOLS}
    else:  # latin faces: ASCII + whatever the old Latin design font carried
        merged |= {ord(c) for c in ASCII}
    # keep any codepoint the old design fonts carried (e.g. arrows), drop controls
    return {cp for cp in merged if cp >= 0x20 and cp != 0x7F}


def charset_stats(cps: set[int]) -> dict:
    return {
        "total": len(cps),
        "hanzi": sum(1 for cp in cps if 0x4E00 <= cp <= 0x9FFF),
        "ascii": sum(1 for cp in cps if 0x20 <= cp <= 0x7E),
        "other": sum(1 for cp in cps if not (0x4E00 <= cp <= 0x9FFF) and not (0x20 <= cp <= 0x7E)),
    }


# ---------------------------------------------------------------------------
# lv_font_conv
# ---------------------------------------------------------------------------
def make_tabular_clock_ttf() -> Path:
    """Equal digit advances (the live seconds must not move the clock)."""
    from fontTools.ttLib import TTFont

    target = STAGE / "LilitaOne-Tabular.ttf"
    clock = TTFont(LILITA)
    cmap = clock.getBestCmap()
    metrics = clock["hmtx"].metrics
    width = max(metrics[cmap[ord(d)]][0] for d in "0123456789")
    for digit in "0123456789":
        glyph = cmap[ord(digit)]
        advance, bearing = metrics[glyph]
        offset = (width - advance) // 2
        clock["glyf"][glyph].coordinates.translate((offset, 0))
        clock["glyf"][glyph].recalcBounds(clock["glyf"])
        metrics[glyph] = (width, bearing + offset)
    for record in clock["name"].names:
        if record.nameID in (1, 3, 4, 6):
            record.string = "Gift Clock".encode(record.getEncoding())
    clock.save(target)
    return target



# The quotes page uses U+21BB (\u21bb) which no OFL face ships; Noto's U+2192
# arrow is visually close, so the build clones it to U+21BB/U+21BA in a cached
# build-only TTF.  This keeps the shipped fonts 100% OFL content.
PATCH_GLYPHS = {0x21BB: 0x2192, 0x21BA: 0x2192}


def family_ttf(spec: dict) -> Path:
    base = FAMILY_TTF[spec["family"]]
    if base.name.startswith("Noto") and not spec.get("tabular"):
        from fontTools.ttLib import TTFont
        from copy import deepcopy

        patched = STAGE / (base.stem + "-symbols.ttf")
        if not patched.exists() or patched.stat().st_mtime < base.stat().st_mtime:
            font = TTFont(str(base))
            cmap = font.getBestCmap()
            hmtx = font["hmtx"].metrics
            order = list(font.getGlyphOrder())
            for target, source in PATCH_GLYPHS.items():
                source_name = cmap.get(source)
                if source_name is None or target in cmap:
                    continue
                new_name = "uni%04X" % target
                font["glyf"].glyphs[new_name] = deepcopy(font["glyf"].glyphs[source_name])
                hmtx[new_name] = hmtx[source_name]
                if new_name not in order:
                    order.append(new_name)
                for table in font["cmap"].tables:
                    if table.isUnicode():
                        table.cmap[target] = new_name
            font.setGlyphOrder(order)
            font.save(str(patched))
        return patched
    return base


def run_converter(canon: str, spec: dict, cps: set[int], out_path: Path) -> list[str]:
    ttf = family_ttf(spec)
    if not ttf.exists():
        raise FileNotFoundError(f"{canon}: missing OFL source {ttf}")
    if spec.get("tabular"):
        ttf = make_tabular_clock_ttf()
    symbols = "".join(chr(cp) for cp in sorted(cps))
    cmd = [
        "node", "--max-old-space-size=3072", str(CONV),
        "--font", str(ttf), "--size", str(spec["size"]), "--bpp", "4",
        "--format", "lvgl", "--lv-font-name", canon, "--no-kerning",
        f"--symbols={symbols}", "-o", str(out_path),
    ]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return cmd


def opts_line(canon: str, spec: dict, cps: set[int], target: Path) -> str:
    """One-line reproducible recipe written into the file header."""
    chars = "".join(chr(cp) for cp in sorted(cps))
    return (f"--font {FAMILY_TTF[spec['family']]} --size {spec['size']} --bpp 4 "
            f"--format lvgl --lv-font-name {canon} --no-kerning "
            f"--symbols {chars} -o {target}")


def struct_block(text: str, symbol: str) -> str:
    m = re.search(r"const lv_font_t\s+" + re.escape(symbol) + r"\s*=\s*\{.*?\n\};", text, re.S)
    if not m:
        raise ValueError(f"no lv_font_t {symbol} in generated file")
    return m.group(0)




def alias_struct(symbol: str, text: str) -> str:
    """A plain lv_font_t initializer that reuses the canonical glyph arrays."""
    def field(name: str, default: str = "NULL") -> str:
        m = re.search(r"\." + name + r"\s*=\s*([^,\n]+)", text)
        return m.group(1).strip() if m else default

    dsc = field("dsc", "&font_dsc")
    return (
        f"const lv_font_t {symbol} = {{\n"
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
        "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
        f"    .line_height = {field('line_height')},\n"
        f"    .base_line = {field('base_line')},\n"
        f"    .subpx = {field('subpx')},\n"
        f"    .underline_position = {field('underline_position')},\n"
        f"    .underline_thickness = {field('underline_thickness')},\n"
        f"    .dsc = {dsc},\n"
        f"    .fallback = {field('fallback')},\n"
        f"    .user_data = {field('user_data')},\n"
        "};\n"
    )

def postprocess_generated(text: str, canon: str, spec: dict, cps: set[int], target: Path,
                          aliases: list[str]) -> str:
    """Third-party shim -> firmware style: plain lvgl include, real fallback,
    reproducible Opts line, and one shared-array alias struct per old symbol."""
    text = text.replace("\r\n", "\n")
    text = re.sub(r'#ifdef LV_LVGL_H_INCLUDE_SIMPLE\s*#include "lvgl.h"\s*#else\s*#include "lvgl/lvgl.h"\s*#endif',
                  '#include "lvgl.h"', text)
    fallback = FALLBACK.get(canon, DEFAULT_FALLBACK)
    fb = "NULL" if fallback is None else f"&{fallback}"
    text = re.sub(r"\.fallback\s*=\s*[^,\n]+", f".fallback = {fb}", text)
    if fallback:
        text = text.replace('#include "lvgl.h"', f'#include "lvgl.h"\nextern const lv_font_t {fallback};')
    opts = opts_line(canon, spec, cps, target)
    text = re.sub(r"^(\s*\*\s*Opts:\s*).+$", lambda m: m.group(1) + opts, text, count=1, flags=re.M)
    if aliases:
        blocks = ["\n/* F-d alias: same glyph arrays, zero extra flash. */\n\n"
                  + alias_struct(alias, text) for alias in aliases]
        end = text.rfind("\n#endif /*#if")
        text = text[:end] + "".join(blocks) + text[end:]
    return text.replace("\n", "\r\n")


# ---------------------------------------------------------------------------
# Coverage API (same public interface as task-5, union of all new fonts)
# ---------------------------------------------------------------------------
def write_coverage(codepoints: set[int]) -> None:
    cjk = sorted(cp for cp in codepoints if 0x4E00 <= cp <= 0x9FFF)
    bitmap = bytearray((0x9FFF - 0x4E00 + 1 + 7) // 8)
    for cp in cjk:
        index = cp - 0x4E00
        bitmap[index >> 3] |= 1 << (index & 7)
    others = sorted(cp for cp in codepoints if not 0x4E00 <= cp <= 0x9FFF)
    ranges: list[tuple[int, int]] = []
    for cp in others:
        if ranges and ranges[-1][1] == cp - 1:
            ranges[-1] = (ranges[-1][0], cp)
        else:
            ranges.append((cp, cp))
    header = '''#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime glyph-coverage check for every font shipped by the F-d scheme.
 * Generated by hardware/scripts/rebuild_fd_fonts.py - do not edit by hand.
 * The union is the CJK bitmap of all Noto Sans/Serif faces plus the range
 * table of Latin/punctuation codepoints of the Montserrat/Lilita faces.
 */
bool gift_coverage_has(uint32_t codepoint);
bool gift_coverage_text_supported(const char *utf8);

#ifdef __cplusplus
}

/* C++ convenience wrappers (fw-eng calls GiftCoverage::has / textSupported). */
namespace GiftCoverage {
inline bool has(uint32_t codepoint) { return gift_coverage_has(codepoint); }
inline bool textSupported(const char *utf8) { return gift_coverage_text_supported(utf8); }
}
#endif
'''
    bitmap_lines = []
    for i in range(0, len(bitmap), 16):
        bitmap_lines.append("    " + ", ".join("0x%02X" % b for b in bitmap[i:i + 16]) + ",")
    range_init = ",\n    ".join("{0x%04X, 0x%04X}" % (lo, hi) for lo, hi in ranges)
    source = f'''#include "gift_glyph_coverage.h"
#include <stddef.h>

/* Union of every F-d font cmap. CJK block U+4E00-U+9FFF: {len(cjk):,} codepoints
 * in a 1-bit bitmap. Everything else: {len(others):,} codepoints in {len(ranges)} ranges.
 */

static const uint8_t cjk_bitmap[] = {{
{chr(10).join(bitmap_lines)}
}};

typedef struct {{ uint32_t first; uint32_t last; }} coverage_range;

static const coverage_range other_ranges[] = {{
    {range_init}
}};

#define CJK_FIRST 0x4E00u
#define CJK_LAST  0x9FFFu

bool gift_coverage_has(uint32_t codepoint)
{{
    if(codepoint >= CJK_FIRST && codepoint <= CJK_LAST) {{
        uint32_t index = codepoint - CJK_FIRST;
        return (cjk_bitmap[index >> 3] >> (index & 7)) & 1;
    }}
    uint32_t low = 0, high = (uint32_t)(sizeof(other_ranges) / sizeof(other_ranges[0]));
    while(low < high) {{
        uint32_t mid = (low + high) / 2;
        if(codepoint < other_ranges[mid].first) high = mid;
        else if(codepoint > other_ranges[mid].last) low = mid + 1;
        else return true;
    }}
    return false;
}}

bool gift_coverage_text_supported(const char *utf8)
{{
    if(utf8 == NULL) return false;
    const uint8_t *p = (const uint8_t *)utf8;
    while(*p) {{
        uint32_t cp;
        uint8_t c = *p++;
        if(c < 0x80) cp = c;
        else if((c & 0xE0) == 0xC0) {{
            if((*p & 0xC0) != 0x80) return false;
            cp = ((uint32_t)(c & 0x1F) << 6) | (*p++ & 0x3F);
        }}
        else if((c & 0xF0) == 0xE0) {{
            if((p[0] & 0xC0) != 0x80 || (p[1] & 0xC0) != 0x80) return false;
            cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        }}
        else if((c & 0xF8) == 0xF0) {{
            if((p[0] & 0xC0) != 0x80 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
            cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[0] & 0x3F) << 12)
               | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        }}
        else return false;
        if(!gift_coverage_has(cp)) return false;
    }}
    return true;
}}
'''
    (GA / "gift_glyph_coverage.h").write_text(header, encoding="utf-8")
    (GA / "gift_glyph_coverage.c").write_text(source, encoding="utf-8")
    print(f"gift_glyph_coverage: {len(cjk):,} CJK + {len(others):,} other codepoints "
          f"in {len(ranges)} ranges, bitmap {len(bitmap):,} B", flush=True)


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true", help="plan + charset stats only")
    parser.add_argument("--only", nargs="*", help="canonical fonts to regenerate")
    parser.add_argument("--no-generate", action="store_true", help="only stubs/header/coverage/manifest")
    args = parser.parse_args(argv)

    STAGE.mkdir(parents=True, exist_ok=True)
    started = time.time()
    project = project_hanzi()
    old_cps = old_font_codepoints()
    print(f"old font cmaps: {len(old_cps)} fonts; project hanzi: {len(project)}", flush=True)
    try:
        import check_text_coverage
        LITERAL_SUPPLEMENTS.update(check_text_coverage.literal_supplements())
        total_lit = sum(len(v) for v in LITERAL_SUPPLEMENTS.values())
        print(f"call-site literal supplements: {total_lit:,} chars over "
              f"{len(LITERAL_SUPPLEMENTS)} partial fonts", flush=True)
        for canon in sorted(LITERAL_SUPPLEMENTS):
            print(f"  {canon:16s} +{len(LITERAL_SUPPLEMENTS[canon]):4d} literal chars", flush=True)
    except Exception as exc:  # noqa: BLE001 - never block font generation on the checker
        print(f"WARN literal supplement analysis failed: {exc}", flush=True)

    # snapshot old font files for the width/overflow analysis (first run wins)
    for path in sorted(FD.glob("*.c")):
        dest = BACKUP / REL_FD / path.name
        if not dest.exists():
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(path.read_bytes())
    for path in sorted(GA.glob("gift_font_*.c")):
        dest = BACKUP / REL_GA / path.name
        if not dest.exists():
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(path.read_bytes())

    # which aliases belong in which canonical file (symbols, not file stems)
    alias_symbols: dict[str, list[str]] = {canon: [] for canon in SPECS}
    for old, canon in ALIASES.items():
        if old.startswith("gift_") and not old.startswith("gift_font_"):
            symbol = f"saved_{old}_data"          # saved_gift_*_data
        else:
            symbol = old                          # gift_font_* / desk_font
        alias_symbols[canon].append(symbol)

    matrix: list[dict] = []
    total_linked = 0
    for canon, spec in SPECS.items():
        cps = charset_for(canon, spec, old_cps, project)
        stats = charset_stats(cps)
        if args.dry_run:
            print(f"{canon:18s} {spec['family']:8s} size={spec['size']:3d} cps={len(cps):5d} "
                  f"(hanzi {stats['hanzi']}, ascii {stats['ascii']}, other {stats['other']})", flush=True)
            continue
        target = GA / (canon + ".c")
        if args.no_generate or (args.only and canon not in args.only):
            if target.exists():
                fonts = list(compare_fonts.parse_font_file(target).values())
                linked = fonts[0].linked_bytes() + 28 * (len(fonts) - 1)
            else:
                linked = 0
        else:
            staging = STAGE / (canon + ".raw.c")
            run_converter(canon, spec, cps, staging)
            text = postprocess_generated(staging.read_text(encoding="utf-8"), canon, spec, cps, target,
                                         alias_symbols[canon])
            target.write_bytes(text.encode("utf-8"))
            fonts = compare_fonts.parse_font_file(target)
            linked = fonts[canon].linked_bytes() + 28 * (len(fonts) - 1)
            font = fonts[canon]
            print(f"{canon:18s} size={spec['size']:3d} cps={len(font.cp2gid):5d} "
                  f"bitmap={font.bitmap_bytes:8,d} linked~{linked:8,d} "
                  f"lh={font.line_height} bl={font.base_line} aliases={len(alias_symbols[canon])}", flush=True)
        total_linked += linked
        matrix.append({
            "canonical": canon, "family": spec["family"], "size": spec["size"],
            "charset": spec["charset"], "codepoints": len(cps), "stats": stats,
            "link_estimate": linked,
            "aliases": alias_symbols[canon],
        })

    if args.dry_run:
        return 0

    # ---- stubs: old .c files keep their filename, symbols now live elsewhere
    for name in OLD_SAVED_FILES:
        path = FD / name
        stem = path.stem
        canon = ALIASES.get(stem)
        path.write_text(
            f"/* F-d: symbol saved_{stem}_data is an alias sharing the glyph arrays of\n"
            f" * src/gift_assets/{canon}.c (OFL font) - this file is intentionally empty.\n"
            f" * Same pixels for this symbol, zero extra flash.\n"
            f" * Regenerate with hardware/scripts/rebuild_fd_fonts.py. */\n", encoding="utf-8")
    for name in OLD_GIFT_FILES:
        path = GA / name
        if path == GIFTS_H:
            continue
        if name == "desk_font.c":
            DESK_FONT_C.write_text(
                "/* F-d: desk_font is an alias of fd_sans_14 defined in\n"
                " * src/gift_assets/fd_sans_14.c - this file is intentionally empty.\n"
                " * Regenerate with hardware/scripts/rebuild_fd_fonts.py. */\n", encoding="utf-8")
            continue
        stem = path.stem
        canon = ALIASES.get(stem)
        path.write_text(
            f"/* F-d: symbol {stem} is an alias sharing the glyph arrays of\n"
            f" * src/gift_assets/{canon}.c (OFL font) - this file is intentionally empty.\n"
            f" * Regenerate with hardware/scripts/rebuild_fd_fonts.py. */\n", encoding="utf-8")

    # removed runtime-scaling glue
    for path in (GA / "gift_font_scale.h", ROOT / "hardware/arduino/yiyi_desk/src/saved_design/saved_fallback.h",
                 ROOT / "hardware/arduino/yiyi_desk/src/saved_design/saved_fallback.c"):
        if path.exists():
            path.unlink()
            print(f"removed {path.relative_to(ROOT)}", flush=True)

    # ---- public header: canonical fonts + every legacy symbol
    declarations = [f"extern const lv_font_t {canon};" for canon in SPECS]
    legacy = sorted({symbol for symbols in alias_symbols.values() for symbol in symbols})
    declarations += [f"extern const lv_font_t {s};" for s in legacy]
    GIFTS_H.write_text(
        '#pragma once\n#include <lvgl.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n'
        + "\n".join(declarations) + '\n#ifdef __cplusplus\n}\n#endif\n', encoding="utf-8")

    # ---- coverage: union of all generated fonts
    union: set[int] = set()
    for canon in SPECS:
        path = GA / (canon + ".c")
        if not path.exists():
            continue
        union |= set(compare_fonts.parse_font_file(path)[canon].cp2gid)
    write_coverage(union)

    REPORT_JSON.write_text(json.dumps({
        "matrix": matrix,
        "aliases": ALIASES,
        "total_link_estimate": total_linked,
        "charset": {"full": len(gb2312_hanzi_set()), "punct": len(CJK_PUNCT),
                    "dynamic_safe": len(DYNAMIC_SAFE), "project_hanzi": len(project)},
    }, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\ntotal fonts linked estimate: {total_linked:,} B "
          f"({total_linked / 1e6:.2f} MB target <= 1.9 MB) in {time.time() - started:.1f}s", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
