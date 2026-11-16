"""Instance the OFL Noto Sans SC / Noto Serif SC variable fonts for F-d.

The Windows-shipped Noto *variable* fonts are themselves SIL OFL 1.1, so the
static instances produced here are redistributable (unlike Microsoft YaHei /
Segoe UI / Georgia / KaiTi, which F-d removed).  Nothing from this script is
committed: the output lives in ``build/font-source/open/`` and is consumed by

    hardware/scripts/rebuild_fd_fonts.py

which turns them (plus the OFL Montserrat faces and Lilita One already in the
repo) into the firmware fonts.

    python hardware/scripts/prepare_open_fonts.py

Outputs
    build/font-source/open/NotoSansSC-Regular.ttf   (wght 400, CJK cmap ~20,976)
    build/font-source/open/NotoSansSC-Bold.ttf      (wght 700)
    build/font-source/open/NotoSerifSC-Regular.ttf  (wght 400, CJK cmap ~20,992)
    build/font-source/open/NotoSerifSC-Bold.ttf     (wght 700)
"""
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from pathlib import Path

# Input: the OFL variable fonts that ship with Windows.  Only the instanced
# static output is redistributed; the source path is machine-local.
SRCS = [
    (r"C:\Windows\Fonts\NotoSansSC-VF.ttf", "NotoSansSC"),
    (r"C:\Windows\Fonts\NotoSerifSC-VF.ttf", "NotoSerifSC"),
]
OUTDIR = Path("build/font-source/open")

for src, base in SRCS:
    f = TTFont(src)
    axes = {a.axisTag: (a.minValue, a.defaultValue, a.maxValue) for a in f["fvar"].axes}
    print(base, "axes:", axes, "glyphs:", len(f.getGlyphOrder()), "unitsPerEm:", f["head"].unitsPerEm)
    for wght, name in [(400, "Regular"), (700, "Bold")]:
        g = TTFont(src)
        inst = instantiateVariableFont(g, {"wght": wght}, updateFontNames=True)
        out = OUTDIR / f"{base}-{name}.ttf"
        inst.save(out, reorderTables=False)
        # verify cmap + open again
        chk = TTFont(out)
        cmap = chk.getBestCmap()
        cjk = sum(1 for cp in cmap if 0x4E00 <= cp <= 0x9FFF)
        print(f"  {out} {out.stat().st_size / 1e6:.1f}MB glyphs={len(chk.getGlyphOrder())} cjk_cmap={cjk}")
