// Host-only integration tests render the production LVGL page and send pointer
// samples through LVGL's input driver. Board gestures are never fabricated events.
#include "Arduino.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/pages/snake_engine.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int64_t HostEpoch = 0;
lv_color_t pixels[320 * 240], buffer[320 * 40];
std::string out;
unsigned frames = 0;
lv_indev_t *pointer = nullptr;
lv_point_t finger = {0, 0};
bool fingerDown = false;

void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 320 && y >= 0 && y < 240) pixels[y * 320 + x] = *data;
            ++data;
        }
    lv_disp_flush_ready(drv);
}
void readPointer(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point = finger;
    data->state = fingerDown ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
lv_obj_t *findLabel(lv_obj_t *obj, const char *value, unsigned &skip) {
    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), value)) {
        if (!skip) return obj;
        --skip;
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i)
        if (auto *found = findLabel(lv_obj_get_child(obj, i), value, skip)) return found;
    return nullptr;
}
lv_obj_t *label(const char *value, unsigned skip = 0) { return findLabel(root, value, skip); }
std::string state() { return SnakePage::status().state; }
void checkLabels(lv_obj_t *obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_point_t size{};
        lv_txt_get_size(&size, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0),
                        lv_obj_get_style_text_letter_space(obj, 0), lv_obj_get_style_text_line_space(obj, 0),
                        lv_obj_get_width(obj), LV_TEXT_FLAG_NONE);
        if (size.y > lv_obj_get_height(obj)) {
            std::cerr << "Clipped label: " << lv_label_get_text(obj) << " height "
                      << size.y << "/" << lv_obj_get_height(obj) << "\n";
            assert(false);
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) checkLabels(lv_obj_get_child(obj, i));
}
void save(const std::string &name, bool validate = true) {
    lv_obj_update_layout(root);
    if (validate) checkLabels(root);
    lv_refr_now(nullptr);
    std::ofstream file(out + "/" + name + ".rgb565", std::ios::binary);
    file.write((char *)pixels, sizeof(pixels));
    ++frames;
    std::cout << "frame " << name << "\n";
}
void advance(uint32_t ms) {
    for (uint32_t done = 0; done < ms;) {
        const uint32_t step = std::min<uint32_t>(5, ms - done);
        HostMillis += step;
        lv_tick_inc(step);
        lv_timer_handler();
        done += step;
    }
    lv_obj_update_layout(root);
}
void sample(int x, int y, bool down) {
    finger.x = x; finger.y = y; fingerDown = down;
    lv_timer_ready(pointer->driver->read_timer);
    advance(5);
}
void tap(int x, int y) { sample(x, y, true); sample(x, y, false); }
void press(const char *value, unsigned nth = 0) {
    lv_obj_update_layout(root);
    auto *caption = label(value, nth);
    if (!caption) std::cerr << "Missing caption: " << value << " occurrence " << nth << "\n";
    assert(caption);
    auto *target = caption;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    lv_area_t area; lv_obj_get_coords(target, &area);
    std::cout << "finger tap " << value << "\n";
    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
    SnakePage::refresh(true);
}
void swipe(int x0, int y0, int x1, int y1) {
    sample(x0, y0, true);
    for (int i = 1; i <= 6; ++i) sample(x0 + (x1-x0)*i/6, y0 + (y1-y0)*i/6, true);
    sample(x1, y1, false);
}
void show(App target, uint8_t targetPage = 6) {
    app = target; page = targetPage;
    renderPending = false;
    GiftUI::leaveView();
    Ui::cancelAnimations(); Ui::clearStatusCluster();
    lv_obj_clean(root); SavedDesign::invalidate();
    lv_indev_reset(pointer, nullptr);
    assert(GiftUI::render(root));
    lv_obj_update_layout(root);
}
void nextMove() {
    const auto before = SnakePage::status();
    for (unsigned elapsed = 0; elapsed <= before.intervalMs + 60; elapsed += 5) {
        advance(5);
        const auto after = SnakePage::status();
        if (after.headX != before.headX || after.headY != before.headY || state() != before.state) return;
    }
    std::cerr << "Snake timer failed to produce a step\n";
    assert(false);
}
void fresh() { fingerDown = false; lv_indev_reset(pointer, nullptr); SnakePage::debugVariant(5); }
void checkSwipeOnlyBoard() {
    static_assert(SnakeEngine::Rows == 13 && SnakeEngine::Capacity == 260, "Touch board uses all 208px below header");
    for (const char *caption : {"左", "上", "下", "右"}) assert(!label(caption));
    bool found = false;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        auto *child = lv_obj_get_child(root, i);
        lv_area_t area; lv_obj_get_coords(child, &area);
        if (area.x1 == 0 && area.y1 == 32 && area.x2 == 319 && area.y2 == 239) {
            assert(lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE));
            assert(lv_obj_has_flag(child, LV_OBJ_FLAG_PRESS_LOCK));
            found = true;
        }
    }
    assert(found);
}

