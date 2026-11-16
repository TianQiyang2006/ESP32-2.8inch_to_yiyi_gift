"""Generate LVGL RGB565 artwork and a complete BMP CJK font from local assets.

Run from any directory. The Windows font is a local build input; it is not copied.
All output is deterministic. Font: 14px, 4-bit alpha, 18px line height.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'hardware/arduino/yiyi_desk'
RANGES = [(0x20, 0x7E, 8), (0x3000, 0x303F, 16),
          (0x4E00, 0x9FFF, 16), (0xFF00, 0xFFEF, 16)]

def array_file(path, name, data, prefix='', suffix=''):
    with path.open('w', encoding='utf-8', newline='\n') as f:
        f.write(prefix + '\nconst unsigned char ' + name + '[] = {\n')
        for i in range(0, len(data), 32):
            f.write(','.join(str(x) for x in data[i:i+32]) + ',\n')
        f.write('};\n' + suffix)

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    image = Image.open(ROOT/'design/mockups/home-v3-artwork-320x240.jpg').convert('RGB')
    assert image.size == (320,240)
    pixels = bytearray()
    for r,g,b in image.getdata():
        c = (r>>3)<<11 | (g>>2)<<5 | (b>>3)
        pixels += c.to_bytes(2,'little')
    array_file(OUT/'home_art.c', 'home_art_pixels', pixels, '#include <lvgl.h>', '''
const lv_img_dsc_t home_art = {
    .header = {.cf = LV_IMG_CF_TRUE_COLOR, .always_zero = 0, .reserved = 0, .w = 320, .h = 240},
    .data_size = 153600, .data = home_art_pixels
};
''')
    font = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',14)
    bitmap = bytearray()
    offsets = []
    for start,end,width in RANGES:
        offsets.append(len(bitmap))
        for cp in range(start,end+1):
            im = Image.new('L',(width,18))
            ImageDraw.Draw(im).text((0,14),chr(cp),font=font,fill=255,anchor='ls')
            values = [min(15,(v+8)//17) for v in im.getdata()]
            bitmap.extend((values[i]<<4)|values[i+1] for i in range(0,len(values),2))
    rows = '\n'.join(f'    if (cp >= {a} && cp <= {b}) {{ *w = {w}; return {o} + (cp-{a})*{w*18//2}; }}'
                     for (a,b,w),o in zip(RANGES, offsets))
    suffix = '''
static int glyph_index(uint32_t cp, uint8_t *w) {
%s
    return -1;
}
static bool glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *d, uint32_t cp, uint32_t next) {
    (void)font; (void)next;
    uint8_t w; if (glyph_index(cp,&w) < 0) return false;
    d->adv_w = w; d->box_w = w; d->box_h = 18; d->ofs_x = 0; d->ofs_y = 0; d->bpp = 4;
    return true;
}
static const uint8_t *glyph_bitmap(const lv_font_t *font, uint32_t cp) {
    (void)font; uint8_t w; int i = glyph_index(cp,&w); return i < 0 ? 0 : desk_font_pixels+i;
}
const lv_font_t desk_font = {
    .get_glyph_dsc = glyph_dsc, .get_glyph_bitmap = glyph_bitmap,
    .line_height = 18, .base_line = 3, .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2, .underline_thickness = 1, .dsc = 0,
    .fallback = &lv_font_montserrat_14, .user_data = 0
};
''' % rows
    array_file(OUT/'desk_font.c','desk_font_pixels',bitmap,'#include <lvgl.h>',suffix)
    print(f'Artwork {len(pixels):,} bytes; font {len(bitmap):,} bytes, '
          f'{sum(b-a+1 for a,b,_ in RANGES):,} codepoints')

if __name__ == '__main__':
    main()
