"""Export the saved seven LVGL Pro pages into the board's LVGL 8 build.

Layout/style statements are kept verbatim; only API names, root ownership and
resource representation change. Source PNGs become native RGB565 + alpha.
"""
from pathlib import Path
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'lvgl_ui_project'
OUT = ROOT / 'hardware/arduino/yiyi_desk/src/saved_design'
# 'letter' is gone: page 5 is now the hand-written screen_schedule.cpp (课程表).
# Leaving it in PAGES regenerated a dead screen and its 8 orphan images.
PAGES = ['components', 'weather', 'holiday', 'quotes', 'daily', 'apps']
RUNTIME_IMAGES = ['gift_charge', 'gift_wifi_on']
# Fonts and images referenced only by the hand-written pages (gift_ui.cpp).
# They are not mentioned by the LVGL Pro page sources any more, so keep them in
# the generated resource header explicitly instead of dropping them.
RUNTIME_NAMES = ['gift_battery', 'gift_wifi', 'gift_yahei_b_22_0',
                 'gift_yahei_r_11_0', 'gift_yahei_r_15_4', 'gift_yahei_r_19_0',
                 'gift_yahei_r_9_0']


def alias_fonts():
    """Fonts whose glyph data already live in src/gift_assets/fd_*.c.

    Those files define the ``saved_gift_*_data`` symbols for the UI font names,
    sharing the glyph arrays of the fd_* masters. They must be declared in
    saved_resources.h but not emitted again here, otherwise the linker reports
    duplicate definitions.
    """
    symbols = set()
    for path in (ROOT / 'hardware/arduino/yiyi_desk/src/gift_assets').glob('*.c'):
        text = path.read_text(encoding='utf-8', errors='replace')
        symbols.update(re.findall(r'const\s+lv_font_t\s+(saved_\w+)_data\s*=', text))
    return symbols

def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding='utf-8') != content:
        path.write_text(content, encoding='utf-8')