void uiTests() {
    GiftUI::setAppsPage(1); show(App::None); save("launcher-2", false);
    press("游戏"); assert(app == App::Games); show(App::Games);
    save("games-1", false);
    press("贪吃蛇");
    assert(app == App::Snake && renderPending);
    show(App::Snake);
    assert(state() == "ready"); save("snake-ready");
    press("返回", 1);
    assert(app == App::Games && renderPending);
    show(App::Games); press("贪吃蛇"); show(App::Snake);
    assert(state() == "ready");
    press("慢速"); assert(SnakePage::status().speed == 0);
    assert(lv_obj_get_style_bg_color(lv_obj_get_parent(label("慢速")), 0).full == lv_color_hex(0x315C43).full);
    assert(lv_obj_get_style_bg_color(lv_obj_get_parent(label("标准")), 0).full == lv_color_hex(0xE3ECD9).full);
    press("快速"); assert(SnakePage::status().speed == 2);
    assert(lv_obj_get_style_bg_color(lv_obj_get_parent(label("快速")), 0).full == lv_color_hex(0x315C43).full);
    assert(lv_obj_get_style_bg_color(lv_obj_get_parent(label("慢速")), 0).full == lv_color_hex(0xE3ECD9).full);
    press("开始游戏");
    assert(state() == "playing" && SnakePage::status().speed == 2 && SnakePage::status().intervalMs == 120);
    SnakePage::debugVariant(0);
    press("标准"); assert(SnakePage::status().speed == 1);
    press("开始游戏");
    assert(state() == "playing" && SnakePage::status().speed == 1 && SnakePage::status().intervalMs == 180);
    checkSwipeOnlyBoard();
    save("snake-start");

    // The timer earns the first 10 points through the real engine, then grows.
    fresh(); const auto beforeFood = SnakePage::status(); nextMove();
    const auto ate = SnakePage::status();
    assert(ate.score == 10 && ate.length == beforeFood.length + 1 && ate.best >= 10);
    save("snake-eat");

    // A swipe opposite the current direction is ignored.
    fresh(); swipe(190, 120, 110, 120); nextMove();
    assert(SnakePage::status().direction == 1 && SnakePage::status().headX == 9);
    fresh(); swipe(150, 145, 150, 65); nextMove();
    assert(SnakePage::status().direction == 0 && SnakePage::status().headY == 4);
    swipe(190, 120, 110, 120); nextMove(); assert(SnakePage::status().direction == 3);
    swipe(150, 65, 150, 145); nextMove(); assert(SnakePage::status().direction == 2);
    swipe(110, 120, 190, 120); nextMove(); assert(SnakePage::status().direction == 1);

    // Resting a finger or minor touch jitter does not change the direction.
    fresh(); tap(160, 130); nextMove(); assert(SnakePage::status().direction == 1);
    fresh(); sample(160, 130, true); sample(170, 139, true); sample(170, 139, false);
    nextMove(); assert(SnakePage::status().direction == 1);

    // Actual pointer motion generates the board's gesture callbacks, including
    // drags that start in the left edge reserved by normal app navigation.
    fresh(); swipe(150, 145, 150, 65); nextMove();
    assert(app == App::Snake && SnakePage::status().direction == 0);
    fresh(); swipe(150, 65, 150, 145); nextMove();
    assert(SnakePage::status().direction == 2);
    fresh(); swipe(150, 145, 150, 65); nextMove(); swipe(170, 100, 85, 100); nextMove();
    assert(SnakePage::status().direction == 3);
    fresh(); swipe(150, 145, 150, 65); nextMove(); swipe(5, 100, 95, 100); nextMove();
    assert(app == App::Snake && SnakePage::status().direction == 1);

    // The former button strip is now fully playable board. Gestures can start
    // and finish inside its bottom 40 pixels, including x=5 at the left edge.
    fresh(); swipe(160, 235, 160, 200); nextMove();
    assert(SnakePage::status().direction == 0);
    fresh(); swipe(160, 200, 160, 235); nextMove();
    assert(SnakePage::status().direction == 2);
    fresh(); swipe(150, 145, 150, 65); nextMove(); swipe(220, 235, 145, 235); nextMove();
    assert(SnakePage::status().direction == 3);
    fresh(); swipe(150, 145, 150, 65); nextMove(); swipe(5, 220, 95, 220); nextMove();
    assert(app == App::Snake && SnakePage::status().direction == 1);

    // The board owns a drag until release even when the finger travels onto
    // the header controls. These paths cross the title and pause key.
    fresh(); swipe(160, 70, 160, 10); nextMove();
    assert(state() == "playing" && SnakePage::status().direction == 0);
    fresh(); swipe(280, 70, 280, 10); nextMove();
    assert(state() == "playing" && SnakePage::status().direction == 0);
    fresh(); swipe(160, 175, 160, 225); nextMove();
    assert(state() == "playing" && SnakePage::status().direction == 2);
    nextMove(); assert(SnakePage::status().direction == 2);
    // A direction-changing drag is interpreted once per finger contact, even
    // if that contact then moves horizontally across the expanded lower board.
    fresh(); sample(160, 175, true); sample(160, 195, true);
    sample(280, 225, true); sample(280, 225, false);
    nextMove(); assert(SnakePage::status().direction == 2);
    nextMove(); assert(SnakePage::status().direction == 2 && state() == "playing");

    // Queue two turns before one timer update; both must survive in order.
    fresh(); swipe(150, 145, 150, 65); swipe(190, 120, 110, 120); nextMove();
    assert(SnakePage::status().direction == 0); nextMove();
    assert(SnakePage::status().direction == 3);

    fresh(); press("暂停"); assert(state() == "paused" && label("已暂停"));
    const auto frozen = SnakePage::status(); advance(3000);
    assert(SnakePage::status().headX == frozen.headX && SnakePage::status().headY == frozen.headY);
    save("snake-paused");
    press("快速"); assert(state() == "paused" && SnakePage::status().speed == 2);
    press("继续");
    assert(state() == "playing" && SnakePage::status().intervalMs == 120);
    const auto resumed = SnakePage::status(); advance(40);
    assert(SnakePage::status().headX == resumed.headX && SnakePage::status().headY == resumed.headY);
    nextMove();
    for (int i = 0; i < 24; ++i) {
        fresh(); press("暂停"); press("继续"); advance(80);
        assert(state() == "playing");
    }
    fresh(); show(App::Snake); assert(state() == "playing");
    press("暂停"); const auto best = SnakePage::status().best; press("重新开始");
    assert(state() == "playing" && SnakePage::status().score == 0 && SnakePage::status().best == best);

    fresh(); show(App::None); assert(state() == "paused");
    show(App::Snake); assert(label("已暂停")); press("继续"); assert(state() == "playing");
    SnakePage::debugVariant(1); save("snake-play");
    SnakePage::debugVariant(6); nextMove();
    assert(state() == "gameover" && label("游戏结束")); save("snake-gameover");
    press("慢速"); assert(state() == "gameover" && SnakePage::status().speed == 0);
    press("再来一局");
    assert(state() == "playing" && SnakePage::status().score == 0 && SnakePage::status().intervalMs == 240);
    press("暂停"); press("标准"); press("继续");
    assert(state() == "playing" && SnakePage::status().speed == 1 && SnakePage::status().intervalMs == 180);
    SnakePage::debugVariant(4); assert(state() == "won"); save("snake-won");
    // The modal's second matching label is the accessible return button; the
    // header's label stays behind the input-blocking scrim.
    press("返回", 1); assert(renderPending);
    assert(app == App::Games); show(App::Games);
    assert(app == App::Games && page == 6);
    GiftUI::setAppsPage(0); show(App::None); save("launcher-1", false);
    std::cout << "PASS: full-height swipe-only board, four swipes including edge and bottom strip, touch jitter, queued turns, timer growth, "
                 "pause/resume/restart, 24 overlay cycles, rebuild/leave lifecycle, gameover and win\n";
}
int persistent(const std::string &mode) {
    GiftUI::setAppsPage(1); show(App::Snake);
    if (mode == "best-restore") {
        assert(SnakePage::status().best >= 10);
    } else if (mode == "best-play" || mode == "best-leave") {
        fresh(); nextMove(); assert(SnakePage::status().score == 10);
        if (mode == "best-play") {
            for (unsigned guard = 0; state() == "playing" && guard < 25; ++guard) nextMove();
            assert(state() == "gameover");
        } else { show(App::None); assert(state() == "paused"); }
    } else return 2;
    std::cout << "BEST=" << SnakePage::bestScore() << "\nPASS process " << mode << "\n";
    return 0;
}
void init() {
    LittleFS.directory = "build/snake-host/store";
    std::filesystem::create_directories(LittleFS.directory);
    lv_init();
    static lv_disp_draw_buf_t drawbuf; lv_disp_draw_buf_init(&drawbuf, buffer, nullptr, 320 * 40);
    static lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res = 320; display.ver_res = 240; display.draw_buf = &drawbuf; display.flush_cb = flush;
    lv_disp_drv_register(&display);
    static lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER; input.read_cb = readPointer;
    pointer = lv_indev_drv_register(&input);
    lv_timer_set_period(pointer->driver->read_timer, 5);
    root = lv_scr_act(); lv_obj_remove_style_all(root); lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    Ui::setRoot(root);
    Ui::setActionHandler([](lv_event_t *e) {
        if (uintptr_t(lv_event_get_user_data(e)) == Ui::Back && !GiftUI::back()) {
            app = App::None; page = 6; renderPending = true;
        }
    });
    assert(GiftData::begin());
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 12, 12, 0);
}
int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf); std::cerr.setf(std::ios::unitbuf);
#ifdef _WIN32
    _set_error_mode(1);
#endif
    init();
    if (argc > 2 && std::string(argv[1]) == "--mode") return persistent(argv[2]);
    if (argc < 2) return 2;
    out = argv[1]; std::filesystem::create_directories(out);
    uiTests();
    std::cout << "PASS: " << frames << " screenshots from production firmware widgets; no serial/device access\n";
    return 0;
}
