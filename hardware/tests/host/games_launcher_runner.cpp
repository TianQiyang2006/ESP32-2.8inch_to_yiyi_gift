// Exercise the production launcher with pointer input and the shared origin table.
#include "Arduino.h"
#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/page_api.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int64_t HostEpoch = DeskLogic::beijingEpoch(2026, 11, 15, 12, 0);
static lv_color_t pixels[320 * 240], buffer[320 * 40];
static lv_point_t finger{};
static bool down = false;
static lv_indev_t *pointer = nullptr;
static std::string output;
static unsigned screenshots = 0;

struct Entry { const char *name; App app; };
static const Entry games[] = {
    {"俄罗斯方块", App::Tetris}, {"贪吃蛇", App::Snake}, {"跳一跳", App::Jump},
    {"羊了个羊", App::Sheep}, {"黄金矿工", App::Miner}, {"飞机大战", App::Plane},
    {"打砖块", App::Breakout}, {"扫雷", App::Mines},
    {"见缝插针", App::Pin}, {"记忆翻牌", App::Memory}
};

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 320 && y >= 0 && y < 240) pixels[y * 320 + x] = *data;
            ++data;
        }
    lv_disp_flush_ready(driver);
}
static void input(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point = finger;
    data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
static void advance() {
    HostMillis += 20; lv_tick_inc(20); lv_timer_handler();
}
static void tap(int x, int y) {
    finger = {(lv_coord_t)x, (lv_coord_t)y};
    down = true; lv_timer_ready(pointer->driver->read_timer); advance();
    down = false; lv_timer_ready(pointer->driver->read_timer); advance();
    lv_obj_update_layout(root);
}
static lv_obj_t *findLabel(lv_obj_t *object, const char *value) {
    if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), value))
        return object;
    for (unsigned i = lv_obj_get_child_cnt(object); i > 0; --i)
        if (auto *found = findLabel(lv_obj_get_child(object, i - 1), value)) return found;
    return nullptr;
}
static lv_obj_t *label(const char *value) { return findLabel(root, value); }
static void press(const char *value) {
    lv_obj_update_layout(root);
    auto *object = label(value);
    if (!object) std::cerr << "Missing caption: " << value << "\n";
    assert(object);
    while (object && !lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE)) object = lv_obj_get_parent(object);
    assert(object);
    lv_area_t area; lv_obj_get_coords(object, &area);
    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
}
static void rebuild() {
    renderPending = false;
    GiftUI::leaveView(); Ui::cancelAnimations(); Ui::clearStatusCluster();
    lv_obj_clean(root); SavedDesign::invalidate(); lv_indev_reset(pointer, nullptr);
    assert(GiftUI::render(root)); lv_obj_update_layout(root);
}
static void show(App value) { app = value; page = 6; rebuild(); }
static void save(const std::string &name) {
    lv_refr_now(nullptr);
    std::ofstream file(output + "/" + name + ".rgb565", std::ios::binary);
    file.write((char *)pixels, sizeof(pixels)); ++screenshots;
}
static void assertNoLooseGames() {
    for (const auto &game : games) assert(!label(game.name));
}
static void assertGlyphsAndFit(const char *value) {
    auto *object = label(value); assert(object);
    auto *font = lv_obj_get_style_text_font(object, 0);
    lv_point_t size{};
    lv_txt_get_size(&size, value, font, lv_obj_get_style_text_letter_space(object, 0),
                    lv_obj_get_style_text_line_space(object, 0), LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    assert(size.x <= lv_obj_get_width(object));
    assert(size.y <= lv_obj_get_height(object));
    uint32_t offset = 0;
    while (value[offset]) {
        uint32_t codepoint = _lv_txt_encoded_next(value, &offset);
        lv_font_glyph_dsc_t glyph{};
        assert(lv_font_get_glyph_dsc(font, &glyph, codepoint, 0));
        assert(!glyph.is_placeholder);
    }
    lv_area_t area; lv_obj_get_coords(object, &area);
    assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 320 && area.y2 < 240);
}
static void checkMenu(unsigned sheet) {
    assert(app == App::Games && label("游戏"));
    assert(label(sheet ? "2 / 2" : "1 / 2"));
    assertGlyphsAndFit("游戏");
    for (unsigned i = 0; i < 10; ++i) {
        const bool visible = (i / 6) == sheet;
        assert(bool(label(games[i].name)) == visible);
        if (visible) assertGlyphsAndFit(games[i].name);
    }
}
static void tests() {
    GiftUI::setAppsPage(0); show(App::None); assertNoLooseGames();
    assert(!label("游戏")); save("launcher-1");
    assert(!GiftUI::swipeApps(80));
    assert(GiftUI::swipeApps(-80)); rebuild();
    assert(GiftUI::appsPageIndex() == 1); assertNoLooseGames();
    for (const char *name : {"闹钟", "番茄时钟", "音乐", "游戏", "倒计时", "倒数日"})
        assert(label(name));
    save("launcher-2");
    assert(GiftUI::swipeApps(-80)); rebuild(); assertNoLooseGames();
    assert(GiftUI::appsPageIndex() == 2 && label("在线 FM") && !label("游戏"));
    assert(!GiftUI::swipeApps(-80));
    save("launcher-3");
    GiftUI::setAppsPage(99); assert(GiftUI::appsPageIndex() == 2);
    GiftUI::setAppsPage(1); rebuild();
    press("游戏"); assert(app == App::Games && renderPending); rebuild();
    checkMenu(0); save("games-1");
    const std::string firstKey = GiftUI::viewKey().c_str();
    assert(!GiftUI::swipeApps(80));
    tap(241, 16); assert(!renderPending); // disabled previous button
    tap(288, 16); assert(renderPending); rebuild();
    checkMenu(1); save("games-2");
    const std::string secondKey = GiftUI::viewKey().c_str();
    assert(firstKey != secondKey);
    assert(!GiftUI::swipeApps(-80));
    tap(288, 16); assert(!renderPending); // disabled next button
    tap(241, 16); assert(renderPending); rebuild(); checkMenu(0);

    for (unsigned i = 0; i < 10; ++i) {
        if (i == 6) { assert(GiftUI::swipeApps(-80)); rebuild(); }
        checkMenu(i / 6);
        press(games[i].name); assert(app == games[i].app && renderPending); rebuild();
        save("open-" + std::to_string(i + 1));
        // The real page header forwards Back through the same handler as the board.
        if (games[i].app == App::Tetris) tap(172, 16); else press("返回");
        assert(app == App::Games && renderPending); rebuild();
        checkMenu(i / 6);
        assert(std::string(GiftUI::viewKey().c_str()) == (i < 6 ? firstKey : secondKey));
    }
    save("games-return");
    tap(20, 16); assert(app == App::None && page == 6 && renderPending); rebuild();
    assert(GiftUI::appsPageIndex() == 1 && label("游戏"));
    press("游戏"); rebuild(); checkMenu(1); // Remember the last game list page.
    assert(GiftUI::swipeApps(80)); rebuild(); checkMenu(0);
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
        assert(GiftUI::swipeApps(-80)); rebuild(); checkMenu(1);
        press("记忆翻牌"); rebuild(); press("返回"); rebuild(); checkMenu(1);
        assert(GiftUI::swipeApps(80)); rebuild(); checkMenu(0);
    }
    std::cout << "PASS: one game entry, three app sheets, ten game callbacks, both list pages, "
                 "pointer arrows and boundaries, glyph coverage/fit, nested returns and page memory\n";
}
int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf); std::cerr.setf(std::ios::unitbuf);
#ifdef _WIN32
    _set_error_mode(1);
#endif
    if (argc < 2) return 2;
    output = argv[1]; std::filesystem::create_directories(output);
    LittleFS.directory = output + "/store"; std::filesystem::create_directories(LittleFS.directory);
    lv_init();
    static lv_disp_draw_buf_t draw; lv_disp_draw_buf_init(&draw, buffer, nullptr, 320 * 40);
    static lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res = 320; display.ver_res = 240; display.draw_buf = &draw; display.flush_cb = flush;
    lv_disp_drv_register(&display);
    static lv_indev_drv_t inputDriver; lv_indev_drv_init(&inputDriver);
    inputDriver.type = LV_INDEV_TYPE_POINTER; inputDriver.read_cb = input;
    pointer = lv_indev_drv_register(&inputDriver); lv_timer_set_period(pointer->driver->read_timer, 10);
    root = lv_scr_act(); lv_obj_remove_style_all(root); lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    Ui::setRoot(root);
    Ui::setActionHandler([](lv_event_t *event) {
        if (uintptr_t(lv_event_get_user_data(event)) == Ui::Back) assert(GiftUI::back());
    });
    assert(GiftData::begin()); tests();
    std::cout << "PASS: " << screenshots << " screenshots, host only; no device access\n";
    return 0;
}
