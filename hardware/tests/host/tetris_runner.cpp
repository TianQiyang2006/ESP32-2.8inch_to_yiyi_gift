#include "launcher_assertions.h"
// Host-only offscreen renderer for the Tetris mini app. It drives the real
// GiftUI::render()/TetrisPage widgets and the real button callbacks; fixtures
// below exist only in this host executable (the firmware never links them).
#include "Arduino.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int64_t HostEpoch = 0;  // set in init(); Tetris has no calendar dependency
lv_color_t pixels[320 * 240], buffer[320 * 40];
std::string out;
unsigned frames = 0;

void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 320 && y >= 0 && y < 240) pixels[y * 320 + x] = *data;
            ++data;
        }
    lv_disp_flush_ready(drv);
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
lv_obj_t *label(const char *value, unsigned skip = 0, lv_obj_t *top = nullptr) {
    return findLabel(top ? top : root, value, skip);
}
void checkLabels(lv_obj_t *obj, bool inRoller = false) {
    inRoller = inRoller || lv_obj_check_type(obj, &lv_roller_class);
    if (lv_obj_check_type(obj, &lv_label_class) && !inRoller && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        const char *value = lv_label_get_text(obj);
        auto *font = lv_obj_get_style_text_font(obj, 0);
        lv_point_t size{};
        lv_txt_get_size(&size, value, font, lv_obj_get_style_text_letter_space(obj, 0),
                        lv_obj_get_style_text_line_space(obj, 0), LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
        if (size.x > lv_obj_get_width(obj)) {
            std::cerr << "Clipped label: " << value << " " << size.x << "/" << lv_obj_get_width(obj) << "\n";
            assert(false);
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) checkLabels(lv_obj_get_child(obj, i), inRoller);
}
void save(const std::string &name, bool validate = true) {
    if (name == "launcher-2") writeLauncherReport(root, out + "/launcher-2.json");
    lv_obj_update_layout(root);
    if (validate) {
        checkLabels(root);
        checkLabels(lv_layer_top());
    }
    lv_refr_now(nullptr);
    std::ofstream file(out + "/" + name + ".rgb565", std::ios::binary);
    file.write((char *)pixels, sizeof(pixels));
    ++frames;
    std::cout << "frame " << name << "\n";
}
// Rebuilds the current view exactly like desk.cpp does for a page switch. The
// App/page are set before leaveView(), mirroring desk.cpp (which sets app before
// calling render()); TetrisPage::cancel() uses `app` to tell "left the app" from
// "same-app background rebuild".
void show(App target, uint8_t targetPage = 6) {
    app = target;
    page = targetPage;
    GiftUI::leaveView();
    Ui::cancelAnimations();
    Ui::clearStatusCluster();
    lv_obj_clean(root);
    SavedDesign::invalidate();
    assert(GiftUI::render(root));
    lv_obj_update_layout(root);
}
void click(const char *value, unsigned nth = 0, lv_obj_t *top = nullptr) {
    std::cout << "click " << value << "\n";
    unsigned skip = nth;
    auto *caption = findLabel(top ? top : root, value, skip);
    if (!caption) std::cerr << "Missing caption: " << value << "\n";
    assert(caption);
    lv_obj_t *target = caption;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    lv_event_send(target, LV_EVENT_CLICKED, nullptr);
    TetrisPage::refresh(true);
    lv_obj_update_layout(root);
}
// Emulates a real finger: PRESSED -> RELEASED -> CLICKED, then lets the shared
// press-feedback animation finish. The device sends the same sequence, which is
// what exposed a stale press-feedback handle after an overlay deleted its button.
void settle(uint32_t ms = 240) {
    for (uint32_t done = 0; done < ms; done += 20) {
        const uint32_t step = ms - done < 20 ? ms - done : 20;
        HostMillis += step;
        lv_tick_inc(step);
        lv_timer_handler();
    }
    lv_obj_update_layout(root);
}
void pressTap(const char *value, unsigned nth = 0) {
    std::cout << "press " << value << std::endl;
    unsigned skip = nth;
    auto *caption = findLabel(root, value, skip);
    assert(caption);
    lv_obj_t *target = caption;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    lv_event_send(target, LV_EVENT_PRESSED, nullptr);
    lv_event_send(target, LV_EVENT_RELEASED, nullptr);
    lv_event_send(target, LV_EVENT_CLICKED, nullptr);
    TetrisPage::refresh(true);
    settle();
}
void longPressRepeat(const char *value) {
    unsigned skip = 0;
    auto *caption = findLabel(root, value, skip);
    assert(caption);
    lv_obj_t *target = caption;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    lv_event_send(target, LV_EVENT_LONG_PRESSED_REPEAT, nullptr);
    TetrisPage::refresh(true);
}
void touch(int x, int y) {
    lv_obj_update_layout(root);
    lv_obj_t *target = nullptr;
    auto search = [&](auto &self, lv_obj_t *p) -> void {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) return;
        lv_area_t a; lv_obj_get_coords(p, &a);
        if (x < a.x1 || x > a.x2 || y < a.y1 || y > a.y2) return;
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_CLICKABLE)) target = p;
        for (unsigned i = 0; i < lv_obj_get_child_cnt(p); ++i) self(self, lv_obj_get_child(p, i));
    };
    search(search, root);
    assert(target);
    lv_event_send(target, LV_EVENT_CLICKED, nullptr);
    TetrisPage::refresh(true);
    lv_obj_update_layout(root);
}
// Advances both the firmware millis() clock and LVGL's tick, then runs the real
// lv_timer callbacks (including TetrisPage's gravity timer).
void advance(uint32_t ms) {
    for (uint32_t done = 0; done < ms; done += 20) {
        const uint32_t step = ms - done < 20 ? ms - done : 20;
        HostMillis += step;
        lv_tick_inc(step);
        lv_timer_handler();
    }
    lv_obj_update_layout(root);
}
uint32_t pixelDiff(const lv_color_t *before, const lv_color_t *after) {
    uint32_t changed = 0;
    for (int i = 0; i < 320 * 240; ++i)
        if (before[i].full != after[i].full) ++changed;
    return changed;
}
std::vector<lv_color_t> snapshot() { return std::vector<lv_color_t>(pixels, pixels + 320 * 240); }

