#include "ui_demo.h"
#include "../../board_config.h"
#include <Arduino.h>

extern "C" const lv_font_t desk_font;

namespace UiExport {
namespace {

bool enabled_ = false;
lv_obj_t *screen_ = nullptr;   // Owned screen, exactly like generated ui_ScreenN.
lv_obj_t *noteLabel_ = nullptr;

// Built once and reused, mirroring how generated code builds its screens once in
// ui_init() and then only switches between them.
lv_obj_t *buildScreen() {
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1B2A22), 0);
    lv_obj_set_style_text_font(screen, &desk_font, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "AiDoGUI 接入演示");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F0E4), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    noteLabel_ = lv_label_create(screen);
    lv_label_set_text(noteLabel_,
        "这个屏幕由 src/ui_export/ 自己创建，\n"
        "并用 lv_scr_load() 切换进来，\n"
        "与内置界面完全不同的一套模型。\n\n"
        "用小智接入时，这条路径就是\n"
        "AiDoGUI 导出代码的接入位置。");
    lv_obj_set_style_text_color(noteLabel_, lv_color_hex(0xB9CDB4), 0);
    lv_obj_set_style_text_line_space(noteLabel_, 4, 0);
    lv_obj_align(noteLabel_, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *footer = lv_label_create(screen);
    lv_label_set_text(footer, "uiexport off 可返回内置界面");
    lv_obj_set_style_text_color(footer, lv_color_hex(0x6E8A72), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -10);

    return screen;
}

}  // namespace

bool provider(int slot) {
    if (!enabled_) return false;
    if (slot != Screens::SlotAppWeather) return false;
    if (!screen_) screen_ = buildScreen();
    if (!screen_) return false;
    if (lv_scr_act() != screen_) lv_scr_load(screen_);
    if (noteLabel_) lv_label_set_text_fmt(noteLabel_,
        "这个屏幕由 src/ui_export/ 自己创建，\n"
        "并用 lv_scr_load() 切换进来，\n"
        "与内置界面完全不同的一套模型。\n\n"
        "已接管 slot=%d（天气应用）\n"
        "LVGL %d.%d.%d",
        slot, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return true;
}

void setEnabled(bool enabled) { enabled_ = enabled; }
bool enabled() { return enabled_; }

void attach() { Screens::setProvider(provider); }
void detach() { Screens::setProvider(nullptr); }

bool selfTest() {
    bool ok = true;

    // Disabled by default: the provider must not claim anything.
    const bool savedEnabled = enabled_;
    enabled_ = false;
    ok &= !provider(Screens::SlotAppWeather);

    // When enabled it claims exactly its own slot and nothing else.
    enabled_ = true;
    for (int slot = 0; slot < Screens::SlotCount; ++slot) {
        const bool claimed = provider(slot);
        if (slot == Screens::SlotAppWeather) ok &= claimed;
        else ok &= !claimed;
    }
    // The screen must really have been created and made current. These run on the
    // LVGL thread, so touching LVGL here is safe.
    ok &= (screen_ != nullptr);
    ok &= (lv_scr_act() == screen_);

    // Restore the caller's state and hand the screen back to the seam, so a
    // self-test never leaves the device showing the demo screen.
    enabled_ = savedEnabled;
    Screens::releaseExternal();
    Screens::activateBuiltIn(nullptr);
    return ok;
}

}
