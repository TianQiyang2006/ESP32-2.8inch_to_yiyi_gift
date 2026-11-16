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
#include "hardware/arduino/yiyi_desk/src/pages/jump_engine.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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
std::string state() { return JumpPage::status().state; }
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
    JumpPage::refresh(true);
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
void fresh() {
    fingerDown = false; lv_indev_reset(pointer, nullptr);
    JumpPage::debugVariant(0); press("开始游戏");
    assert(state() == "playing");
}
void until(const char *expected, uint32_t limit = 3500) {
    for (uint32_t elapsed = 0; state() != expected && elapsed < limit; elapsed += 5) advance(5);
    if (state() != expected) std::cerr << "Expected " << expected << ", got " << state() << "\n";
    assert(state() == expected);
}
void holdFor(uint32_t ms, int x = 160, int y = 145) {
    sample(x, y, true); advance(ms); sample(x, y, false);
}
uint32_t perfectCharge() { return (JumpPage::status().targetDistance * 1000u + 209u) / 210u; }
void landPerfect() {
    assert(state() == "playing");
    const auto score = JumpPage::status().score;
    holdFor(perfectCharge());
    assert(state() == "jumping");
    until("playing");
    assert(JumpPage::status().score >= score + 2);
}
lv_obj_t *buttonFor(const char *value, unsigned nth = 0) {
    auto *caption = label(value, nth); assert(caption);
    while (caption && !lv_obj_has_flag(caption, LV_OBJ_FLAG_CLICKABLE)) caption = lv_obj_get_parent(caption);
    assert(caption); return caption;
}
void externalPause() {
    // An external UI action while one pointer is already held cannot be a
    // second physical finger on this single-touch panel. Exercise the real
    // Pause callback separately from ordinary coordinate-driven controls.
    lv_event_send(buttonFor("暂停"), LV_EVENT_CLICKED, nullptr);
    JumpPage::refresh(true);
    assert(state() == "paused");
}
std::vector<lv_color_t> frame() {
    Ui::cancelAnimations(); lv_refr_now(nullptr);
    return std::vector<lv_color_t>(pixels, pixels + 320 * 240);
}
void uiTests() {
    GiftUI::setAppsPage(0); show(App::None);
    assert(!GiftUI::swipeApps(80) && GiftUI::appsPageIndex() == 0);
    assert(GiftUI::swipeApps(-80) && GiftUI::appsPageIndex() == 1);
    show(App::None); assert(GiftUI::swipeApps(-80) && GiftUI::appsPageIndex() == 2);
    show(App::None); assert(!GiftUI::swipeApps(-80) && GiftUI::appsPageIndex() == 2);
    assert(GiftUI::swipeApps(80) && GiftUI::appsPageIndex() == 1);
    show(App::None); assert(GiftUI::swipeApps(80) && GiftUI::appsPageIndex() == 0);
    GiftUI::setAppsPage(1); show(App::None); press("游戏");
    assert(app == App::Games); show(App::Games);
    save("games-1", false); press("跳一跳");
    assert(app == App::Jump && renderPending); show(App::Jump);
    assert(state() == "ready"); save("jump-ready");
    press("返回", 1); assert(app == App::Games && renderPending);
    show(App::Games); press("跳一跳"); show(App::Jump); press("开始游戏");
    assert(state() == "playing" && JumpPage::status().score == 0); save("jump-start");

    // The press that closed the modal must never carry over as a charge.
    advance(250); assert(state() == "playing" && JumpPage::status().chargeMs == 0);
    JumpPage::debugVariant(0); sample(160, 180, true);
    lv_event_send(buttonFor("开始游戏"), LV_EVENT_CLICKED, nullptr);
    advance(200);
    assert(state() == "playing" && JumpPage::status().chargeMs == 0);
    sample(160, 180, false); advance(30);
    assert(state() == "playing" && JumpPage::status().jumps == 0);
    tap(160, 145); assert(state() == "jumping" || state() == "playing");
    until("playing"); assert(JumpPage::status().score == 0);

    // Actual measured pointer holds produce successful centre landings.
    const auto duration = perfectCharge(); sample(160, 145, true); advance(duration / 2);
    assert(state() == "charging" && JumpPage::status().chargeMs > 0);
    save("jump-charging");
    advance(duration - duration / 2); sample(160, 145, false);
    assert(state() == "jumping"); advance(100); save("jump-jumping");
    until("settling"); save("jump-center");
    until("playing");
    assert(JumpPage::status().score >= 2 && JumpPage::status().jumps == 1 && JumpPage::status().combo >= 1);
    save("jump-landed");
    const auto combo = JumpPage::status().combo; landPerfect();
    assert(JumpPage::status().combo > combo);

    // Holding the left edge belongs to gameplay, never app navigation.
    fresh(); const auto edgeHold = perfectCharge(); holdFor(edgeHold, 5, 170);
    until("playing"); assert(app == App::Jump && JumpPage::status().score >= 2);

    // Leaving the stage cancels instead of activating a header action or jump.
    fresh(); sample(160, 145, true); advance(250);
    sample(280, 15, true); sample(280, 15, false); advance(80);
    assert(app == App::Jump && state() == "playing" && JumpPage::status().chargeMs == 0);
    assert(JumpPage::status().jumps == 0 && JumpPage::status().score == 0);

    // Charge is capped even when the finger remains held for several seconds.
    fresh(); sample(160, 145, true); advance(2400);
    assert(state() == "charging" && JumpPage::status().chargeMs == 1000 && JumpPage::status().chargePercent == 100);
    sample(160, 145, false); until("gameover");
    assert(label("游戏结束")); save("jump-gameover");
    press("再来一局"); assert(state() == "playing" && JumpPage::status().score == 0);

    // A pause freezes an in-flight frame, then resumes the same successful hop.
    fresh(); holdFor(perfectCharge()); advance(80); press("暂停");
    assert(state() == "paused"); save("jump-paused");
    const auto frozen = frame();
    const auto flight = JumpPage::status().flightElapsedMs;
    advance(1500); const auto afterPause = frame();
    assert(!memcmp(afterPause.data(), frozen.data(), frozen.size() * sizeof(lv_color_t)));
    assert(JumpPage::status().flightElapsedMs == flight);
    press("继续"); until("playing"); assert(JumpPage::status().score >= 2);

    // External pause cancels an outstanding charge and consumes its old finger.
    fresh(); sample(160, 145, true); advance(200); externalPause();
    lv_event_send(buttonFor("继续"), LV_EVENT_CLICKED, nullptr);
    advance(80);
    assert(state() == "playing" && JumpPage::status().chargeMs == 0 && JumpPage::status().jumps == 0);
    sample(160, 145, false);
    landPerfect();

    fresh(); sample(160, 145, true); advance(200); show(App::Jump);
    sample(160, 145, false); advance(80);
    assert(state() == "playing" && JumpPage::status().chargeMs == 0 && JumpPage::status().jumps == 0);
    holdFor(perfectCharge()); advance(100);
    const auto beforeRebuild = JumpPage::status().flightElapsedMs;
    show(App::Jump);
    assert(state() == "jumping" && JumpPage::status().flightElapsedMs == beforeRebuild);
    until("playing"); assert(JumpPage::status().score >= 2);
    for (int i = 0; i < 24; ++i) {
        press("暂停"); assert(state() == "paused"); press("继续"); assert(state() == "playing");
    }
    landPerfect(); const auto best = JumpPage::status().best;
    press("暂停"); press("重新开始");
    assert(state() == "playing" && JumpPage::status().score == 0 && JumpPage::status().best == best);
    show(App::None); assert(state() == "paused"); show(App::Jump);
    assert(state() == "paused"); press("继续"); assert(state() == "playing");

    JumpPage::debugVariant(6); save("jump-falling"); until("gameover");
    press("返回", 1); assert(app == App::Games && renderPending);
    show(App::None);
    GiftUI::setAppsPage(0); show(App::None); save("launcher-1", false);
    GiftUI::setAppsPage(1); show(App::None); save("launcher-2", false);
    std::cout << "PASS: pointer hold/release, measured centre jumps/combo, short tap, max charge, outside cancel, edge hold, "
                 "pause charge/flight, rebuild, 24 overlay cycles, restart and the unified game launcher\n";
}
int persistent(const std::string &mode) {
    GiftUI::setAppsPage(2); show(App::Jump);
    if (mode == "best-restore") assert(JumpPage::status().best >= 2);
    else if (mode == "best-play" || mode == "best-leave") {
        press("开始游戏"); landPerfect(); assert(JumpPage::status().score >= 2);
        if (mode == "best-play") { holdFor(1200); until("gameover"); }
        else { show(App::None); assert(state() == "paused"); }
    } else return 2;
    std::cout << "BEST=" << JumpPage::bestScore() << "\nPASS process " << mode << "\n";
    return 0;
}
void init() {
    LittleFS.directory = "build/jump-host/store";
    std::filesystem::create_directories(LittleFS.directory);
    lv_init();
    static lv_disp_draw_buf_t drawbuf; lv_disp_draw_buf_init(&drawbuf, buffer, nullptr, 320 * 40);
    static lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res = 320; display.ver_res = 240; display.draw_buf = &drawbuf; display.flush_cb = flush;
    lv_disp_drv_register(&display);
    static lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER; input.read_cb = readPointer;
    pointer = lv_indev_drv_register(&input); lv_timer_set_period(pointer->driver->read_timer, 5);
    root = lv_scr_act(); lv_obj_remove_style_all(root); lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    Ui::setRoot(root);
    Ui::setActionHandler([](lv_event_t *e) {
        if (uintptr_t(lv_event_get_user_data(e)) == Ui::Back && !GiftUI::back()) {
            app = App::None; page = 6; renderPending = true;
        }
    });
    assert(GiftData::begin()); HostEpoch = DeskLogic::beijingEpoch(2026, 11, 12, 12, 0);
}
int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf); std::cerr.setf(std::ios::unitbuf);
#ifdef _WIN32
    _set_error_mode(1);
#endif
    init();
    if (argc > 2 && std::string(argv[1]) == "--mode") return persistent(argv[2]);
    if (argc < 2) return 2;
    out = argv[1]; std::filesystem::create_directories(out); uiTests();
    std::cout << "PASS: " << frames << " screenshots from production firmware widgets; no device access\n";
}