void uiTests() {
    GiftUI::setAppsPage(1);
    show(App::None, 6);
    save("launcher-2", false);
    click("游戏"); assert(app == App::Games); show(App::Games, 6);
    save("games-1", false);
    click("俄罗斯方块");
    assert(app == App::Tetris && renderPending);
    show(App::Tetris, 6);
    assert(std::string(TetrisPage::status().state) == "playing");
    save("tetris-start");

    // Buttons drive the production event callback: move, rotate, drop.
    auto start = snapshot();
    click("左移");
    lv_refr_now(nullptr);
    assert(pixelDiff(start.data(), pixels) > 0);
    auto moved = snapshot();
    click("右移");
    lv_refr_now(nullptr);
    assert(pixelDiff(moved.data(), pixels) > 0);
    auto rotated = snapshot();
    click("旋转");
    lv_refr_now(nullptr);
    assert(pixelDiff(rotated.data(), pixels) > 0);
    auto repeated = snapshot();
    longPressRepeat("左移");
    lv_refr_now(nullptr);
    assert(pixelDiff(repeated.data(), pixels) > 0);

    const uint32_t scoreBefore = TetrisPage::status().score;
    const uint32_t bestBefore = TetrisPage::status().best;
    click("落底");
    const auto dropped = TetrisPage::status();
    assert(dropped.score > scoreBefore);
    assert(dropped.best >= bestBefore && dropped.best >= dropped.score);
    assert(std::string(dropped.state) == "playing");
    save("tetris-drop");

    // Pause freezes gravity and is exposed by the overlay. These taps use the full
    // device event sequence (press/release/click) so the shared press-feedback
    // handle is exercised exactly like a real touch.
    pressTap("暂停");
    assert(std::string(TetrisPage::status().state) == "paused");
    assert(label("已暂停"));
    const auto pausedScore = TetrisPage::status().score;
    advance(3000);
    assert(TetrisPage::status().score == pausedScore);
    save("tetris-paused-manual");
    pressTap("继续");
    assert(std::string(TetrisPage::status().state) == "playing");
    assert(!label("已暂停"));
    // Close/reopen the overlay once more, then press a game button: this is the
    // sequence that previously dereferenced the deleted overlay button handle.
    pressTap("暂停");
    pressTap("继续");
    const uint32_t scoreBeforePress = TetrisPage::status().score;
    pressTap("落底");
    assert(TetrisPage::status().score > scoreBeforePress);

    // Gravity moves the piece; the interval comes from the engine level table.
    lv_refr_now(nullptr);
    auto beforeGravity = snapshot();
    advance(900);
    lv_refr_now(nullptr);
    assert(pixelDiff(beforeGravity.data(), pixels) > 0);

    // A same-app rebuild (e.g. the pomodoro toast calling queueRender) must not
    // pause the game; only leaving the App does.
    show(App::Tetris, 6);
    assert(std::string(TetrisPage::status().state) == "playing");

    // Restart zeroes the run but keeps the best score.
    click("暂停");
    const uint32_t bestAfterRun = TetrisPage::status().best;
    click("重新开始");
    const auto restarted = TetrisPage::status();
    assert(std::string(restarted.state) == "playing");
    assert(restarted.score == 0 && restarted.lines == 0 && restarted.level == 1);
    assert(restarted.best == bestAfterRun);

    // Deterministic screenshot scenarios (production widgets, fixture state).
    TetrisPage::debugVariant(1);
    save("tetris-play");
    TetrisPage::debugVariant(2);
    save("tetris-paused");
    TetrisPage::debugVariant(3);
    save("tetris-gameover");
    assert(label("游戏结束"));
    assert(label("得分 1240 · 最高 2860"));

    // Game over -> 返回 leaves the App through the central Ui::Back dispatcher.
    pressTap("返回");
    assert(renderPending);
    assert(app == App::Games);
    show(App::Games, 6);
    assert(app == App::Games && page == 6);
    save("games-return", false);

    // Leaving an active game pauses it; re-entering shows the paused overlay.
    show(App::Tetris, 6);
    TetrisPage::debugVariant(1);
    assert(std::string(TetrisPage::status().state) == "playing");
    show(App::None, 6);
    assert(std::string(TetrisPage::status().state) == "paused");
    show(App::Tetris, 6);
    assert(label("已暂停"));
    click("继续");
    assert(std::string(TetrisPage::status().state) == "playing");

    // Page 1 is untouched so the locked launcher can still be compared exactly.
    GiftUI::setAppsPage(0);
    show(App::None, 6);
    save("launcher-1", false);
    std::cout << "PASS: launcher tile entry, real button callbacks (move/rotate/repeat/drop/pause/"
                 "resume/restart), gravity timer, pause on leave, overlays and 返回 dispatch\n";
}

