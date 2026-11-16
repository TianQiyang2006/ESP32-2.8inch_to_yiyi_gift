#include "dashboard_clock_settings.h"
#include "dashboard_clock_config.h"
#include "../gift_assets/gift_fonts.h"
#include "../saved_design/saved_resources.h"
#include "../touch.h"
#include "../ui_widgets.h"
#include <stdio.h>

namespace DashboardClockSettings {
namespace {
using DashboardClockConfig::Config;
constexpr uint32_t Bg = 0xf7f8f2, Ink = 0x294c3d, Muted = 0x52674f;
constexpr uint32_t Green = 0x527d59, Light = 0xe5eddf, White = 0xffffff;
constexpr uint32_t Colors[] = {0xffffff, 0x659fe8, 0xef7993, 0xf2a666, 0x8cdb92, 0x68cdd7};
const char *ColorNames[] = {"白色", "蓝色", "粉色", "橙色", "绿色", "青色"};
const char *Tabs[] = {"配色", "字体", "时间", "天气"};
enum Command { Tab = 10, Color = 20, Custom = 30, Save, UseColor, CancelColor,
               ToggleHour24 = 40, ToggleSeconds, ToggleTilt, ToggleLunar, ToggleFestival,
               ToggleRain, ToggleSnow, ToggleThunder, FontSlider = 100, RgbSlider = 200 };
lv_obj_t *view = nullptr, *message = nullptr, *values[3]{}, *rgbPreview = nullptr, *rgbHex = nullptr;
lv_timer_t *timer = nullptr;
Config draft;
uint32_t customColor = 0;
int tab = 0;
bool redraw = false, custom = false, dirty = false, saved = false, failed = false;
bool holding = false;

void event(lv_event_t *e);

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius = 8) {
    auto *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *label(lv_obj_t *parent, const String &s, int x, int y, int w,
                const lv_font_t *font = &fd_sans_bold_12, uint32_t ink = Ink,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    auto *o = lv_label_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, font->line_height + 2);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(ink), 0);
    lv_obj_set_style_text_align(o, align, 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    lv_label_set_text(o, s.c_str());
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *button(lv_obj_t *parent, const String &s, int x, int y, int w, int h,
                 int command, bool primary = false) {
    auto *o = box(parent, x, y, w, h, primary ? Green : Light);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, event, LV_EVENT_CLICKED, (void *)(intptr_t)command);
    Ui::attachPressFeedback(o);
    if (s.length()) label(o, s, 2, (h - fd_sans_bold_12.line_height) / 2, w - 4,
                          &fd_sans_bold_12, primary ? White : Ink, LV_TEXT_ALIGN_CENTER);
    return o;
}

void border(lv_obj_t *o, int width, uint32_t color) {
    lv_obj_set_style_border_width(o, width, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
}

void status() {
    if (!message) return;
    const char *s = failed ? "保存失败，请重试" : saved ? "已保存" : dirty ? "修改后请保存" : "所有新钟面共用";
    lv_label_set_text(message, s);
    lv_obj_set_style_text_color(message, lv_color_hex(failed ? 0xa44d43 : Muted), 0);
}

void changed() {
    dirty = true;
    saved = failed = false;
    status();
}

void header() {
    auto *back = Ui::button(view, "", 6, 2, 34, 34, Ui::Back, Bg);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    if (auto *caption = lv_obj_get_child(back, 0)) {
        lv_label_set_text(caption, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(caption, &lv_font_montserrat_20, 0);
    }
    label(view, custom ? "自定义颜色" : "钟面设置", 44, 9, 230, &fd_sans_bold_16);
    box(view, 12, 36, 296, 1, 0xe0e6da, 0);
    for (int i = 0; i < 4; ++i) {
        auto *o = button(view, Tabs[i], 12 + i * 75, 43, 71, 29, Tab + i, tab == i);
        lv_obj_set_style_radius(o, 7, 0);
    }
}

void footer() {
    box(view, 12, 200, 296, 1, 0xe0e6da, 0);
    if (custom) {
        button(view, "取消", 12, 205, 72, 32, CancelColor);
        button(view, "使用此颜色", 204, 205, 104, 32, UseColor, true);
        label(view, "尚未保存", 90, 214, 108, &fd_sans_bold_12, Muted, LV_TEXT_ALIGN_CENTER);
    } else {
        auto *cancel = Ui::button(view, "取消", 12, 205, 72, 32, Ui::Back, Light);
        if (auto *caption = lv_obj_get_child(cancel, 0)) lv_obj_set_style_text_font(caption, &fd_sans_bold_12, 0);
        button(view, "保存", 236, 205, 72, 32, Save, true);
        message = label(view, "", 88, 214, 144, &fd_sans_bold_12, Muted, LV_TEXT_ALIGN_CENTER);
        status();
    }
}

void colorPage() {
    for (int i = 0; i < 6; ++i) {
        auto *o = button(view, "", 12 + (i % 3) * 101, 82 + (i / 3) * 40, 94, 34, Color + i);
        lv_obj_set_style_bg_color(o, lv_color_hex(White), 0);
        border(o, draft.color == Colors[i] ? 2 : 1, draft.color == Colors[i] ? Green : 0xdce5d6);
        auto *swatch = box(o, 10, 7, 20, 20, Colors[i], LV_RADIUS_CIRCLE);
        border(swatch, 1, 0xd7dfd1);
        label(o, ColorNames[i], 38, 9, 48);
    }
    auto *customButton = button(view, "", 12, 165, 296, 29, Custom);
    auto *swatch = box(customButton, 10, 5, 20, 20, draft.color, 5);
    border(swatch, 1, 0xc9d6c3);
    label(customButton, "自定义颜色", 40, 8, 120);
    char hex[8]; snprintf(hex, sizeof(hex), "#%06lX", (unsigned long)draft.color);
    label(customButton, hex, 183, 8, 102, &fd_sans_bold_12, Muted, LV_TEXT_ALIGN_RIGHT);
}

lv_obj_t *slider(int x, int y, int width, int low, int high, int value, int command, uint32_t color = Green) {
    auto *o = lv_slider_create(view);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, width, 7);
    lv_slider_set_range(o, low, high);
    lv_slider_set_value(o, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(o, lv_color_hex(Light), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(o, 5, LV_PART_KNOB);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_ext_click_area(o, 8);
    lv_obj_add_event_cb(o, event, LV_EVENT_ALL, (void *)(intptr_t)command);
    return o;
}

String fontValue(int index) {
    if (index == 0) return String(draft.weight);
    if (index == 1) return String(draft.opacity) + "%";
    return String(draft.spacing + 19) + "/11";
}

void fontPage() {
    label(view, "字重与间距用于天气大字钟、纯时钟", 14, 80, 292, &fd_sans_bold_12, Muted);
    const char *names[] = {"数字字重", "数字透明度", "数字间距"};
    const int lows[] = {1, 10, -18}, highs[] = {9, 100, -8};
    const int selected[] = {draft.weight / 100, draft.opacity, draft.spacing};
    for (int i = 0; i < 3; ++i) {
        int y = 101 + i * 32;
        label(view, names[i], 14, y, 180);
        values[i] = label(view, fontValue(i), 235, y, 71, &fd_sans_bold_12, Green, LV_TEXT_ALIGN_RIGHT);
        slider(25, y + 21, 270, lows[i], highs[i], selected[i], FontSlider + i);
    }
}

void toggle(const char *name, int y, int height, bool on, int command, int x = 12, int width = 296) {
    auto *row = button(view, "", x, y, width, height, command);
    lv_obj_set_style_bg_color(row, lv_color_hex(White), 0);
    label(row, name, 12, (height - fd_sans_bold_12.line_height) / 2, width - 72);
    auto *track = box(row, width - 50, (height - 18) / 2, 38, 18, on ? Green : 0xc8d1c2, LV_RADIUS_CIRCLE);
    box(track, on ? 21 : 3, 3, 12, 12, White, LV_RADIUS_CIRCLE);
}

void timePage() {
    toggle("24 小时制", 83, 29, draft.hour24, ToggleHour24);
    toggle("显示秒钟", 117, 29, draft.seconds, ToggleSeconds);
    toggle("数字轻微倾斜", 151, 29, draft.tilt, ToggleTilt);
    label(view, "紧凑看板固定显示时分，数字保持直立", 14, 184, 292, &fd_sans_bold_12, Muted);
}

void weatherPage() {
    toggle("显示农历", 82, 32, draft.lunar, ToggleLunar, 12, 144);
    toggle("显示节日", 82, 32, draft.festival, ToggleFestival, 164, 144);
    toggle("下雨动效", 122, 32, draft.rain, ToggleRain, 12, 144);
    toggle("下雪动效", 122, 32, draft.snow, ToggleSnow, 164, 144);
    toggle("雷电动效", 162, 32, draft.thunder, ToggleThunder);
}

void rgbPage() {
    const char *names[] = {"红", "绿", "蓝"};
    const uint32_t colors[] = {0xc56f67, 0x6ba16d, 0x699bc9};
    rgbPreview = box(view, 188, 79, 28, 18, customColor, 4);
    border(rgbPreview, 1, 0xc9d6c3);
    rgbHex = label(view, "", 222, 82, 84, &fd_sans_bold_12, Green, LV_TEXT_ALIGN_RIGHT);
    char hex[8]; snprintf(hex, sizeof(hex), "#%06lX", (unsigned long)customColor);
    lv_label_set_text(rgbHex, hex);
    for (int i = 0; i < 3; ++i) {
        int y = 101 + i * 32;
        unsigned channel = (customColor >> (16 - 8 * i)) & 255;
        label(view, names[i], 14, y, 180);
        values[i] = label(view, String(channel), 235, y, 71, &fd_sans_bold_12, colors[i], LV_TEXT_ALIGN_RIGHT);
        slider(25, y + 21, 270, 0, 255, (int)channel, RgbSlider + i, colors[i]);
    }
}

void render() {
    redraw = false;
    message = rgbPreview = rgbHex = nullptr;
    for (auto &o : values) o = nullptr;
    Ui::cancelAnimations();
    lv_obj_clean(view);
    header();
    if (custom) rgbPage();
    else if (tab == 0) colorPage();
    else if (tab == 1) fontPage();
    else if (tab == 2) timePage();
    else weatherPage();
    footer();
}

void event(lv_event_t *e) {
    int command = (int)(intptr_t)lv_event_get_user_data(e);
    const auto code = lv_event_get_code(e);
    const bool isSlider = (command >= FontSlider && command < FontSlider + 3) ||
                          (command >= RgbSlider && command < RgbSlider + 3);
    if (isSlider) {
        if (code == LV_EVENT_PRESSED) holding = true;
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_DELETE) holding = false;
    }
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        int value = lv_slider_get_value(lv_event_get_target(e));
        if (command >= FontSlider && command < FontSlider + 3) {
            int index = command - FontSlider;
            if (index == 0) draft.weight = value * 100;
            else if (index == 1) draft.opacity = value;
            else draft.spacing = value;
            if (values[index]) lv_label_set_text(values[index], fontValue(index).c_str());
            changed();
        } else if (command >= RgbSlider && command < RgbSlider + 3) {
            int index = command - RgbSlider, shift = 16 - index * 8;
            customColor = (customColor & ~(0xffu << shift)) | (uint32_t(value) << shift);
            if (values[index]) lv_label_set_text(values[index], String(value).c_str());
            if (rgbPreview) lv_obj_set_style_bg_color(rgbPreview, lv_color_hex(customColor), 0);
            if (rgbHex) {
                char hex[8]; snprintf(hex, sizeof(hex), "#%06lX", (unsigned long)customColor);
                lv_label_set_text(rgbHex, hex);
            }
        }
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || Touch::swiped()) return;
    if (command >= Tab && command < Tab + 4) {
        tab = command - Tab;
        custom = false;
        redraw = true;
    } else if (command >= Color && command < Color + 6) {
        draft.color = Colors[command - Color];
        changed(); redraw = true;
    } else if (command == Custom) {
        customColor = draft.color;
        custom = true; redraw = true;
    } else if (command == UseColor) {
        draft.color = customColor;
        custom = false; changed(); redraw = true;
    } else if (command == CancelColor) {
        custom = false; redraw = true;
    } else if (command == Save) {
        String error;
        if (DashboardClockConfig::save(draft, error)) {
            dirty = false; saved = true; failed = false;
        } else {
            saved = false; failed = true;
        }
        status();
    } else {
        bool *option = nullptr;
        switch (command) {
        case ToggleHour24: option = &draft.hour24; break;
        case ToggleSeconds: option = &draft.seconds; break;
        case ToggleTilt: option = &draft.tilt; break;
        case ToggleLunar: option = &draft.lunar; break;
        case ToggleFestival: option = &draft.festival; break;
        case ToggleRain: option = &draft.rain; break;
        case ToggleSnow: option = &draft.snow; break;
        case ToggleThunder: option = &draft.thunder; break;
        }
        if (option) { *option = !*option; changed(); redraw = true; }
    }
}

void tick(lv_timer_t *) { refresh(); }

void deleted(lv_event_t *) {
    cancel();
    view = message = rgbPreview = rgbHex = nullptr;
    for (auto &o : values) o = nullptr;
    custom = dirty = saved = failed = holding = false;
}
}

void build(lv_obj_t *parent) {
    cancel();
    draft = DashboardClockConfig::load();
    tab = 0;
    custom = dirty = saved = failed = holding = false;
    view = box(parent, 0, 0, 320, 240, Bg, 0);
    lv_obj_add_event_cb(view, deleted, LV_EVENT_DELETE, nullptr);
    render();
    timer = lv_timer_create(tick, 30, nullptr);
}

void refresh() { if (view && redraw) render(); }

void cancel() {
    if (timer) { lv_timer_del(timer); timer = nullptr; }
    redraw = false;
    holding = false;
}

bool active() { return view != nullptr; }
bool inputActive() { return holding; }

bool back() {
    if (!view || !custom) return false;
    custom = false;
    redraw = true;
    return true;
}
}