def main():
    sources = [(SOURCE / f'screens/screen_{page}_gen.c').read_text(encoding='utf-8') for page in PAGES]
    names = set(re.findall(r'\bgift_\w+', '\n'.join(sources)))
    globals_xml = ET.parse(SOURCE / 'globals.xml')
    image_paths = {n.attrib['name']: SOURCE / n.attrib['src_path'] for n in globals_xml.findall('.//images/file')}
    names.update(n for n in image_paths if n.startswith(('gift_weather_', 'gift_forecast_', 'gift_mood_')))
    names.update(RUNTIME_NAMES)
    for mood in ('tired','anxious','sad'):
        image_paths[f'gift_mood_{mood}_27'] = image_paths[f'gift_mood_{mood}']
        names.add(f'gift_mood_{mood}_27')
    header = ['#pragma once', '#include <lvgl.h>', '#include <string.h>', '#ifdef __cplusplus', 'extern "C" {', '#endif']
    header += ['static inline void saved_set_name(lv_obj_t *obj, const char *name) { lv_obj_set_user_data(obj, (void *)name); }']
    header += ['void saved_contain(lv_obj_t *obj);']
    fonts = []
    images = []
    aliases = alias_fonts()
    digest = hashlib.sha256()
    for name in RUNTIME_IMAGES:
        source = (OUT / 'images' / f'{name}.c').read_text(encoding='utf-8')
        digest.update(source.encode())
        header += [f'extern const lv_img_dsc_t saved_{name}_data;',
                   f'#define saved_{name} (&saved_{name}_data)']
    for page, code in zip(PAGES, sources):
        digest.update(code.encode())
        code = code.replace('#include "../lvgl_open_template.h"', '#include "saved_resources.h"')
        code = code.replace(f'screen_{page}_create(void)', f'saved_screen_{page}_create(lv_obj_t *root)')
        code = code.replace('lv_obj_t * obj_0 = lv_obj_create(NULL);', 'lv_obj_t * obj_0 = root;')
        replacements = {'lv_obj_set_name_static': 'saved_set_name', 'lv_image_create': 'lv_img_create',
                        'lv_button_create': 'lv_btn_create', 'lv_obj_remove_flag': 'lv_obj_clear_flag',
                        'lv_image_set_src': 'lv_img_set_src', 'lv_obj_set_style_image_opa': 'lv_obj_set_style_img_opa',
                        'lv_obj_set_style_shadow_offset_x': 'lv_obj_set_style_shadow_ofs_x',
                        'lv_obj_set_style_shadow_offset_y': 'lv_obj_set_style_shadow_ofs_y',
                        'LV_LABEL_LONG_MODE_DOTS': 'LV_LABEL_LONG_DOT', 'LV_LABEL_LONG_MODE_WRAP': 'LV_LABEL_LONG_WRAP',
                        'LV_LABEL_LONG_MODE_CLIP': 'LV_LABEL_LONG_CLIP', 'LV_LABEL_LONG_MODE_SCROLL_CIRCULAR': 'LV_LABEL_LONG_SCROLL_CIRCULAR'}
        for before, after in replacements.items():
            code = code.replace(before, after)
        code = re.sub(r'lv_image_set_inner_align\((\w+), LV_IMAGE_ALIGN_CONTAIN\);', r'saved_contain(\1);', code)
        code = re.sub(r'\bgift_\w+', lambda m: 'saved_' + m.group(), code)
        # LVGL 8's default button theme adds pressed colors and padding transitions.
        code = re.sub(r'(lv_obj_t \* (obj_\d+) = lv_btn_create\([^;]+;)', r'\1\n    lv_obj_remove_style_all(\2);', code)
        write(OUT / f'screen_{page}.c', code)
        header.append(f'lv_obj_t *saved_screen_{page}_create(lv_obj_t *root);')
    fallback_sizes = {}
    for name in sorted(names):
        if name in image_paths:
            path = image_paths[name]
            raw_source = path.read_bytes()
            digest.update(raw_source)
            image = Image.open(path).convert('RGBA')
            if name.endswith('_27') and image.size != (27,27):
                image = image.resize((27,27), Image.Resampling.LANCZOS)
            data = bytearray()
            for r,g,b,a in image.getdata():
                value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                data.extend((value & 255, value >> 8, a))
            symbol = 'saved_' + name + '_data'
            values = '\n'.join(','.join(f'0x{b:02x}' for b in data[i:i+48])+',' for i in range(0,len(data),48))
            content = f'#include "../saved_resources.h"\nstatic const uint8_t pixels[] = {{\n{values}\n}};\nconst lv_img_dsc_t {symbol} = {{.header = {{.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .w = {image.width}, .h = {image.height}}}, .data_size = sizeof(pixels), .data = pixels}};\n'
            write(OUT / 'images' / (name + '.c'), content)
            header += [f'extern const lv_img_dsc_t {symbol};', f'#define saved_{name} (&{symbol})']
            images.append({'name': name, 'size': image.size, 'bytes': len(data)})
        else:
            symbol = 'saved_' + name + '_data'
            if ('saved_' + name) in aliases:
                # definition is provided by src/gift_assets/fd_*.c
                header += [f'extern const lv_font_t {symbol};',
                           f'#define saved_{name} (&{symbol})']
                fonts.append(name)
                continue
            path = SOURCE / 'fonts' / (name + '_data.c')
            code = path.read_text(encoding='utf-8')
            digest.update(code.encode())
            symbol = 'saved_' + name + '_data'
            code = code.replace(name + '_data', symbol)
            size = max(1, int(float(re.search(r'Size: ([\d.]+)', code).group(1)) + .5))
            if 'yahei' in name or 'kai' in name:
                line = int(re.search(r'\.line_height = (\d+)',code).group(1))
                base = int(re.search(r'\.base_line = (\d+)',code).group(1))
                fallback_name = f'saved_fallback_{size}_{line}_{base}'
                fallback_sizes[fallback_name] = (size,line,base)
                code = '#include "../saved_fallback.h"\n' + code.replace('.fallback = NULL', f'.fallback = &{fallback_name}')
            write(OUT / 'fonts' / (name + '.c'), code)
            header += [f'extern const lv_font_t {symbol};', f'#define saved_{name} (&{symbol})']
            fonts.append(name)
    header += ['#ifdef __cplusplus', '}', '#endif', f'#define SAVED_DESIGN_HASH "{digest.hexdigest()[:16]}"']
    write(OUT / 'saved_resources.h', '\n'.join(header)+'\n')
    fallback_header = ['#pragma once', '#include <lvgl.h>']
    fallback_c = ['#include "../gift_assets/gift_font_scale.h"', '#include "saved_fallback.h"', 'static uint8_t saved_fallback_bitmap[2048];']
    for name,(size,line,base) in fallback_sizes.items():
        fallback_header.append(f'extern const lv_font_t {name};')
        fallback_c += [f'static const gift_scale_spec {name}_spec = {{{size}, saved_fallback_bitmap, sizeof(saved_fallback_bitmap)}};', f'const lv_font_t {name} = {{.get_glyph_dsc=gift_scaled_dsc,.get_glyph_bitmap=gift_scaled_bitmap,.line_height={line},.base_line={base},.dsc=&{name}_spec}};']
    write(OUT / 'saved_fallback.h','\n'.join(fallback_header)+'\n')
    write(OUT / 'saved_fallback.c','\n'.join(fallback_c)+'\n')
    write(OUT / 'manifest.json',json.dumps({'source':'lvgl_ui_project/screens','sha256':digest.hexdigest(),'pages':PAGES,'fonts':fonts,'images':images},ensure_ascii=False,indent=2)+'\n')
    print(f'Exported {len(PAGES)} saved pages, {len(fonts)} fonts, {len(images)} images. Source hash: {digest.hexdigest()[:16]}')

if __name__ == '__main__':
    main()