int persistent(const std::string &mode) {
    GiftUI::setAppsPage(1);
    show(App::Tetris, 6);
    if (mode == "best-play") {
        // Real game-over flow: hard-drop pieces until the board tops out. The game
        // over overlay is what persists the best score.
        int guard = 0;
        while (std::string(TetrisPage::status().state) == "playing" && guard++ < 300) click("落底");
        assert(std::string(TetrisPage::status().state) == "gameover");
        assert(TetrisPage::status().best > 0);
        std::cout << "BEST=" << TetrisPage::status().best << "\n";
        std::cout << "PASS process best-play\n";
        return 0;
    }
    if (mode == "best-midplay") {
        // Beat-the-best then leave from the pause overlay: the score must be
        // persisted even though the run never reached game over.
        for (int i = 0; i < 5; ++i) click("落底");
        assert(TetrisPage::status().best > 0);
        click("暂停");
        assert(std::string(TetrisPage::status().state) == "paused");
        std::cout << "BEST=" << TetrisPage::status().best << "\n";
        std::cout << "PASS process best-midplay\n";
        return 0;
    }
    if (mode == "best-restore") {
        assert(TetrisPage::status().best > 0);
        std::cout << "BEST=" << TetrisPage::status().best << "\n";
        std::cout << "PASS process best-restore\n";
        return 0;
    }
    return 2;
}

void init() {
    LittleFS.directory = "build/tetris-host/store";
    lv_init();
    static lv_disp_draw_buf_t drawbuf;
    lv_disp_draw_buf_init(&drawbuf, buffer, nullptr, 320 * 40);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = 320;
    driver.ver_res = 240;
    driver.draw_buf = &drawbuf;
    driver.flush_cb = flush;
    lv_disp_drv_register(&driver);
    root = lv_scr_act();
    lv_obj_remove_style_all(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    Ui::setRoot(root);
    Ui::setActionHandler([](lv_event_t *e) {
        if (uintptr_t(lv_event_get_user_data(e)) == Ui::Back) {
            if (!GiftUI::back()) {
                app = App::None;
                page = 6;
                renderPending = true;
            }
        }
    });
    assert(GiftData::begin());
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 1, 23, 0);
}

int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
#ifdef _WIN32
    _set_error_mode(1);  // Assertions must go to stderr, never a modal dialog.
#endif
    if (argc > 2 && std::string(argv[1]) == "--mode") {
        init();
        return persistent(argv[2]);
    }
    if (argc < 2) return 2;
    out = argv[1];
    std::filesystem::create_directories(out);
    std::filesystem::create_directories("build/tetris-host/store");
    init();
    uiTests();
    std::cout << "PASS: " << frames << " screenshots from firmware widgets, no serial port opened or firmware flashed\n";
    return 0;
}
