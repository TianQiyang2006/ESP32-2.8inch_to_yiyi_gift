"""Build the seven-page LVGL assets from the existing HTML artwork.

Images contain no live values. The original GIFs and reference images are unchanged.
"""
from pathlib import Path
import argparse
import json
import re
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'hardware/arduino/yiyi_desk/src/gift_assets'

def array_file(path, name, data, suffix):
    with path.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('#include <lvgl.h>\nconst unsigned char '+name+'_pixels[] = {\n')
        for offset in range(0, len(data), 32):
            stream.write(','.join(map(str, data[offset:offset+32]))+',\n')
        stream.write('};\n'+suffix)

def image(name, source, size, alpha=False):
    im = Image.open(ROOT/'design'/source).convert('RGBA').resize(size, Image.Resampling.LANCZOS)
    if name=='gift_weather_art':
        # The original CSS darkens the left edge to keep white live values legible.
        overlay=Image.new('RGBA',size);draw=ImageDraw.Draw(overlay)
        for x in range(size[0]):
            opacity=round(171*max(0,1-x/(size[0]*.73)))
            draw.line((x,0,x,size[1]),fill=(23,51,24,opacity))
        im=Image.alpha_composite(im,overlay)
    pixels = bytearray()
    for r,g,b,a in im.getdata():
        pixels.extend((((r>>3)<<11)|((g>>2)<<5)|(b>>3)).to_bytes(2,'little'))
        if alpha:
            pixels.append(a)
    w,h=size
    array_file(OUT/(name+'.c'), name, pixels,
        'const lv_img_dsc_t '+name+' = {\n.header = {.cf = '+('LV_IMG_CF_TRUE_COLOR_ALPHA' if alpha else 'LV_IMG_CF_TRUE_COLOR')+', .w = '+str(w)+', .h = '+str(h)+'},\n.data_size = '+str(len(pixels))+', .data = '+name+'_pixels\n};\n')
    return len(pixels)

def indexed_image(name, source, size):
    """8-bit indexed color: 256-entry BGRA palette + one byte per pixel.

    Flash cost is about half of RGB565 with no decoder; used for the six
    full-screen clock backgrounds where soft gradients hide the dithering.
    """
    im = Image.open(ROOT/'design'/source).convert('RGB').resize(size, Image.Resampling.LANCZOS)
    quantized = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = list(quantized.getpalette() or [])
    palette += [0] * (768 - len(palette))
    pixels = bytearray()
    for index in range(256):
        r, g, b = palette[index*3:index*3+3]
        pixels.extend((b, g, r, 255))
    pixels.extend(quantized.getdata())
    w, h = size
    array_file(OUT/(name+'.c'), name, pixels,
        'const lv_img_dsc_t '+name+' = {\n.header = {.cf = LV_IMG_CF_INDEXED_8BIT, .w = '+str(w)+', .h = '+str(h)+'},\n.data_size = '+str(len(pixels))+', .data = '+name+'_pixels\n};\n')
    return len(pixels)

