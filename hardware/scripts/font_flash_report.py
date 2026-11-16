"""Aggregate the final firmware .map by object file (post-GC attribution).

Each linked input-section line looks like:
                0x000000003c6b7400    0x13000 .../objs.a(gift_clock_day.c.o)
We sum the byte sizes per object file and print grouped totals, so optimisation
targets (fonts / images / generated screens / legacy code) are measured, not
guessed. Read-only.
"""
import re
import sys
from pathlib import Path
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[2]
MAP = ROOT / 'build/yiyi_desk/yiyi_desk.ino.map'

line_re = re.compile(r'^\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)\s+(.+?)\s*$')
obj_re = re.compile(r'\((?:objs\.a\()?([^()\s]+\.o)\)?$')

def main():
    sizes = defaultdict(int)
    text = MAP.read_text(encoding='utf-8', errors='replace')
    marker = 'Linker script and memory map'
    if marker in text:
        text = text.split(marker, 1)[1]
    current = ''
    out_re = re.compile(r'^(\.[A-Za-z0-9_.]+)\s')
    keep_prefixes = ('.flash', '.iram', '.dram', '.rtc')
    for line in text.splitlines():
        om = out_re.match(line)
        if om:
            current = om.group(1)
            continue
        if not current.startswith(keep_prefixes):
            continue
        m = line_re.match(line)
        if not m:
            continue
        size = int(m.group(1), 16)
        rest = m.group(2)
        omm = obj_re.search(rest)
        if not omm:
            continue
        sizes[omm.group(1)] += size
    total = sum(sizes.values())
    def group(pred):
        return sum(v for k, v in sizes.items() if pred(k))
    fonts = group(lambda k: ('font' in k or k.startswith('fd_sans') or 'weather_city15' in k
                             or 'weather_temp44' in k))
    images = group(lambda k: k.startswith('gift_') or 'image' in k or 'pixels' in k)
    screens = group(lambda k: k.startswith('screen_'))
    print(f'linked object bytes total: {total:,} B')
    print(f'  fonts (font/fd_sans/weather_*): {fonts:,} B')
    print(f'  gift_* images/assets         : {images:,} B')
    print(f'  generated screen_*.c         : {screens:,} B')
    print('\n=== top 40 objects by flash size ===')
    for name, size in sorted(sizes.items(), key=lambda kv: -kv[1])[:40]:
        print(f'{size:>9,}  {name}')
    print('\n=== files >= 8 KB ===')
    for name, size in sorted(sizes.items(), key=lambda kv: -kv[1]):
        if size < 8192:
            continue
        print(f'{size:>9,}  {name}')

if __name__ == '__main__':
    sys.exit(main())
