// Real LVGL 8.4 pointer regression for desk.cpp's release-time click guard.
// No event is injected: every PRESSED/SCROLL/RELEASED/CLICKED comes from LVGL.
#include <lvgl.h>
#include "src/touch.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
enum class Policy { Legacy, PreserveScroll };
Policy policy = Policy::Legacy;
lv_indev_t *pointer = nullptr;
lv_obj_t *screen = nullptr, *list = nullptr, *header = nullptr;
lv_point_t point{};
bool down = false, wasDown = false;
int pressX = 0, pressY = 0;
unsigned headerClicks = 0, rowClicks = 0, backgroundClicks = 0, scrollBegins = 0;
unsigned preventedSwipes = 0, nativeScrollReleases = 0;
lv_color_t pixels[320 * 20];

void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

void trace(const char *stage) {
    std::cout << "TRACE " << stage
              << " scroll_obj=" << (lv_indev_get_scroll_obj(pointer) != nullptr)
              << " scroll_dir=" << int(lv_indev_get_scroll_dir(pointer))
              << " header=" << headerClicks << " row=" << rowClicks
              << " native_scroll_releases=" << nativeScrollReleases << std::endl;
}

void read(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point = point;
    data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    if (down && !wasDown) {
        pressX = point.x;
        pressY = point.y;
    }
    if (!down && wasDown) {
        // Production Touch::readCallback reports the gesture to desk.cpp before
        // LVGL consumes this same RELEASED sample. Match that ordering here.
        const bool moved = std::abs(int(point.x) - pressX) > 32 ||
                           std::abs(int(point.y) - pressY) > 32;
        const bool scrolling = lv_indev_get_scroll_obj(pointer) != nullptr;
        if (policy == Policy::Legacy) {
            if (moved) lv_indev_wait_release(pointer);
        } else {
            Touch::suppressSwipeClick(pointer, int(point.x) - pressX, int(point.y) - pressY);
        }
        if (moved && (policy == Policy::Legacy || !scrolling)) ++preventedSwipes;
        else if (moved && scrolling) ++nativeScrollReleases;
    }
    wasDown = down;
}

// Directly pump the real input timer at known timestamps. A timer read every
// 10 ms makes the release/next-press gap explicit and avoids a host scheduling
// delay accidentally letting the entire scroll throw finish between gestures.
void sample(int x, int y, bool pressed, unsigned elapsed = 10) {
    point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
    down = pressed;
    lv_tick_inc(elapsed);
    lv_indev_read_timer_cb(pointer->driver->read_timer);
    lv_timer_handler();
    lv_obj_update_layout(screen);
}

void idle(unsigned milliseconds) {
    for (unsigned t = 0; t < milliseconds; t += 10)
        sample(point.x, point.y, false);
}

