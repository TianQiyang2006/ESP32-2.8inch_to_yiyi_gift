#pragma once
#include <lvgl.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <string>

// Runtime evidence from the production widgets, for scripts comparing screenshots.
inline lv_obj_t *launcherCaption(lv_obj_t *object, const char *value) {
    if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), value))
        return object;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(object); ++i)
        if (auto *found = launcherCaption(lv_obj_get_child(object, i), value)) return found;
    return nullptr;
}
inline void writeLauncherReport(lv_obj_t *root, const std::string &path) {
    static const char *names[] = {"闹钟", "番茄时钟", "音乐", "游戏", "倒计时", "倒数日"};
    lv_obj_update_layout(root);
    std::ofstream report(path); report << "[\n";
    for (unsigned index = 0; index < 6; ++index) {
        auto *caption = launcherCaption(root, names[index]); assert(caption);
        auto *tile = lv_obj_get_parent(caption);
        lv_area_t area; lv_obj_get_coords(tile, &area);
        assert(area.x1 == 20 + int(index % 3) * 100 && area.y1 == 16 + int(index / 3) * 109);
        auto *font = lv_obj_get_style_text_font(caption, 0);
        lv_point_t size{};
        lv_txt_get_size(&size, names[index], font, lv_obj_get_style_text_letter_space(caption, 0),
                        lv_obj_get_style_text_line_space(caption, 0), LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
        const bool fits = size.x <= lv_obj_get_width(caption) && size.y <= lv_obj_get_height(caption);
        bool glyphs = true; uint32_t offset = 0;
        while (names[index][offset]) {
            const auto codepoint = _lv_txt_encoded_next(names[index], &offset);
            lv_font_glyph_dsc_t glyph{};
            glyphs &= lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) && !glyph.is_placeholder;
        }
        assert(fits && glyphs);
        report << "  {\"name\":\"" << names[index] << "\",\"x\":" << area.x1
               << ",\"y\":" << area.y1 << ",\"w\":" << lv_obj_get_width(tile)
               << ",\"h\":" << lv_obj_get_height(tile)
               << ",\"fontName\":\"production-ui-font\",\"textFits\":true,\"glyphsPresent\":true}"
               << (index < 5 ? ",\n" : "\n");
    }
    report << "]\n";
}