def font(name, px):
    # A complete small Chinese font is needed for phone-supplied city names.
    ranges=[(32,126,6),(0x3000,0x303f,px),(0x4e00,0x9fff,px),(0xff00,0xffef,px)]
    glyph=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',px)
    height=px+4
    bits=bytearray(); rows=[]
    for start,end,width in ranges:
        rows.append(f'if(cp>={start} && cp<={end}){{*w={width};return {len(bits)}+(cp-{start})*{width*height//2};}}')
        for cp in range(start,end+1):
            im=Image.new('L',(width,height)); ImageDraw.Draw(im).text((0,px),chr(cp),font=glyph,fill=255,anchor='ls')
            values=[min(15,(value+8)//17) for value in im.getdata()]
            bits.extend((values[i]<<4)|values[i+1] for i in range(0,len(values),2))
    suffix='''
static int glyph_index(uint32_t cp,uint8_t *w){%s return -1;}
static bool glyph_dsc(const lv_font_t *font,lv_font_glyph_dsc_t *d,uint32_t cp,uint32_t next){
 (void)font;(void)next;uint8_t w;if(glyph_index(cp,&w)<0)return false;
 d->adv_w=w;d->box_w=w;d->box_h=%d;d->ofs_x=0;d->ofs_y=0;d->bpp=4;return true;
}
static const uint8_t *glyph_bitmap(const lv_font_t *font,uint32_t cp){(void)font;uint8_t w;int i=glyph_index(cp,&w);return i<0?0:%s_pixels+i;}
const lv_font_t %s={.get_glyph_dsc=glyph_dsc,.get_glyph_bitmap=glyph_bitmap,.line_height=%d,.base_line=3,.fallback=&lv_font_montserrat_10};
'''%(''.join(rows),height,name,name,height)
    array_file(OUT/(name+'.c'),name,bits,suffix)
    return len(bits)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    total=0; names=[]
    definitions=[('gift_home_bg','assets/postcard-background.jpg',(320,240),False),
        ('gift_stamp','assets/postcard-art.jpg',(194,116),False),
        ('gift_weather_art','assets/postcard-weather.jpg',(83,65),False),
        ('gift_week_cat','assets/postcard-cat.png',(55,43),True),
        ('gift_daily_cat','assets/daily-mascot.png',(99,94),True),
        ('gift_message_bg','assets/message-board.jpg',(320,240),False),
        ('gift_holiday_calendar','assets/holiday-calendar.png',(110,107),True),
        ('gift_holiday_plane','assets/holiday-plane.png',(22,22),True),
        ('gift_holiday_dragon','assets/holiday-dragon.png',(22,22),True),
        ('gift_wood_art','assets/daily-wood.jpg',(100,57),False)]
    for mood in ['neutral','tired','anxious']:
        definitions += [('gift_mood_'+mood,'assets/daily-'+mood+'.png',(64,64),True),('gift_mood_small_'+mood,'assets/daily-'+mood+'.png',(27,27),True)]
    definitions += [('gift_clock_'+period,'assets/clock-'+period+'.png',(320,240),False)
        for period in ['predawn','morning','day','sunset','dusk','night']]
    definitions += [('gift_clock_preview_'+preset,'mockups/clock-'+preset+'-320x240.png',(84,63),False)
        for preset in ['landscape','matrix','flip']]
    definitions += [('gift_clock_pixel_tile','assets/clock-pixel-tile.png',(26,26),True)]
    clock_names={'gift_clock_'+period for period in ['predawn','morning','day','sunset','dusk','night']}
    for name,source,size,alpha in definitions:
        total+=(indexed_image(name,source,size) if name in clock_names else image(name,source,size,alpha));names.append(name)
    (OUT/'gift_assets.h').write_text('#pragma once\n#include <lvgl.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n'+
        '\n'.join('extern const lv_img_dsc_t '+name+';' for name in names)+
        '\n#ifdef __cplusplus\n}\n#endif\n#include "gift_fonts.h"\n#include "gift_icons.h"\n',encoding='utf-8')
    # Use the very same civil-date table as the HTML holiday page.
    text=(ROOT/'design/holiday-calendar.js').read_text(encoding='utf-8')
    table=re.search(r'const springDates\s*=\s*\((.*?)\)\.trim',text,re.S)
    dates=''.join(re.findall(r"'([^']*)'",table.group(1))).split()
    (OUT/'spring_dates.h').write_text('#pragma once\n#include <stdint.h>\nstatic const uint16_t GiftSpringDates[] = {'+
        ','.join(str(int(day[:2])*100+int(day[2:])) for day in dates)+'};\n',encoding='utf-8')
    print(f'Seven-page resources: {len(names)} images, {total:,} bytes; {len(dates)} spring-festival dates.')

def icons():
    names=[]
    for file in sorted((ROOT/'build/design-icons').glob('*.png')):
        name='gift_'+file.stem;names.append(name)
        im=Image.open(file);size=im.size
        image(name,str(file.relative_to(ROOT/'design')) if file.is_relative_to(ROOT/'design') else '../build/design-icons/'+file.name,size,True)
    (OUT/'gift_icons.h').write_text('#pragma once\n#include <lvgl.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n'+'\n'.join('extern const lv_img_dsc_t '+name+';' for name in names)+'\n#ifdef __cplusplus\n}\n#endif\n',encoding='utf-8')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clock-only',action='store_true',help='Rebuild landscape backgrounds and their picker preview, preserving other assets and headers')
    args=parser.parse_args()
    if args.clock_only:
        OUT.mkdir(parents=True,exist_ok=True)
        total=sum(indexed_image('gift_clock_'+period,'assets/clock-'+period+'.png',(320,240))
                  for period in ['predawn','morning','day','sunset','dusk','night'])
        total+=image('gift_clock_preview_landscape','mockups/clock-landscape-320x240.png',(84,63))
        print(f'Clock resources: 7 images, {total:,} bytes.')
    else:
        main()
        icons()