void tap(int x, int y) {
    sample(x, y, true);
    sample(x, y, true);
    sample(x, y, false);
}

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, bool clickable) {
    auto *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    if (!clickable) lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

void buildList(bool musicLayout) {
    // desk.cpp::render resets the indev and cancels page owners before clean.
    lv_indev_reset(pointer, nullptr);
    lv_obj_clean(screen);
    list = nullptr;
    header = box(screen, 0, 0, 48, 40, true);
    lv_obj_add_event_cb(header, [](lv_event_t *) { ++headerClicks; }, LV_EVENT_CLICKED, nullptr);
    list = box(screen, 8, musicLayout ? 78 : 83, 304, 115, musicLayout);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_add_event_cb(list, [](lv_event_t *) { ++scrollBegins; }, LV_EVENT_SCROLL_BEGIN, nullptr);
    for (int i = 0; i < 20; ++i) {
        auto *row = box(list, 4, i * 40, 296, 38, true);
        lv_obj_add_event_cb(row, [](lv_event_t *) { ++rowClicks; }, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_update_layout(screen);
    // The actual reset is applied by the input timer, just like production.
    sample(0, 0, false);
    require(lv_indev_get_scroll_obj(pointer) == nullptr,
            "first list render inherited a previous scroll object despite reset");
}

void dragList() {
    const int bottom = 185;
    sample(120, bottom, true);
    sample(120, bottom, true);
    for (int y : {175, 160, 140, 115, 90}) sample(120, y, true);
    require(lv_indev_get_scroll_obj(pointer) == list, "real pointer did not begin scrolling list");
    // One release tick only: keep momentum alive for the next immediate tap.
    sample(120, 90, false);
    require(lv_indev_get_scroll_obj(pointer) == list, "scroll throw finished before rapid next tap");
    require(rowClicks == 0, "a list scroll selected a row");
    trace("after_scroll_release");
}

void init() {
    lv_init();
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, pixels, nullptr, 320 * 20);
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = 320;
    display.ver_res = 240;
    display.draw_buf = &draw;
    display.flush_cb = [](lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) {
        lv_disp_flush_ready(driver);
    };
    lv_disp_drv_register(&display);
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = read;
    pointer = lv_indev_drv_register(&input);
    // Timers are driven explicitly, so only this fixture schedules input reads.
    lv_timer_pause(pointer->driver->read_timer);
    screen = lv_scr_act();
    lv_obj_remove_style_all(screen);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void testRapidAfterScroll(bool musicLayout, bool tapRow) {
    buildList(musicLayout);
    headerClicks = rowClicks = 0;
    // First-list construction itself must leave normal buttons responsive.
    tap(20, 18);
    tap(120, 100);
    require(headerClicks == 1 && rowClicks == 1,
            "fresh list header or row could not be clicked before any scroll");
    headerClicks = rowClicks = 0;
    std::cout << "PASS immediate first-list header and row taps" << std::endl;
    dragList();
    if (tapRow) {
        tap(120, 130);
        trace("rapid_row_tap");
        idle(1000);
        tap(120, 130);
        trace("row_after_one_second");
        require(rowClicks == 2,
                "rapid row tap left stale scroll_obj: row CLICKED permanently suppressed");
        require(lv_indev_get_scroll_obj(pointer) == nullptr,
                "row tap did not clear scroll ownership");
        return;
    }
    tap(20, 18);
    trace("rapid_header_tap");
    idle(1000);
    tap(20, 18);
    trace("header_after_one_second");
    require(headerClicks == 2,
            "rapid header tap left stale scroll_obj: header CLICKED permanently suppressed");
    require(lv_indev_get_scroll_obj(pointer) == nullptr,
            "header tap did not clear scroll ownership");
}

void testNonScrollSwipe() {
    lv_indev_reset(pointer, nullptr);
    lv_obj_clean(screen);
    auto *background = box(screen, 0, 0, 320, 240, true);
    lv_obj_add_event_cb(background, [](lv_event_t *) { ++backgroundClicks; }, LV_EVENT_CLICKED, nullptr);
    sample(50, 100, false);
    sample(50, 100, true);
    sample(100, 100, true);
    sample(150, 100, true);
    require(lv_indev_get_scroll_obj(pointer) == nullptr, "non-scroll swipe unexpectedly scrolls");
    sample(150, 100, false);
    require(backgroundClicks == 0, "non-scroll swipe delivered a false CLICKED");
    tap(100, 100);
    require(backgroundClicks == 1, "non-scroll swipe guard poisoned the next tap");
    std::cout << "PASS non-scroll swipe suppresses false click and preserves next tap" << std::endl;
}

void testFirstRenderAfterOldScroll() {
    buildList(false);
    headerClicks = rowClicks = 0;
    dragList();
    // Rebuild while old scroll ownership is still live, as a new list render.
    buildList(true);
    headerClicks = rowClicks = 0;
    tap(20, 18);
    tap(120, 100);
    require(headerClicks == 1 && rowClicks == 1,
            "list render reset did not release previous view's input capture");
    std::cout << "PASS first render resets prior view's live scroll ownership" << std::endl;
}
}

int main(int argc, char **argv) {
    policy = argc > 1 && !std::strcmp(argv[1], "fixed") ? Policy::PreserveScroll : Policy::Legacy;
    const std::string scenario = argc > 2 ? argv[2] : "radio_header";
    try {
        init();
        if (scenario == "non_scroll") testNonScrollSwipe();
        else if (scenario == "first_render") testFirstRenderAfterOldScroll();
        else testRapidAfterScroll(scenario.find("music") == 0, scenario.find("row") != std::string::npos);
        std::cout << "PASS policy=" << (policy == Policy::Legacy ? "legacy" : "fixed")
                  << " scenario=" << scenario
                  << " scroll_begins=" << scrollBegins
                  << " prevented_non_scroll_swipes=" << preventedSwipes
                  << " native_scroll_releases=" << nativeScrollReleases << '\n';
        return 0;
    } catch (const std::exception &error) {
        trace("failure");
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
