// Review-only LVGL layouts. No serial connection, storage writes or scheduling.
#include <lvgl.h>
#include "hardware/arduino/yiyi_desk/src/gift_assets/gift_fonts.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_resources.h"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int64_t HostEpoch = 0;
namespace {
constexpr uint32_t Bg = 0xF7F7EE, Ink = 0x213F32, Muted = 0x51685A;
constexpr uint32_t Light = 0xE7EEDD, Green = 0x315C43, White = 0xFFFFFF;
constexpr uint32_t Faint = 0xA1B09B;
const lv_font_t *F14 = saved_gift_font_bold_14_0;
const lv_font_t *F16 = &fd_sans_bold_16;
const lv_font_t *F12 = &fd_sans_bold_12;
lv_obj_t *screen;
lv_color_t pixels[320 * 240], buffer[320 * 40];
std::string output;
unsigned frames = 0;

void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x, ++data)
            if (x >= 0 && x < 320 && y >= 0 && y < 240) pixels[y * 320 + x] = *data;
    lv_disp_flush_ready(driver);
}
lv_obj_t *box(int x, int y, int w, int h, uint32_t color = White, int radius = 10) {
    auto *o = lv_obj_create(screen); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0); lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
void text(const char *value, int x, int y, int w, const lv_font_t *font = nullptr,
          uint32_t color = Ink, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    if (!font) font = F14;
    lv_point_t size{}; lv_txt_get_size(&size, value, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    if (size.x > w || x < 0 || x + w > 320 || y < 0 || y + font->line_height + 2 > 240) {
        std::cerr << "Text does not fit: " << value << " (" << size.x << "/" << w << ")\n";
        std::abort();
    }
    // Check every code point against the exact production font and its fallbacks.
    uint32_t at = 0;
    while (value[at]) {
        auto cp = _lv_txt_encoded_next(value, &at); lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(font, &glyph, cp, 0) || glyph.is_placeholder) {
            std::cerr << "Missing glyph " << cp << " in " << value << "\n"; std::abort();
        }
    }
    auto *o = lv_label_create(screen); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, font->line_height + 2);
    lv_obj_set_style_text_font(o, font, 0); lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, align, 0); lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    lv_label_set_text(o, value); lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
}
void centered(const char *value, int y, const lv_font_t *font = nullptr, uint32_t color = Ink) {
    text(value, 12, y, 296, font, color, LV_TEXT_ALIGN_CENTER);
}
void button(const char *value, int x, int y, int w, int h, bool primary = false,
            const lv_font_t *font = nullptr) {
    if (!font) font = F16;
    box(x, y, w, h, primary ? Green : Light, 8);
    text(value, x + 4, y + (h - font->line_height) / 2, w - 8, font,
         primary ? White : Ink, LV_TEXT_ALIGN_CENTER);
}
void line(const lv_point_t *points, unsigned count, int x, int y, uint32_t color = Ink, int width = 2) {
    auto *o = lv_line_create(screen); lv_line_set_points(o, points, count); lv_obj_set_pos(o, x, y);
    lv_obj_set_style_line_color(o, lv_color_hex(color), 0); lv_obj_set_style_line_width(o, width, 0);
    lv_obj_set_style_line_rounded(o, true, 0);
}
void header(const char *title, const char *right = nullptr, bool rightButton = false) {
    static const lv_point_t back[] = {{7, 0}, {0, 7}, {7, 14}};
    line(back, 3, 14, 11, Ink, 3); text(title, 43, 9, 180, F16);
    if (right && rightButton) button(right, 236, 2, 72, 36, false, F14);
    else if (right) text(right, 230, 11, 78, F14, Muted, LV_TEXT_ALIGN_RIGHT);
}
void begin() {
    lv_obj_clean(screen); lv_obj_remove_style_all(screen); lv_obj_set_size(screen, 320, 240);
    lv_obj_set_style_bg_color(screen, lv_color_hex(Bg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0); lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}
void save(const char *name) {
    lv_obj_update_layout(screen); lv_refr_now(nullptr);
    std::ofstream file(output + "/" + name + ".rgb565", std::ios::binary);
    file.write(reinterpret_cast<char *>(pixels), sizeof(pixels)); assert(file.good());
    ++frames; std::cout << "Rendered " << name << "\n";
}
void footer(const char *left, const char *right, int leftWidth = 112) {
    button(left, 12, 198, leftWidth, 40);
    button(right, 20 + leftWidth, 198, 288 - leftWidth, 40, true);
}
void home() {
    begin(); header("倒计时", "提示音", true);
    box(12, 44, 296, 52, Light, 10);
    static const lv_point_t hourglass[] = {{0, 0}, {20, 0}, {20, 5}, {0, 23}, {0, 28},
                                          {20, 28}, {20, 23}, {0, 5}, {0, 0}};
    line(hourglass, 9, 28, 56, Green, 2);
    text("设置倒计时", 64, 49, 200, F16);
    text("选择时、分、秒", 64, 75, 184, F12, Muted);
    static const lv_point_t arrow[] = {{0, 0}, {6, 6}, {0, 12}};
    line(arrow, 3, 282, 64, Green, 2);
    text("常用计时器", 14, 109, 140, F14);
    button("+ 添加", 236, 100, 72, 30, false, F14);
    const char *names[] = {"刷牙", "蒸蛋", "面膜", "午睡"};
    const char *times[] = {"00:02:00", "00:10:00", "00:15:00", "00:30:00"};
    for (int i = 0; i < 4; ++i) {
        int x = 12 + (i % 2) * 152, y = 134 + (i / 2) * 54;
        box(x, y, 144, 48, White, 8);
        text(names[i], x + 12, y + 3, 120, F14, Muted);
        text(times[i], x + 12, y + 22, 120, &fd_mont_sb_21, Ink);
    }
    save("01-home");
}
void duration() {
    begin(); header("设置倒计时");
    const char *units[] = {"小时", "分钟", "秒钟"};
    const char *previous[] = {"23", "14", "59"};
    const char *selected[] = {"00", "15", "00"};
    const char *next[] = {"01", "16", "01"};
    box(12, 98, 296, 54, Light, 10);
    for (int i = 0; i < 3; ++i) {
        int x = 16 + i * 100;
        text(units[i], x, 42, 88, F14, Muted, LV_TEXT_ALIGN_CENTER);
        text(previous[i], x, 68, 88, &fd_mont_sb_21, Faint, LV_TEXT_ALIGN_CENTER);
        text(selected[i], x, 99, 88, &lv_font_montserrat_48, Ink, LV_TEXT_ALIGN_CENTER);
        text(next[i], x, 158, 88, &fd_mont_sb_21, Faint, LV_TEXT_ALIGN_CENTER);
    }
    text(":", 98, 98, 24, &lv_font_montserrat_48, Ink, LV_TEXT_ALIGN_CENTER);
    text(":", 198, 98, 24, &lv_font_montserrat_48, Ink, LV_TEXT_ALIGN_CENTER);
    footer("存为常用", "开始倒计时", 128);
    save("02-duration");
}
void running(bool paused) {
    begin(); header("倒计时", paused ? "已暂停" : "计时中");
    centered("面膜", 49, F16, Green);
    centered("00:12:34", 83, &lv_font_montserrat_48, Ink);
    centered("总时长 00:15:00", 147, F14, Muted);
    box(28, 175, 264, 6, Light, 3);
    box(28, 175, 221, 6, paused ? 0x83977B : Green, 3);
    footer("重置", paused ? "继续倒计时" : "暂停", 96);
    save(paused ? "04-paused" : "03-running");
}
void sounds() {
    begin(); header("提示音设置");
    const char *names[] = {"经典滴滴", "温柔钟声", "渐快提醒"};
    for (int i = 0; i < 3; ++i) {
        int y = 42 + i * 41;
        box(12, y, 296, 36, i == 1 ? Light : White, 8);
        auto *radio = box(24, y + 10, 16, 16, i == 1 ? Green : 0xCDD6C7, 8); (void)radio;
        if (i == 1) box(29, y + 15, 6, 6, White, 3);
        text(names[i], 52, y + 8, 180, F14);
        button("试听", 246, y, 62, 36, false, F14);
    }
    text("音量", 14, 171, 44, F14, Muted);
    box(72, 178, 160, 6, Light, 3); box(72, 178, 32, 6, Green, 3);
    box(96, 173, 16, 16, Green, 8);
    text("20%", 252, 170, 56, F14, Green, LV_TEXT_ALIGN_RIGHT);
    footer("取消", "保存", 96); save("05-sounds");
}
void finished() {
    begin(); centered("倒计时结束", 25, F16, Green);
    centered("00:00:00", 77, &lv_font_montserrat_48);
    centered("面膜 · 00:15:00", 142, F14, Muted);
    centered("温柔钟声", 168, F12, Muted);
    footer("停止提醒", "再来一次", 128); save("06-finished");
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    output = argv[1]; std::filesystem::create_directories(output);
    lv_init(); static lv_disp_draw_buf_t draw; lv_disp_draw_buf_init(&draw, buffer, nullptr, 320 * 40);
    static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
    driver.hor_res = 320; driver.ver_res = 240; driver.draw_buf = &draw; driver.flush_cb = flush;
    lv_disp_drv_register(&driver); screen = lv_scr_act();
    home(); duration(); running(false); running(true); sounds(); finished();
    std::cout << "PASS: " << frames << " native LVGL design screens; text bounds and glyphs checked\n";
}
