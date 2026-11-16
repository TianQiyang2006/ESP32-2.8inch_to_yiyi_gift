"""Prove that regenerated LVGL fmt_txt fonts render pixel-identically.

Parses two generations of a font .c file (plain or RLE-compressed), rebuilds the
equivalent of LVGL's lv_font_fmt_txt.c decode path (get_glyph_dsc_id + bitmap
decompression), and compares:

  * bpp / line_height / base_line / fallback / subpx / underline fields
  * for every codepoint present in the baseline: box_w/box_h/ofs_x/ofs_y/adv_w
    and every decoded alpha value of the glyph bitmap
  * kerning pairs (compared by codepoint pair, so glyph-id reordering from a
    merged / de-duplicated font does not matter)

The RLE decoder is a faithful port of LVGL 8.4.0
(src/font/lv_font_fmt_txt.c: decompress(), rle_init(), rle_next(), get_bits()),
including the optional vertical XOR prefilter used by
LV_FONT_FMT_TXT_COMPRESSED (bitmap_format == 1).

Usage
-----
    python hardware/scripts/compare_fonts.py
    python hardware/scripts/compare_fonts.py --baseline build/font-baseline \
        --manifest build/font-rebuild/manifest.json --verbose
    python hardware/scripts/compare_fonts.py --old a.c --new b.c \
        --old-symbol sym_a --new-symbol sym_b

Exit code 0 iff every comparison PASSes.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# lv_font_fmt_txt_bitmap_format_t
PLAIN, COMPRESSED, COMPRESSED_NO_PREFILTER = 0, 1, 2
FORMAT0_FULL = "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL"
SPARSE_FULL = "LV_FONT_FMT_TXT_CMAP_SPARSE_FULL"
FORMAT0_TINY = "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY"
SPARSE_TINY = "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY"


# ---------------------------------------------------------------------------
# LVGL RLE decoder port
# ---------------------------------------------------------------------------
class RleReader:
    """Port of rle_init()/rle_next() from lv_font_fmt_txt.c (LVGL 8.4.0)."""

    SINGLE, REPEATE, COUNTER = 0, 1, 2

    def __init__(self, data: bytes, bpp: int):
        self.data = data
        self.bpp = bpp
        self.state = self.SINGLE
        self.rdp = 0
        self.prev_v = 0
        self.cnt = 0

    def get_bits(self, bit_pos: int, length: int) -> int:
        bit_mask = (1 << length) - 1
        byte_pos = bit_pos >> 3
        bit_pos &= 0x7
        if bit_pos + length >= 8:
            hi = self.data[byte_pos] if byte_pos < len(self.data) else 0
            lo = self.data[byte_pos + 1] if byte_pos + 1 < len(self.data) else 0
            return ((hi << 8) + lo) >> (16 - bit_pos - length) & bit_mask
        return (self.data[byte_pos] >> (8 - bit_pos - length)) & bit_mask

    def next(self) -> int:
        ret = 0
        if self.state == self.SINGLE:
            ret = self.get_bits(self.rdp, self.bpp)
            if self.rdp != 0 and self.prev_v == ret:
                self.cnt = 0
                self.state = self.REPEATE
            self.prev_v = ret
            self.rdp += self.bpp
        elif self.state == self.REPEATE:
            v = self.get_bits(self.rdp, 1)
            self.cnt += 1
            self.rdp += 1
            if v == 1:
                ret = self.prev_v
                if self.cnt == 11:
                    self.cnt = self.get_bits(self.rdp, 6)
                    self.rdp += 6
                    if self.cnt != 0:
                        self.state = self.COUNTER
                    else:
                        ret = self.get_bits(self.rdp, self.bpp)
                        self.prev_v = ret
                        self.rdp += self.bpp
                        self.state = self.SINGLE
            else:
                ret = self.get_bits(self.rdp, self.bpp)
                self.prev_v = ret
                self.rdp += self.bpp
                self.state = self.SINGLE
        else:  # COUNTER
            ret = self.prev_v
            self.cnt -= 1
            if self.cnt == 0:
                ret = self.get_bits(self.rdp, self.bpp)
                self.prev_v = ret
                self.rdp += self.bpp
                self.state = self.SINGLE
        return ret


def _get_bits(data: bytes, bit_pos: int, length: int) -> int:
    mask = (1 << length) - 1
    byte_pos = bit_pos >> 3
    bit_pos &= 0x7
    if bit_pos + length >= 8:
        hi = data[byte_pos] if byte_pos < len(data) else 0
        lo = data[byte_pos + 1] if byte_pos + 1 < len(data) else 0
        return ((hi << 8) + lo) >> (16 - bit_pos - length) & mask
    return (data[byte_pos] >> (8 - bit_pos - length)) & mask


def _bits_write(out: bytearray, bit_pos: int, val: int, length: int) -> None:
    if length == 3:
        length = 4
        val = (0, 2, 4, 6, 9, 11, 13, 15)[val]
    byte_pos = bit_pos >> 3
    bit_pos &= 0x7
    bit_pos = 8 - bit_pos - length
    mask = (1 << length) - 1
    out[byte_pos] &= (~mask) << bit_pos & 0xFF
    out[byte_pos] |= val << bit_pos


def decompress(data: bytes, w: int, h: int, bpp: int, prefilter: bool) -> bytes:
    """Port of decompress(): returns the continuous per-pixel packed stream."""
    wr_size = 4 if bpp == 3 else bpp
    pixel_count = w * h
    out_len = (pixel_count * bpp + 7) // 8
    out = bytearray(out_len)
    rle = RleReader(data, bpp)
    line1 = [rle.next() for _ in range(w)]
    wrp = 0
    for x in range(w):
        _bits_write(out, wrp, line1[x], bpp)
        wrp += wr_size
    for _y in range(1, h):
        line2 = [rle.next() for _ in range(w)]
        if prefilter:
            for x in range(w):
                line1[x] ^= line2[x]
                _bits_write(out, wrp, line1[x], bpp)
                wrp += wr_size
        else:
            line1 = line2
            for x in range(w):
                _bits_write(out, wrp, line1[x], bpp)
                wrp += wr_size
    return bytes(out)


# ---------------------------------------------------------------------------
# C source parsing
# ---------------------------------------------------------------------------
_NUM_RE = re.compile(r"(?<![\w.])(-?)(0[xX][0-9a-fA-F]+|\d+)(?![\w.])")


def numbers(body: str) -> list[int]:
    out = []
    for m in _NUM_RE.finditer(body):
        v = int(m.group(2), 16) if m.group(2).lower().startswith("0x") else int(m.group(2))
        out.append(-v if m.group(1) == "-" else v)
    return out


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


@dataclass
class Cmap:
    range_start: int
    range_length: int
    glyph_id_start: int
    unicode_list: list[int] | None
    glyph_id_ofs_list: list[int] | None
    list_length: int
    type: str


@dataclass
class Glyph:
    bitmap_index: int
    adv_w: int
    box_w: int
    box_h: int
    ofs_x: int
    ofs_y: int


@dataclass
class Font:
    path: Path
    symbol: str
    bpp: int
    bitmap_format: int
    line_height: int
    base_line: int
    fallback: str
    kern_scale: int
    kern_classes: int
    glyph_bitmap: bytes
    glyphs: list[Glyph]
    cmaps: list[Cmap]
    kern_pairs: dict[tuple[int, int], int] = field(default_factory=dict)  # (gid_l, gid_r) -> value
    kern_class_values: dict[tuple[int, int], int] = field(default_factory=dict)  # (class_l, class_r) -> value
    left_class_mapping: list[int] = field(default_factory=list)
    right_class_mapping: list[int] = field(default_factory=list)
    underline_position: int = 0
    underline_thickness: int = 0
    subpx: int = 0
    _cp2gid: dict[int, int] | None = None
    _gid2cp: dict[int, int] | None = None

    # -- bitmap decoding ----------------------------------------------------
    def glyph_pixels(self, gid: int) -> list[int]:
        g = self.glyphs[gid]
        if g.box_w == 0 or g.box_h == 0:
            return []
        count = g.box_w * g.box_h
        if self.bitmap_format == PLAIN:
            # Plain format: glyph record starts at glyph_bitmap[bitmap_index] and
            # bits are packed continuously (no row padding) - see
            # lv_draw_sw_letter.c width_bit = box_w * bpp.
            buf = self.glyph_bitmap[g.bitmap_index:]
            start_bit = 0
        else:
            start = g.bitmap_index
            # compressed glyph record: [control byte][...]; control byte bit7 marks
            # compression (lv_font_conv always compresses every glyph).
            raw = self.glyph_bitmap[start:]
            prefilter = self.bitmap_format == COMPRESSED
            buf = decompress(raw, g.box_w, g.box_h, self.bpp, prefilter)
            start_bit = 0
        if self.bpp == 8:
            return list(buf[:count])
        return [_get_bits(buf, start_bit + i * self.bpp, self.bpp) for i in range(count)]

    # -- cmap ---------------------------------------------------------------
    @property
    def cp2gid(self) -> dict[int, int]:
        if self._cp2gid is None:
            m: dict[int, int] = {}
            for cm in self.cmaps:
                if cm.type == FORMAT0_TINY:
                    for rcp in range(cm.range_length):
                        m.setdefault(cm.range_start + rcp, cm.glyph_id_start + rcp)
                elif cm.type == FORMAT0_FULL:
                    # lv_font_conv groups consecutive glyph ids into a FORMAT0
                    # table; codepoints missing from the requested set are
                    # stored as offset 0 and map to glyph_id_start at runtime.
                    # Glyph ids inside one such table are consecutive, so a
                    # non-zero offset that does not continue the previous
                    # offset + 1 (or a later 0) is a hole, not a real mapping.
                    expected = None
                    for rcp in range(cm.range_length):
                        ofs = cm.glyph_id_ofs_list[rcp]
                        if expected is not None and ofs != expected:
                            continue  # hole
                        if ofs == 0 and rcp != 0:
                            continue  # hole (leading 0 is the first codepoint)
                        m.setdefault(cm.range_start + rcp, cm.glyph_id_start + ofs)
                        expected = ofs + 1
                elif cm.type == SPARSE_TINY:
                    for i, delta in enumerate(cm.unicode_list[:cm.list_length]):
                        m.setdefault(cm.range_start + delta, cm.glyph_id_start + i)
                elif cm.type == SPARSE_FULL:
                    for i, delta in enumerate(cm.unicode_list[:cm.list_length]):
                        m.setdefault(cm.range_start + delta, cm.glyph_id_start + cm.glyph_id_ofs_list[i])
                else:
                    raise ValueError(f"{self.path.name}: unknown cmap type {cm.type}")
            self._cp2gid = m
        return self._cp2gid

    @property
    def gid2cp(self) -> dict[int, int]:
        if self._gid2cp is None:
            self._gid2cp = {gid: cp for cp, gid in self.cp2gid.items()}
        return self._gid2cp

    def kern_by_cp(self) -> dict[tuple[int, int], int]:
        """Kerning re-keyed by codepoint pair (glyph ids are layout dependent)."""
        out: dict[tuple[int, int], int] = {}
        if self.kern_classes:
            for gid_l, cp_l in self.gid2cp.items():
                if gid_l >= len(self.left_class_mapping):
                    continue
                cl = self.left_class_mapping[gid_l]
                for gid_r, cp_r in self.gid2cp.items():
                    if gid_r >= len(self.right_class_mapping):
                        continue
                    cr = self.right_class_mapping[gid_r]
                    val = self.kern_class_values.get((cl, cr))
                    if val:
                        out[(cp_l, cp_r)] = val
        else:
            for (gid_l, gid_r), val in self.kern_pairs.items():
                cp_l, cp_r = self.gid2cp.get(gid_l), self.gid2cp.get(gid_r)
                if cp_l is not None and cp_r is not None:
                    out[(cp_l, cp_r)] = val
        return out

    @property
    def bitmap_bytes(self) -> int:
        return len(self.glyph_bitmap)

    def linked_bytes(self) -> int:
        """Approximate .rodata cost: structs + dsc arrays + bitmaps."""
        size = 28 + 24  # lv_font_t + lv_font_fmt_txt_dsc_t
        size += self.bitmap_bytes
        size += 16 * len(self.glyphs)  # LARGE glyph_dsc (4+4+2+2+1+1 padded to 16)
        size += 20 * len(self.cmaps)  # cmap struct
        for cm in self.cmaps:
            if cm.unicode_list is not None:
                size += 2 * len(cm.unicode_list)
            if cm.glyph_id_ofs_list is not None:
                size += (2 if cm.type == SPARSE_FULL else 1) * len(cm.glyph_id_ofs_list)
        size += len(self.kern_pairs) * 2  # value + glyph id (approx)
        return size


def _array_body(text: str, decl_re: str) -> str | None:
    m = re.search(decl_re + r"\s*(?:=\s*)?\{(.*?)\n\};", text, re.S)
    if not m:
        m = re.search(decl_re + r"\s*(?:=\s*)?\{(.*?)\};", text, re.S)
    return m.group(1) if m else None


def _struct_body(text: str, decl_re: str) -> str | None:
    m = re.search(decl_re + r"\s*=\s*\{(.*?)\n\};", text, re.S)
    return m.group(1) if m else None


def parse_field(body: str, name: str) -> str | None:
    m = re.search(r"\." + name + r"\s*=\s*([^,}\n]+)", body)
    return m.group(1).strip() if m else None


def parse_int_field(body: str, name: str, default: int | None = None) -> int:
    raw = parse_field(body, name)
    if raw is None:
        if default is None:
            raise ValueError(f"missing field .{name}")
        return default
    raw = raw.strip()
    try:
        return int(raw, 0)
    except ValueError:
        return default if default is not None else 0


def parse_font_file(path: Path, symbol: str | None = None) -> dict[str, Font]:
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    body = _array_body(text, r"glyph_bitmap\[\]")
    glyph_bitmap = bytes(v & 0xFF for v in numbers(body)) if body is not None else b""

    body = _struct_body(text, r"lv_font_fmt_txt_glyph_dsc_t\s+glyph_dsc\[\]")
    glyphs: list[Glyph] = []
    for entry in re.findall(r"\{([^{}]*)\}", body or ""):
        glyphs.append(Glyph(
            bitmap_index=parse_int_field(entry, "bitmap_index"),
            adv_w=parse_int_field(entry, "adv_w"),
            box_w=parse_int_field(entry, "box_w"),
            box_h=parse_int_field(entry, "box_h"),
            ofs_x=parse_int_field(entry, "ofs_x"),
            ofs_y=parse_int_field(entry, "ofs_y"),
        ))

    # cmaps
    body = _struct_body(text, r"lv_font_fmt_txt_cmap_t\s+cmaps\[\]")
    cmaps: list[Cmap] = []
    for entry in re.findall(r"\{([^{}]*)\}", body or ""):
        ul = parse_field(entry, "unicode_list")
        ofs = parse_field(entry, "glyph_id_ofs_list")
        cm = Cmap(
            range_start=parse_int_field(entry, "range_start"),
            range_length=parse_int_field(entry, "range_length"),
            glyph_id_start=parse_int_field(entry, "glyph_id_start"),
            unicode_list=None,
            glyph_id_ofs_list=None,
            list_length=parse_int_field(entry, "list_length", 0),
            type=(parse_field(entry, "type") or "").strip(),
        )
        if ul and ul != "NULL":
            ub = _array_body(text, re.escape(ul) + r"\[\]")
            cm.unicode_list = numbers(ub) if ub is not None else []
        if ofs and ofs != "NULL":
            ob = _array_body(text, re.escape(ofs) + r"\[\]")
            cm.glyph_id_ofs_list = numbers(ob) if ob is not None else []
        cmaps.append(cm)

    dsc_body = _struct_body(text, r"lv_font_fmt_txt_dsc_t\s+font_dsc")
    if dsc_body is None:
        raise ValueError(f"{path.name}: no font_dsc")
    bpp = parse_int_field(dsc_body, "bpp", 4)
    bitmap_format = parse_int_field(dsc_body, "bitmap_format", 0)
    kern_scale = parse_int_field(dsc_body, "kern_scale", 0)
    kern_classes = parse_int_field(dsc_body, "kern_classes", 0)

    fonts: dict[str, Font] = {}
    for name, struct_body in re.findall(r"const lv_font_t\s+(\w+)\s*=\s*\{(.*?)\n\};", text, re.S):
        if symbol and name != symbol:
            continue
        fallback = parse_field(struct_body, "fallback") or "NULL"
        f = Font(
            path=path,
            symbol=name,
            bpp=bpp,
            bitmap_format=bitmap_format,
            line_height=parse_int_field(struct_body, "line_height"),
            base_line=parse_int_field(struct_body, "base_line"),
            fallback=fallback,
            kern_scale=kern_scale,
            kern_classes=kern_classes,
            glyph_bitmap=glyph_bitmap,
            glyphs=glyphs,
            cmaps=cmaps,
            underline_position=parse_int_field(struct_body, "underline_position", 0),
            underline_thickness=parse_int_field(struct_body, "underline_thickness", 0),
            subpx=parse_int_field(struct_body, "subpx", 0),
        )
        _parse_kerning(text, f)
        fonts[name] = f
    if not fonts:
        raise ValueError(f"{path.name}: no const lv_font_t {symbol or ''} found")
    return fonts


def _parse_kerning(text: str, f: Font) -> None:
    if f.kern_classes:
        body = _struct_body(text, r"lv_font_fmt_txt_kern_classes_t\s+kern_classes")
        if body is None:
            return
        values_name = parse_field(body, "class_pair_values")
        lmap = parse_field(body, "left_class_mapping")
        rmap = parse_field(body, "right_class_mapping")
        if values_name:
            vb = _array_body(text, re.escape(values_name) + r"\[\]")
            vals = numbers(vb) if vb is not None else []
            lc = parse_int_field(body, "left_class_cnt", 0)
            rc = parse_int_field(body, "right_class_cnt", 0)
            for i, v in enumerate(vals):
                if lc and rc:
                    f.kern_class_values[(i // rc, i % rc)] = v
        if lmap:
            lb = _array_body(text, re.escape(lmap) + r"\[\]")
            f.left_class_mapping = numbers(lb) if lb is not None else []
        if rmap:
            rb = _array_body(text, re.escape(rmap) + r"\[\]")
            f.right_class_mapping = numbers(rb) if rb is not None else []
        return
    body = _struct_body(text, r"lv_font_fmt_txt_kern_pair_t\s+kern_pairs")
    if body is None:
        return
    ids_name = parse_field(body, "glyph_ids")
    values_name = parse_field(body, "values")
    pair_cnt = parse_int_field(body, "pair_cnt", 0)
    glyph_ids_size = parse_int_field(body, "glyph_ids_size", 0)
    if not ids_name or not values_name:
        return
    ib = _array_body(text, re.escape(ids_name) + r"\[\]")
    vb = _array_body(text, re.escape(values_name) + r"\[\]")
    ids = numbers(ib) if ib is not None else []
    values = numbers(vb) if vb is not None else []
    if glyph_ids_size == 0:
        # one uint8 per glyph id: left0,right0,left1,right1,...
        limit = min(pair_cnt, len(values), len(ids) // 2)
        for i in range(limit):
            f.kern_pairs[(ids[2 * i], ids[2 * i + 1])] = values[i]
    else:
        # two uint16 per pair: l0h,l0l,r0h,r0l,l1h,l1l,r1h,r1l,...
        limit = min(pair_cnt, len(values), len(ids) // 4)
        for i in range(limit):
            gl = (ids[4 * i] << 8) | ids[4 * i + 1]
            gr = (ids[4 * i + 2] << 8) | ids[4 * i + 3]
            f.kern_pairs[(gl, gr)] = values[i]


# ---------------------------------------------------------------------------
# Comparison
# ---------------------------------------------------------------------------
@dataclass
class Diff:
    kind: str
    cp: int | None
    detail: str


def compare_fonts(old: Font, new: Font, label: str, verbose: bool = False,
                  subset: bool = False) -> tuple[bool, list[str], dict]:
    problems: list[str] = []
    stats = {
        "old_symbol": old.symbol,
        "new_symbol": new.symbol,
        "old_glyphs_cp": len(old.cp2gid),
        "new_glyphs_cp": len(new.cp2gid),
        "compared_cp": 0,
        "old_bitmap_bytes": old.bitmap_bytes,
        "new_bitmap_bytes": new.bitmap_bytes,
        "missing_cp": 0,
        "dsc_mismatch": 0,
        "pixel_mismatch": 0,
        "kern_checked": 0,
        "kern_mismatch": 0,
    }
    if old.bpp != new.bpp:
        problems.append(f"bpp {old.bpp} -> {new.bpp}")
    for attr in ("line_height", "base_line", "fallback", "subpx", "underline_position", "underline_thickness"):
        ov, nv = getattr(old, attr), getattr(new, attr)
        if ov != nv:
            problems.append(f"{attr} {ov!r} -> {nv!r}")
    old_cp, new_cp = old.cp2gid, new.cp2gid
    missing = sorted(set(old_cp) - set(new_cp))
    stats["missing_cp"] = len(missing)
    if missing and not subset:
        problems.append(f"missing codepoints in new font: {len(missing)} e.g. {[hex(c) for c in missing[:10]]}")
    for cp in sorted(set(old_cp) & set(new_cp)):
        og = old.glyphs[old_cp[cp]]
        ng = new.glyphs[new_cp[cp]]
        stats["compared_cp"] += 1
        od = (og.adv_w, og.box_w, og.box_h, og.ofs_x, og.ofs_y)
        nd = (ng.adv_w, ng.box_w, ng.box_h, ng.ofs_x, ng.ofs_y)
        if od != nd:
            stats["dsc_mismatch"] += 1
            if len(problems) < 40:
                problems.append(f"U+{cp:04X} glyph dsc {od} -> {nd}")
        op = old.glyph_pixels(old_cp[cp])
        np_ = new.glyph_pixels(new_cp[cp])
        if op != np_:
            stats["pixel_mismatch"] += 1
            first = next((i for i, (a, b) in enumerate(zip(op, np_)) if a != b), min(len(op), len(np_)))
            if len(problems) < 40:
                problems.append(
                    f"U+{cp:04X} bitmap differs at px {first} (len {len(op)} vs {len(np_)}): "
                    f"{op[first:first + 8]} vs {np_[first:first + 8]}")
    # kerning
    ok = old.kern_by_cp()
    nk = new.kern_by_cp()
    for pair, val in sorted(ok.items()):
        stats["kern_checked"] += 1
        if nk.get(pair) != val:
            stats["kern_mismatch"] += 1
            if len(problems) < 40:
                problems.append(f"kern U+{pair[0]:04X}/U+{pair[1]:04X} {val} -> {nk.get(pair)}")
    passed = not problems
    return passed, problems, stats


def render_report(lines: list[str]) -> None:
    print("\n".join(lines))


def _load_pair(old_path: Path, new_path: Path, old_symbol: str | None, new_symbol: str | None) -> tuple[Font, Font]:
    old_fonts = parse_font_file(old_path, old_symbol)
    new_fonts = parse_font_file(new_path, new_symbol)
    if old_symbol is None:
        old_symbol = next(iter(old_fonts))
    old = old_fonts[old_symbol]
    if new_symbol is None:
        new_symbol = old_symbol
    if new_symbol not in new_fonts:
        # de-duplicated font: the .c may hold several font structs
        raise SystemExit(f"{new_path.name}: symbol {new_symbol} not found (have {list(new_fonts)})")
    return old, new_fonts[new_symbol]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baseline", default=str(ROOT / "build/font-baseline"),
                    help="directory with the pre-change font .c snapshot (mirrors src paths)")
    ap.add_argument("--manifest", default=str(ROOT / "build/font-rebuild/manifest.json"))
    ap.add_argument("--old", help="single old .c file (manual mode)")
    ap.add_argument("--new", help="single new .c file (manual mode)")
    ap.add_argument("--old-symbol")
    ap.add_argument("--new-symbol")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    failures = 0
    lines: list[str] = []
    totals = {"old_bitmap": 0, "new_bitmap": 0, "compared_cp": 0, "fonts": 0, "fails": 0, "skipped": 0}

    def run_one(label: str, old_path: Path, new_path: Path, old_sym: str | None, new_sym: str | None,
                subset: bool = False) -> None:
        nonlocal failures
        totals["fonts"] += 1
        try:
            old, new = _load_pair(old_path, new_path, old_sym, new_sym)
        except Exception as exc:  # noqa: BLE001 - report parsing problems as failures
            failures += 1
            totals["fails"] += 1
            lines.append(f"[FAIL] {label}: parse error: {exc}")
            return
        old_linked, new_linked = old.linked_bytes(), new.linked_bytes()
        if subset:
            # CJK subset: only intersection is expected to be identical, new must be
            # a subset of old.
            extra = sorted(set(new.cp2gid) - set(old.cp2gid))
            passed, problems, stats = compare_fonts(old, new, label, args.verbose, subset=True)
            if extra:
                passed = False
                problems.insert(0, f"{len(extra)} codepoints not present in baseline e.g. {[hex(c) for c in extra[:10]]}")
            tot = (f"{stats['compared_cp']:,}/{stats['old_glyphs_cp']:,} common cps "
                   f"({stats['missing_cp']:,} baseline-only cps dropped)")
        else:
            passed, problems, stats = compare_fonts(old, new, label, args.verbose)
            tot = f"{stats['compared_cp']:,} cps"
        totals["old_bitmap"] += stats["old_bitmap_bytes"]
        totals["new_bitmap"] += stats["new_bitmap_bytes"]
        totals["compared_cp"] += stats["compared_cp"]
        if passed:
            lines.append(
                f"[PASS] {label}: {tot}, {stats['kern_checked']} kern pairs, "
                f"bitmap {stats['old_bitmap_bytes']:,} -> {stats['new_bitmap_bytes']:,} B "
                f"({_pct(stats['old_bitmap_bytes'], stats['new_bitmap_bytes'])}), "
                f"linked~{old_linked:,} -> {new_linked:,} B")
        else:
            failures += 1
            totals["fails"] += 1
            lines.append(f"[FAIL] {label}: {tot}, {stats['dsc_mismatch']} dsc / {stats['pixel_mismatch']} pixel / "
                         f"{stats['missing_cp']} missing / {stats['kern_mismatch']} kern mismatches")
            for p in problems[:20]:
                lines.append(f"        - {p}")
        if args.verbose and passed:
            lines.append(f"        old={old_path.name} new={new_path.name} "
                         f"format {old.bitmap_format}->{new.bitmap_format} bpp {old.bpp}->{new.bpp}")

    if args.old and args.new:
        run_one(f"{Path(args.old).name} -> {Path(args.new).name}", Path(args.old), Path(args.new),
                args.old_symbol, args.new_symbol)
    else:
        manifest_path = Path(args.manifest)
        if not manifest_path.exists():
            print(f"manifest not found: {manifest_path}", file=sys.stderr)
            return 2
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for entry in manifest["fonts"]:
            status = entry.get("status", "rebuilt")
            label = entry["label"]
            if status == "deleted":
                totals["skipped"] += 1
                lines.append(f"[SKIP] {label}: deleted dead font (not linked in ELF)")
                continue
            if status == "merged-stub":
                totals["skipped"] += 1
                lines.append(f"[SKIP] {label}: arrays merged into {Path(entry['merged_into']).name}")
                continue
            old_path = ROOT / entry["baseline"]
            new_path = ROOT / entry["new"]
            run_one(label, old_path, new_path, entry.get("old_symbol"), entry.get("new_symbol"),
                    subset=entry.get("mode") == "subset")

    lines.append("")
    lines.append(f"summary: {totals['fonts']} fonts, {totals['compared_cp']:,} codepoints, "
                 f"{totals['fails']} FAIL, {totals['skipped']} skipped; "
                 f"bitmap {totals['old_bitmap']:,} -> {totals['new_bitmap']:,} B "
                 f"({_pct(totals['old_bitmap'], totals['new_bitmap'])})")
    lines.append("RESULT: " + ("ALL PASS" if failures == 0 else f"FAIL ({failures})"))
    render_report(lines)
    return 0 if failures == 0 else 1


def _pct(old: int, new: int) -> str:
    if old == 0:
        return "0%"
    return f"{100.0 * new / old:.1f}%"


if __name__ == "__main__":
    sys.exit(main())
