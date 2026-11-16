// Renders production clock widgets and drives their actual LVGL pointer input.
// Time/storage fault injection replaces hardware only; animation and config code
// are exactly the code linked into the ESP32 application.
#include "Arduino.h"
#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/pages/dashboard_clock_page.h"
#include "hardware/arduino/yiyi_desk/src/pages/dashboard_clock_config.h"
#include "hardware/arduino/yiyi_desk/src/pages/dashboard_clock_settings.h"
#include "hardware/arduino/yiyi_desk/src/pages/assets/dashboard_clock_masks.h"
#include "hardware/arduino/yiyi_desk/src/pages/assets/dashboard_clock_calendar.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace DashboardClockPage {
uint32_t debugPaintCount(unsigned digit);
uint32_t debugIconInvalidations();
bool debugLayoutMoving();
}
using DashboardClockConfig::Config;
int64_t HostEpoch = DeskLogic::beijingEpoch(2026, 11, 13, 9, 59) + 59;
static lv_color_t pixels[320 * 240], buffer[320 * 40];
static std::string out;
static lv_indev_t *pointer = nullptr;
static lv_point_t finger = {0, 0};
static bool fingerDown = false;
static unsigned frames = 0, checks = 0, flushCalls = 0, motionPerformanceChecks = 0;
static unsigned clippingScenes = 0, clippingGlyphs = 0, overshootScenes = 0;
static unsigned roundedCrownGlyphs = 0, compactInfoScenes = 0;
static unsigned colonClearanceScenes = 0;
static int colonMinimumClearPixels = 320;

static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *data) {
    ++flushCalls;
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 320 && y >= 0 && y < 240) pixels[y * 320 + x] = *data;
            ++data;
        }
    lv_disp_flush_ready(drv);
}
static void readPointer(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point = finger;
    data->state = fingerDown ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
static void advance(uint32_t ms) {
    while (ms) {
        const uint32_t step = std::min<uint32_t>(5, ms);
        HostMillis += step; lv_tick_inc(step); lv_timer_handler(); ms -= step;
    }
    GiftUI::refresh(true); lv_obj_update_layout(root); lv_refr_now(nullptr);
}
static lv_obj_t *findLabel(lv_obj_t *obj, const char *value, unsigned &skip) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), value)) {
        if (!skip) return obj;
        --skip;
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i)
        if (auto *found = findLabel(lv_obj_get_child(obj, i), value, skip)) return found;
    return nullptr;
}
static lv_obj_t *label(const char *value, unsigned skip = 0) { return findLabel(root, value, skip); }
static bool containsText(lv_obj_t *obj, const char *value) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;
    if (lv_obj_check_type(obj, &lv_label_class) && strstr(lv_label_get_text(obj), value)) return true;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i)
        if (containsText(lv_obj_get_child(obj, i), value)) return true;
    return false;
}
static void checkLabels(lv_obj_t *obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_point_t size{};
        const auto longMode = lv_label_get_long_mode(obj);
        const bool scrolls = longMode == LV_LABEL_LONG_SCROLL || longMode == LV_LABEL_LONG_SCROLL_CIRCULAR;
        lv_txt_get_size(&size, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0),
                        lv_obj_get_style_text_letter_space(obj, 0), lv_obj_get_style_text_line_space(obj, 0),
                        scrolls ? LV_COORD_MAX : lv_obj_get_width(obj), LV_TEXT_FLAG_NONE);
        if (size.y > lv_obj_get_height(obj)) {
            std::cerr << "Clipped label: " << lv_label_get_text(obj) << " height "
                      << size.y << "/" << lv_obj_get_height(obj) << "\n";
            assert(false);
        }
        if (lv_label_get_long_mode(obj) == LV_LABEL_LONG_DOT) {
            lv_point_t natural{};
            lv_txt_get_size(&natural, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0),
                            lv_obj_get_style_text_letter_space(obj, 0), lv_obj_get_style_text_line_space(obj, 0),
                            LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            if (natural.x > lv_obj_get_width(obj)) {
                std::cerr << "Ellipsized label: " << lv_label_get_text(obj) << " width "
                          << natural.x << "/" << lv_obj_get_width(obj) << "\n";
                assert(false);
            }
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) checkLabels(lv_obj_get_child(obj, i));
}
static void save(const std::string &name, bool validate = true) {
    lv_obj_update_layout(root);
    if (validate) checkLabels(root);
    lv_refr_now(nullptr);
    std::ofstream file(out + "/" + name + ".rgb565", std::ios::binary);
    file.write(reinterpret_cast<const char *>(pixels), sizeof(pixels)); ++frames;
    std::cout << "frame " << name << "\n";
}
static unsigned timers() {
    unsigned n = 0;
    for (auto *t = lv_timer_get_next(nullptr); t; t = lv_timer_get_next(t)) ++n;
    return n;
}
static void clean() {
    fingerDown = false; lv_indev_reset(pointer, nullptr);
    GiftUI::leaveView(); Ui::cancelAnimations(); Ui::clearStatusCluster();
    lv_obj_clean(root); SavedDesign::invalidate();
    assert(!DashboardClockPage::active() && !DashboardClockSettings::active());
}
static void rebuild() {
    clean(); Ui::setRoot(root); Ui::setPage(page); assert(GiftUI::render(root));
    GiftUI::refresh(true); lv_obj_update_layout(root); renderPending = false; lv_refr_now(nullptr);
}
static void sample(int x, int y, bool down) {
    finger.x = x; finger.y = y; fingerDown = down;
    lv_timer_ready(pointer->driver->read_timer); advance(5);
}
static void tap(int x, int y) { sample(x, y, true); sample(x, y, false); advance(40); }
static void click(const char *value, unsigned nth = 0) {
    lv_obj_update_layout(root);
    auto *target = label(value, nth);
    if (!target) std::cerr << "Missing caption: " << value << " occurrence " << nth << "\n";
    assert(target);
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    lv_area_t a; lv_obj_get_coords(target, &a);
    std::cout << "finger tap " << value << "\n";
    tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
    if (renderPending) rebuild();
}
static void picker() { GiftUI::reset(); app = App::Clock; page = 6; rebuild(); }
static void clock(const char *caption) { picker(); click(caption); assert(DashboardClockPage::active()); }
static void persist(const Config &config) {
    String error; assert(DashboardClockConfig::save(config, error));
}
static bool equalConfig(const Config &a, const Config &b) {
    return a.color == b.color && a.weight == b.weight && a.opacity == b.opacity && a.spacing == b.spacing &&
           a.seconds == b.seconds && a.hour24 == b.hour24 && a.tilt == b.tilt && a.lunar == b.lunar &&
           a.festival == b.festival && a.rain == b.rain && a.snow == b.snow && a.thunder == b.thunder;
}
static Config quiet() {
    Config c; c.tilt = false; c.rain = c.snow = c.thunder = false;
    return c;
}
static void timeIs(const char *expected, bool seconds) {
    const auto s = DashboardClockPage::status();
    if (strcmp(s.time, expected)) std::cerr << "time mismatch " << s.time << " expected " << expected << "\n";
    assert(!strcmp(s.time, expected) && s.seconds == seconds); ++checks;
}
static std::vector<lv_color_t> clockPixels() {
    lv_refr_now(nullptr);
    return {pixels + 62 * 320, pixels + 176 * 320};
}
static bool same(const std::vector<lv_color_t> &a, const std::vector<lv_color_t> &b) {
    if (a.size() != b.size()) return false;
    for (unsigned i = 0; i < a.size(); ++i) if (a[i].full != b[i].full) return false;
    return true;
}

static void collectDigits(lv_obj_t *obj, std::vector<lv_obj_t *> &digits) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(obj, &lv_img_class)) {
        auto *source = static_cast<const lv_img_dsc_t *>(lv_img_get_src(obj));
        if (source && source->header.cf == LV_IMG_CF_RGB565A8) digits.push_back(obj);
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) collectDigits(lv_obj_get_child(obj, i), digits);
}

static void checkMaskMargins() {
    // A clipped font raster cannot be repaired by enlarging its LVGL parent.
    // Check the authored alpha edges before exercising the production widgets.
    std::ofstream crowns(out + "/crown-verification.json"); crowns << "[\n";
    for (const auto &face : dashboard_clock_mask_fonts) for (const auto &glyph : face.glyphs) {
        unsigned at = 0, ink = 0;
        std::vector<uint8_t> decoded;
        for (unsigned i = 0; i < glyph.length; ++i) {
            const uint8_t run = dashboard_clock_mask_data[glyph.offset + i];
            for (unsigned n = 0; n < unsigned((run >> 4) + 1); ++n, ++at) {
                assert(at < unsigned(face.width) * face.height);
                decoded.push_back((run & 15) * 17);
                if (!(run & 15)) continue;
                ++ink;
                const unsigned x = at % face.width, y = at / face.width;
                if (!x || !y || x + 1 == face.width || y + 1 == face.height) {
                    std::cerr << "Digit source mask touches raster edge: " << face.width << "x" << face.height << "\n";
                    assert(false);
                }
            }
        }
        assert(at == unsigned(face.width) * face.height && ink);
        const unsigned faceIndex = &face - dashboard_clock_mask_fonts, digit = &glyph - face.glyphs;
        if (faceIndex < 9 && (digit == 0 || digit == 8 || digit == 9)) {
            // A round crown starts narrow and grows into its shoulder. Merely
            // adding a transparent border around a broad, sliced-off top row
            // would pass the margin checks above but fail this visual contract.
            int left = face.width, right = -1, firstRow = -1, crownWidth = 0;
            for (int y = 0; y < face.height; ++y) for (int x = 0; x < face.width; ++x) {
                if (decoded[y * face.width + x] < 128) continue;
                left = std::min(left, x); right = std::max(right, x);
                if (firstRow < 0) firstRow = y;
                if (y == firstRow) ++crownWidth;
            }
            const int inkWidth = right - left + 1;
            if (firstRow < 0 || crownWidth * 10 > inkWidth * 3 + 10) {
                std::cerr << "Flattened round digit crown: weight " << (faceIndex + 1) * 100
                          << " digit " << digit << " first row " << crownWidth << "/" << inkWidth << "\n";
                assert(false);
            }
            if (roundedCrownGlyphs) crowns << ",\n";
            crowns << "{\"weight\":" << (faceIndex + 1) * 100 << ",\"digit\":" << digit
                   << ",\"first_solid_row_width\":" << crownWidth << ",\"ink_width\":" << inkWidth
                   << ",\"rounded_crown\":true}";
            ++roundedCrownGlyphs; ++checks;
        }
        if (&glyph == &face.glyphs[8] && (&face == &dashboard_clock_mask_fonts[6] || &face == &dashboard_clock_mask_fonts[9])) {
            const char *name = &face == &dashboard_clock_mask_fonts[6] ? "mask-rounded-700-8.pgm" : "mask-compact-8.pgm";
            std::ofstream mask(out + "/" + name, std::ios::binary);
            mask << "P5\n" << face.width << " " << face.height << "\n255\n";
            mask.write(reinterpret_cast<const char *>(decoded.data()), decoded.size());
        }
    }
    crowns << "\n]\n";
    ++checks;
}

static void checkRenderedDigitBounds(lv_obj_t *image) {
    auto *source = static_cast<const lv_img_dsc_t *>(lv_img_get_src(image));
    const int width = source->header.w, height = source->header.h;
    const auto *alpha = source->data + width * height * 2;
    // Only settled frames are checked. The intentional rolling viewport is
    // allowed to show two partial digits while the old/new values slide past.
    for (int y = 0; y < height; ++y) assert(!alpha[y * width] && !alpha[y * width + width - 1]);
    for (int x = 0; x < width; ++x) assert(!alpha[x] && !alpha[(height - 1) * width + x]);

    lv_area_t actual; lv_obj_get_coords(image, &actual);
    lv_point_t pivot; lv_img_get_pivot(image, &pivot);
    // Render this exact production descriptor through LVGL in an unclipped,
    // centred reference. Compare its visible ink, including antialiasing and
    // rotation, with every real ancestor's clipping rectangle.
    auto *probe = lv_obj_create(root); lv_obj_remove_style_all(probe);
    lv_obj_set_pos(probe, 0, 0); lv_obj_set_size(probe, 320, 240);
    lv_obj_set_style_bg_color(probe, lv_color_black(), 0); lv_obj_set_style_bg_opa(probe, 255, 0);
    lv_obj_clear_flag(probe, LV_OBJ_FLAG_SCROLLABLE);
    auto *reference = lv_img_create(probe);
    lv_img_set_src(reference, source); lv_img_set_pivot(reference, pivot.x, pivot.y);
    lv_img_set_zoom(reference, lv_img_get_zoom(image)); lv_img_set_angle(reference, lv_img_get_angle(image));
    lv_img_set_antialias(reference, lv_img_get_antialias(image));
    const int left = 160 - width / 2, top = 120 - height / 2;
    lv_obj_set_pos(reference, left, top);
    lv_obj_update_layout(root); lv_refr_now(nullptr);
    lv_area_t ink = {320, 240, -1, -1};
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) if (pixels[y * 320 + x].full) {
        ink.x1 = std::min<int>(ink.x1, x); ink.y1 = std::min<int>(ink.y1, y);
        ink.x2 = std::max<int>(ink.x2, x); ink.y2 = std::max<int>(ink.y2, y);
    }
    assert(ink.x1 > 0 && ink.y1 > 0 && ink.x2 < 319 && ink.y2 < 239);
    ink.x1 += actual.x1 - left; ink.x2 += actual.x1 - left;
    ink.y1 += actual.y1 - top; ink.y2 += actual.y1 - top;
    for (auto *ancestor = lv_obj_get_parent(image); ancestor; ancestor = lv_obj_get_parent(ancestor)) {
        lv_area_t clip; lv_obj_get_coords(ancestor, &clip);
        if (ink.x1 < clip.x1 || ink.y1 < clip.y1 || ink.x2 > clip.x2 || ink.y2 > clip.y2) {
            std::cerr << "Clipped rendered digit: ink [" << ink.x1 << "," << ink.y1 << ","
                      << ink.x2 << "," << ink.y2 << "] parent [" << clip.x1 << "," << clip.y1
                      << "," << clip.x2 << "," << clip.y2 << "]\n";
            assert(false);
        }
    }
    lv_obj_del(probe); lv_obj_invalidate(root); lv_refr_now(nullptr); ++clippingGlyphs;
}

static void clippingTests() {
    checkMaskMargins();
    const char *names[] = {"天气大字钟", "天气大字钟", "纯时钟", "纯时钟", "紧凑看板"};
    const char *ids[] = {"dashboard-hhmm", "dashboard-hhmmss", "minimal-hhmm", "minimal-hhmmss", "compact-hhmm"};
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 8, 28) + 8;
    for (unsigned mode = 0; mode < 5; ++mode) for (int spacing : {-18, -8}) {
        Config c = quiet(); c.weight = 900; c.opacity = 100; c.seconds = mode == 1 || mode == 3;
        c.tilt = mode != 4;
        c.spacing = spacing; persist(c); clock(names[mode]);
        std::vector<lv_obj_t *> digits; collectDigits(root, digits);
        assert(digits.size() == (c.seconds && mode != 4 ? 6u : 4u));
        assert(!DashboardClockPage::status().movingDigits);
        for (int angle : {-60, 0, 60}) {
            if (mode == 4 && angle) continue;
            for (auto *digit : digits) lv_img_set_angle(digit, angle);
            lv_obj_update_layout(root); lv_obj_invalidate(root); lv_refr_now(nullptr);
            for (auto *digit : digits) checkRenderedDigitBounds(digit);
            save(std::string("clip-") + ids[mode] + "-spacing" + std::to_string(-spacing) + "-angle" + std::to_string(angle));
            ++clippingScenes; ++checks;
        }
    }
}

static void exportContainerBounds(const char *name) {
    std::vector<lv_obj_t *> digits; collectDigits(root, digits);
    std::ofstream evidence(out + "/" + name + ".json"); evidence << "[\n";
    for (unsigned i = 0; i < digits.size(); ++i) {
        auto *image = digits[i]; auto *source = static_cast<const lv_img_dsc_t *>(lv_img_get_src(image));
        lv_area_t position; lv_obj_get_coords(image, &position);
        lv_point_t pivot; lv_img_get_pivot(image, &pivot); lv_area_t transformed;
        _lv_img_buf_get_transformed_area(&transformed, source->header.w, source->header.h,
                                         lv_img_get_angle(image), lv_img_get_zoom(image), &pivot);
        if (i) evidence << ",\n";
        evidence << "[" << transformed.x1 + position.x1 << "," << transformed.y1 + position.y1 << ","
                 << transformed.x2 + position.x1 << "," << transformed.y2 + position.y1 << "]";
    }
    evidence << "\n]\n";
}

static std::vector<uint8_t> alphaPixels(lv_obj_t *image) {
    auto *source = static_cast<const lv_img_dsc_t *>(lv_img_get_src(image));
    const unsigned count = source->header.w * source->header.h;
    return {source->data + count * 2, source->data + count * 3};
}

static int completeTargetShift(const std::vector<uint8_t> &target, lv_obj_t *image) {
    auto *source = static_cast<const lv_img_dsc_t *>(lv_img_get_src(image));
    const int width = source->header.w, height = source->header.h;
    const auto actual = alphaPixels(image); assert(actual.size() == target.size());
    // Find an exact, complete target silhouette translated upwards. There is no
    // curve or viewport-size calculation here: the oracle is a previously
    // captured settled raster of the same production target glyph.
    for (int shift = -1; shift >= -height / 4; --shift) {
        bool complete = true;
        for (int y = 0; complete && y < height; ++y) for (int x = 0; x < width; ++x) {
            const uint8_t expected = target[y * width + x];
            if (expected && (y + shift < 0 || actual[(y + shift) * width + x] != expected)) {
                complete = false; break;
            }
        }
        if (complete) return shift;
    }
    return 0;
}

static void overshootTests() {
    const char *names[] = {"天气大字钟", "天气大字钟", "纯时钟", "纯时钟", "紧凑看板"};
    const char *ids[] = {"dashboard-hhmm", "dashboard-hhmmss", "minimal-hhmm", "minimal-hhmmss", "compact-hhmm"};
    const int64_t previous = DeskLogic::beijingEpoch(2026, 11, 14, 9, 59) + 59;
    std::ofstream evidence(out + "/overshoot-verification.json"); evidence << "[\n";
    for (unsigned mode = 0; mode < 5; ++mode) {
        Config c = quiet(); c.weight = 900; c.opacity = 100; c.spacing = -18;
        c.seconds = mode == 1 || mode == 3; persist(c);
        HostEpoch = previous + 1; clock(names[mode]);
        std::vector<lv_obj_t *> digits; collectDigits(root, digits);
        const auto target = alphaPixels(digits.back());
        HostEpoch = previous; clock(names[mode]); digits.clear(); collectDigits(root, digits);
        ++HostEpoch; DashboardClockPage::refresh();
        int bestShift = 0; unsigned captureAt = 0;
        for (unsigned elapsed = 20; elapsed <= 1600; elapsed += 20) {
            advance(20);
            if (!DashboardClockPage::digitOffset(digits.size() - 1)) continue;
            const int shift = completeTargetShift(target, digits.back());
            if (shift < bestShift) { bestShift = shift; captureAt = elapsed; }
        }
        if (!captureAt) std::cerr << "No complete overshooting target: " << ids[mode] << "\n";
        assert(captureAt && bestShift < 0);
        // Replay at the largest observed displacement for a reviewable frame.
        HostEpoch = previous; clock(names[mode]); digits.clear(); collectDigits(root, digits);
        ++HostEpoch; DashboardClockPage::refresh(); advance(captureAt);
        assert(completeTargetShift(target, digits.back()) == bestShift);
        save(std::string("overshoot-") + ids[mode]);
        if (mode) evidence << ",\n";
        evidence << "{\"form\":\"" << ids[mode] << "\",\"elapsed_ms\":" << captureAt
                 << ",\"target_shift_px\":" << bestShift << ",\"complete_target_raster\":true}";
        ++overshootScenes; ++checks;
    }
    evidence << "\n]\n";
}
static void weatherFixture(int code = 2) {
    settings.city = "昆明"; weather = Weather{}; weather.valid = true;
    weather.query = "昆明"; weather.fetchedAt = HostEpoch; weather.code = code; weather.day = true;
    weather.temperature = 18; weather.condition = code == 65 ? "大雨" : code == 75 ? "大雪" : code == 95 ? "雷雨" : "多云";
    weather.feels = 17; weather.low = 13; weather.high = 22; weather.humidity = 65;
    weather.uv = 4; weather.aqi = 38; weather.aqiCategory = "优";
}

static void checkColonClearance(std::ofstream &evidence, const char *form, const char *phase,
                                int spacing, int elapsed = 0, int anglePattern = 0) {
    std::vector<lv_obj_t *> digits; collectDigits(root, digits); assert(!digits.empty());
    auto *stage = lv_obj_get_parent(digits.front());
    auto *view = lv_obj_get_parent(stage);
    struct HiddenState { lv_obj_t *object; bool hidden; };
    std::vector<HiddenState> hidden;
    auto hide = [&](lv_obj_t *object) {
        hidden.push_back({object, lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)});
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    };
    // Keep the real stage draw callback: it draws the production colons after
    // its children. Hide all other view content and all digit image children,
    // then capture only those actual colon pixels. No layout formulas or
    // separator coordinates are used by this oracle.
    for (unsigned i = 0; i < lv_obj_get_child_cnt(view); ++i) {
        auto *child = lv_obj_get_child(view, i); if (child != stage) hide(child);
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(stage); ++i) hide(lv_obj_get_child(stage, i));
    lv_obj_update_layout(root); lv_obj_invalidate(root); lv_refr_now(nullptr);
    std::vector<uint8_t> colonMask(320 * 240), digitMask(320 * 240);
    unsigned colonPixels = 0, digitPixels = 0;
    for (unsigned i = 0; i < colonMask.size(); ++i) {
        colonMask[i] = pixels[i].full != 0; colonPixels += colonMask[i];
    }
    for (const auto &state : hidden)
        if (!state.hidden) lv_obj_clear_flag(state.object, LV_OBJ_FLAG_HIDDEN);

    // Render each current digit descriptor, including a rolling/overshooting
    // raster, through LVGL at its true screen position with identical pivot,
    // angle, zoom, antialiasing and opacity. An opaque probe hides the original
    // stage, so the result cannot contain colons or other clock decoration.
    auto *probe = lv_obj_create(root); lv_obj_remove_style_all(probe);
    lv_obj_set_pos(probe, 0, 0); lv_obj_set_size(probe, 320, 240);
    lv_obj_set_style_bg_color(probe, lv_color_black(), 0); lv_obj_set_style_bg_opa(probe, 255, 0);
    lv_obj_clear_flag(probe, LV_OBJ_FLAG_SCROLLABLE);
    for (auto *digit : digits) {
        lv_area_t position; lv_obj_get_coords(digit, &position);
        lv_point_t pivot; lv_img_get_pivot(digit, &pivot);
        auto *reference = lv_img_create(probe);
        lv_img_set_src(reference, lv_img_get_src(digit));
        lv_img_set_pivot(reference, pivot.x, pivot.y);
        lv_img_set_zoom(reference, lv_img_get_zoom(digit));
        lv_img_set_angle(reference, lv_img_get_angle(digit));
        lv_img_set_antialias(reference, lv_img_get_antialias(digit));
        lv_obj_set_style_img_opa(reference, lv_obj_get_style_img_opa(digit, 0), 0);
        lv_obj_set_style_opa(reference, lv_obj_get_style_opa(digit, 0), 0);
        lv_obj_set_pos(reference, position.x1, position.y1);
    }
    lv_obj_update_layout(root); lv_obj_invalidate(root); lv_refr_now(nullptr);
    for (unsigned i = 0; i < digitMask.size(); ++i) {
        digitMask[i] = pixels[i].full != 0; digitPixels += digitMask[i];
    }
    lv_obj_del(probe); lv_obj_invalidate(root); lv_refr_now(nullptr);
    assert(colonPixels && digitPixels);

    // Exact eight-neighbour distance transform, independent of digit bounding
    // boxes and the production grouping calculation. Count every non-black
    // RGB565 pixel, including antialiasing and faint seconds during a fade.
    std::vector<int> distance(digitMask.size(), 1000);
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) {
        const int i = y * 320 + x;
        if (digitMask[i]) { distance[i] = 0; continue; }
        if (x) distance[i] = std::min(distance[i], distance[i - 1] + 1);
        if (y) {
            distance[i] = std::min(distance[i], distance[i - 320] + 1);
            if (x) distance[i] = std::min(distance[i], distance[i - 321] + 1);
            if (x < 319) distance[i] = std::min(distance[i], distance[i - 319] + 1);
        }
    }
    for (int y = 239; y >= 0; --y) for (int x = 319; x >= 0; --x) {
        const int i = y * 320 + x;
        if (x < 319) distance[i] = std::min(distance[i], distance[i + 1] + 1);
        if (y < 239) {
            distance[i] = std::min(distance[i], distance[i + 320] + 1);
            if (x) distance[i] = std::min(distance[i], distance[i + 319] + 1);
            if (x < 319) distance[i] = std::min(distance[i], distance[i + 321] + 1);
        }
    }
    int minimum = 1000, closest = -1;
    for (unsigned i = 0; i < colonMask.size(); ++i) if (colonMask[i] && distance[i] < minimum) {
        minimum = distance[i]; closest = i;
    }
    const int clearPixels = minimum - 1;
    if (clearPixels < 2) {
        std::cerr << "Colon/digit collision: " << form << " phase " << phase << " spacing " << spacing
                  << " elapsed " << elapsed << " pattern " << anglePattern << " clearance " << clearPixels
                  << " at [" << closest % 320 << "," << closest / 320 << "]\n";
        save(std::string("colon-FAIL-") + form + "-" + phase);
        assert(false);
    }
    if (colonClearanceScenes) evidence << ",\n";
    evidence << "{\"form\":\"" << form << "\",\"phase\":\"" << phase << "\",\"spacing\":" << spacing
             << ",\"elapsed_ms\":" << elapsed << ",\"angle_pattern\":" << anglePattern
             << ",\"seconds_visible\":" << (DashboardClockPage::status().seconds ? "true" : "false")
             << ",\"visible_digit_count\":" << digits.size()
             << ",\"colon_pixels\":" << colonPixels << ",\"digit_pixels\":" << digitPixels
             << ",\"minimum_pixel_distance\":" << minimum << ",\"clear_pixels\":" << clearPixels << "}";
    colonMinimumClearPixels = std::min(colonMinimumClearPixels, clearPixels);
    ++colonClearanceScenes; ++checks;
}

static void colonClearanceTests() {
    const char *names[] = {"天气大字钟", "天气大字钟", "纯时钟", "纯时钟"};
    const char *ids[] = {"dashboard-hhmm", "dashboard-hhmmss", "minimal-hhmm", "minimal-hhmmss"};
    std::ofstream evidence(out + "/colon-clearance-verification.json"); evidence << "[\n";
    weatherFixture();
    for (unsigned mode = 0; mode < 4; ++mode) for (int spacing : {-18, -8}) {
        Config c = quiet(); c.weight = 900; c.opacity = 100; c.spacing = spacing;
        c.color = 0x659fe8; c.tilt = true; c.seconds = mode == 1 || mode == 3;
        persist(c); HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 8, 28) + 8; clock(names[mode]);
        std::vector<lv_obj_t *> digits; collectDigits(root, digits);
        for (int pattern : {0, -1, 1}) {
            for (unsigned i = 0; i < digits.size(); ++i)
                lv_img_set_angle(digits[i], pattern * (i % 2 ? -60 : 60));
            lv_obj_update_layout(root); lv_obj_invalidate(root); lv_refr_now(nullptr);
            checkColonClearance(evidence, ids[mode], pattern ? "opposing-tilt" : "still", spacing, 0, pattern);
            if (spacing == -18 && pattern == 1) save(std::string("colon-") + ids[mode] + "-tilt");
        }
        // Disabling tilt removes the rotation reserve from the real layout.
        // Check this narrower separator band independently at both spacings.
        c.tilt = false; persist(c); clock(names[mode]);
        checkColonClearance(evidence, ids[mode], "tilt-disabled", spacing);
        c.tilt = true;

        // A leading zero is hidden in 12h mode. Check both the one-digit first
        // group and every rolling/reflow sample as it grows to 10:00:00.
        c.hour24 = false; persist(c);
        HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 9, 59) + 59; clock(names[mode]);
        digits.clear(); collectDigits(root, digits);
        assert(digits.size() == (c.seconds ? 5u : 3u));
        checkColonClearance(evidence, ids[mode], "12hour-hidden", spacing);
        ++HostEpoch; DashboardClockPage::refresh();
        for (int elapsed = 0; elapsed <= 1400; elapsed += 40) {
            if (elapsed) advance(40);
            checkColonClearance(evidence, ids[mode], "12hour-roll", spacing, elapsed);
        }

        // Drive actual pointer release, then sample every production animation
        // tick in both directions. Starting at t=0 covers the disappearing
        // second colon and the emerging seconds before their first layout step.
        for (int direction = 0; direction < 2; ++direction) {
            const bool before = DashboardClockPage::status().seconds;
            sample(160, 120, true); sample(160, 120, false);
            assert(DashboardClockPage::status().seconds != before);
            for (int elapsed = 0; elapsed <= 600; elapsed += 20) {
                if (elapsed) advance(20);
                checkColonClearance(evidence, ids[mode], "seconds-transition", spacing, direction * 1000 + elapsed);
                if (spacing == -18 && direction == 0 && elapsed == 240)
                    save(std::string("colon-") + ids[mode] + "-seconds-transition");
            }
        }
    }
    Config c = quiet(); c.weight = 900; c.opacity = 100; persist(c);
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 8, 28); clock("紧凑看板");
    checkColonClearance(evidence, "compact-hhmm", "compact", c.spacing);
    evidence << "\n]\n";
}

static void referenceLayoutTests() {
    // Review the same three layouts/times supplied by the user, through the
    // production widgets. Weather remains a known fixture for reproducibility.
    Config c = quiet(); c.color = 0x659fe8; c.weight = 700;
    c.seconds = c.tilt = true; persist(c); weatherFixture(0);
    weather.condition = "晴朗";
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 13, 16, 16) + 54;
    clock("天气大字钟"); timeIs("16:16:54", true);
    assert(containsText(root, "星期五") && containsText(root, "初五"));
    save("reference-dashboard");

    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 13, 16, 17);
    clock("紧凑看板"); timeIs("16:17", false);
    assert(!DashboardClockPage::status().tilt && containsText(root, "初五"));
    save("reference-compact");

    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 13, 16, 18) + 6;
    clock("纯时钟"); timeIs("16:18:06", true);
    assert(!containsText(root, "晴朗") && !containsText(root, "星期五"));
    save("reference-minimal");
}

static void weatherBoundaryTests() {
    Config c = quiet(); persist(c);
    for (int temperature : {-12, -99}) {
        weatherFixture(45); settings.city = "内蒙古呼和浩特市";
        weather.condition = "强沙尘暴"; weather.temperature = temperature;
        weather.low = -99; weather.high = -12; weather.feels = -99;
        weather.humidity = 100; weather.aqi = 500; weather.uv = 11;
        clock("天气大字钟");
        assert(containsText(root, "强沙尘暴") && containsText(root, "呼和浩特") &&
               containsText(root, "100%") && containsText(root, "500") && containsText(root, "11"));
        save(std::string("weather-boundary-negative") + std::to_string(-temperature));
        advance(1600); save(std::string("weather-boundary-scroll") + std::to_string(-temperature));
        ++checks;
    }
    clock("紧凑看板");
    assert(containsText(root, "100%") && containsText(root, "-99"));
    save("weather-boundary-compact"); ++checks;
    weather = Weather{}; settings.city = "";
    clock("天气大字钟");
    assert(containsText(root, "天气待更新") && containsText(root, "未设置地区"));
    save("weather-boundary-unavailable"); ++checks;
}

static void compactInformationTests() {
    std::ofstream evidence(out + "/compact-information-verification.json");evidence << '[';
    auto inspect = [&](const char *id) {
        lv_obj_update_layout(root);checkLabels(root);
        std::vector<lv_obj_t *> labels;
        auto visit = [&](auto &self,lv_obj_t *object)->void {
            if(lv_obj_has_flag(object,LV_OBJ_FLAG_HIDDEN))return;
            if(lv_obj_check_type(object,&lv_label_class)&&lv_label_get_text(object)[0]){
                lv_area_t area;lv_obj_get_coords(object,&area);
                if(area.x1>=192){
                    assert(area.x1>=200&&area.x2<=311&&area.y1>=8&&area.y2<=231);
                    auto mode=lv_label_get_long_mode(object);
                    assert(mode!=LV_LABEL_LONG_SCROLL&&mode!=LV_LABEL_LONG_SCROLL_CIRCULAR);
                    labels.push_back(object);
                }
            }
            for(unsigned i=0;i<lv_obj_get_child_cnt(object);++i)self(self,lv_obj_get_child(object,i));
        };visit(visit,root);
        assert(labels.size()>=6);
        for(unsigned a=0;a<labels.size();++a)for(unsigned b=a+1;b<labels.size();++b){
            lv_area_t aa,bb;lv_obj_get_coords(labels[a],&aa);lv_obj_get_coords(labels[b],&bb);
            if(aa.x1<=bb.x2&&bb.x1<=aa.x2&&aa.y1<=bb.y2&&bb.y1<=aa.y2){
                std::cerr<<"Compact information overlap: "<<lv_label_get_text(labels[a])<<" / "<<lv_label_get_text(labels[b])<<'\n';assert(false);
            }
        }
        if(compactInfoScenes)evidence<<',';
        evidence<<"{\"scene\":\""<<id<<"\",\"labels\":"<<labels.size()<<",\"right_column_inset\":8,\"no_overlap\":true,\"no_scrolling\":true}";
        ++compactInfoScenes;++checks;save(std::string("compact-info-")+id);
    };
    Config c=quiet();persist(c);weatherFixture();
    HostEpoch=DeskLogic::beijingEpoch(2026,11,13,19,51);clock("紧凑看板");inspect("normal");
    weatherFixture(95);weather.condition="雷阵雨伴有冰雹";weather.temperature=-99;weather.humidity=100;
    settings.city="内蒙古自治区呼和浩特市";
    unsigned longest=dashboard_clock_year_offsets[126];
    for(unsigned i=longest;i<dashboard_clock_year_offsets[127];++i)
        if(strlen(dashboard_clock_event_names[dashboard_clock_calendar_events[i].label])>
           strlen(dashboard_clock_event_names[dashboard_clock_calendar_events[longest].label]))longest=i;
    const auto event=dashboard_clock_calendar_events[longest];
    HostEpoch=DeskLogic::beijingEpoch(2026,event.date/100,event.date%100,19,51);clock("紧凑看板");
    assert(containsText(root,"全国中小学生安全教育日")&&containsText(root,"雷阵雨伴有冰雹")&&
           containsText(root,"内蒙古自治区呼和浩特市")&&containsText(root,"-99")&&containsText(root,"100%"));
    inspect("long-festival-weather");
    weatherFixture();HostEpoch=DeskLogic::beijingEpoch(2025,8,8,19,51);clock("紧凑看板");inspect("lunar-month");
    c.lunar=c.festival=false;persist(c);clock("紧凑看板");
    assert(!containsText(root,"丙午")&&!containsText(root,"初五"));inspect("calendar-notes-hidden");
    c=quiet();persist(c);weather=Weather{};settings.city="";HostEpoch=DeskLogic::beijingEpoch(2026,11,13,19,51);
    clock("紧凑看板");assert(containsText(root,"待更新")&&containsText(root,"等待天气数据")&&!containsText(root,"--%"));inspect("unavailable");
    evidence<<"]\n";
}

static lv_obj_t *slider(unsigned nth) {
    auto search = [&](auto &self, lv_obj_t *p) -> lv_obj_t * {
        if (lv_obj_check_type(p, &lv_slider_class)) { if (!nth) return p; --nth; }
        for (unsigned i = 0; i < lv_obj_get_child_cnt(p); ++i)
            if (auto *found = self(self, lv_obj_get_child(p, i))) return found;
        return nullptr;
    };
    auto *result = search(search, root); assert(result); return result;
}
static int dragSlider(unsigned nth, int value) {
    auto *s = slider(nth); lv_area_t a; lv_obj_get_coords(s, &a);
    const int low = lv_slider_get_min_value(s), high = lv_slider_get_max_value(s);
    const int y = (a.y1 + a.y2) / 2;
    const int before = lv_slider_get_value(s);
    const int width = lv_obj_get_width(s);
    const int x0 = a.x1 + width * (before - low) / (high - low);
    const int x1 = a.x1 + width * (value - low) / (high - low);
    sample(x0, y, true);
    assert(DashboardClockSettings::inputActive() && GiftUI::inputActive());
    for (int i = 1; i <= 8; ++i) sample(x0 + (x1 - x0) * i / 8, y, true);
    sample(x1, y, false); advance(40);
    assert(!DashboardClockSettings::inputActive() && !GiftUI::inputActive());
    return lv_slider_get_value(s);
}

static Config seedConfig() {
    Config c; c.color = 0x659fe8; c.weight = 300; c.opacity = 77; c.spacing = -12;
    c.seconds = true; c.hour24 = false; c.tilt = false; c.lunar = false; c.festival = false;
    c.rain = c.snow = c.thunder = false; return c;
}
static void restartMode(const std::string &mode) {
    const Config expected = seedConfig(); String error;
    if (mode == "seed") {
        persist(expected); JsonDocument p; p["clockMode"]["preset"] = "compact";
        assert(GiftData::patch(p.as<JsonVariantConst>(), error));
    } else {
        assert(equalConfig(DashboardClockConfig::load(), expected));
        assert(GiftData::get("clockMode")["preset"] == "compact");
        if (mode == "failed-write" || mode == "partial-write") {
            Config changed = expected; changed.color = 0xef7993; changed.seconds = false;
            LittleFS.writeLimit = mode == "failed-write" ? 0 : 7;
            assert(!DashboardClockConfig::save(changed, error)); LittleFS.resetFaults();
            assert(equalConfig(DashboardClockConfig::load(), expected));
        } else if (mode == "invalid-write") {
            for (int invalid : {99, 150, 1000}) {
                Config changed = expected; changed.weight = invalid;
                assert(!DashboardClockConfig::save(changed, error));
            }
            Config changed = expected; changed.opacity = 0; assert(!DashboardClockConfig::save(changed, error));
            changed = expected; changed.spacing = -7; assert(!DashboardClockConfig::save(changed, error));
            changed = expected; changed.color = 0x1000000; assert(!DashboardClockConfig::save(changed, error));
            assert(equalConfig(DashboardClockConfig::load(), expected));
        } else assert(mode == "restore");
    }
    std::cout << "PASS: separate process " << mode << "\n";
}

static void settingTests() {
    Config c = quiet(); persist(c); picker(); click("钟面设置");
    assert(DashboardClockSettings::active() && label("配色")); save("settings-colors");
    const char *names[] = {"白色", "蓝色", "粉色", "橙色", "绿色", "青色"};
    const uint32_t colors[] = {0xffffff, 0x659fe8, 0xef7993, 0xf2a666, 0x8cdb92, 0x68cdd7};
    for (unsigned i = 0; i < 6; ++i) {
        click(names[i]); click("保存"); assert(label("已保存"));
        assert(DashboardClockConfig::load().color == colors[i]); ++checks;
    }
    click("自定义颜色"); save("settings-custom-color");
    assert(dragSlider(0, 0) == 0); assert(dragSlider(1, 255) == 255); assert(dragSlider(2, 0) == 0);
    click("使用此颜色"); assert(DashboardClockConfig::load().color == 0x68cdd7);
    LittleFS.writeLimit = 0; click("保存"); LittleFS.resetFaults();
    assert(label("保存失败，请重试") && DashboardClockConfig::load().color == 0x68cdd7);
    save("settings-save-failed"); click("保存"); assert(DashboardClockConfig::load().color == 0x00ff00);
    click("字体"); save("settings-fonts");
    const int weight = dragSlider(0, 9) * 100;
    const int opacity = dragSlider(1, 10), spacing = dragSlider(2, -8);
    assert(weight == 900 && opacity == 10 && spacing == -8); click("保存");
    c = DashboardClockConfig::load(); assert(c.weight == weight && c.opacity == opacity && c.spacing == spacing);
    click("时间"); save("settings-time");
    click("24 小时制"); click("显示秒钟"); click("数字轻微倾斜"); click("保存");
    c = DashboardClockConfig::load(); assert(!c.hour24 && c.seconds && c.tilt);
    click("天气"); save("settings-weather");
    click("显示农历"); click("显示节日"); click("下雨动效"); click("下雪动效"); click("雷电动效"); click("保存");
    c = DashboardClockConfig::load(); assert(!c.lunar && !c.festival && c.rain && c.snow && c.thunder);
    // Leaving a modified draft, including the custom-colour subpage, cannot
    // silently persist it; top-level save is the single commit point.
    click("配色"); click("自定义颜色"); dragSlider(0, 255);
    click("取消"); click("取消"); assert(!DashboardClockSettings::active());
    assert(equalConfig(DashboardClockConfig::load(), c));
    click("钟面设置"); click("粉色"); click("取消");
    assert(equalConfig(DashboardClockConfig::load(), c));
    checks += 8;
}


static void motionPerformanceTests() {
    std::ofstream evidence(out + "/motion-performance-verification.json");
    evidence << "{\"layout_transitions\":[";
    bool first = true;
    auto verify = [](bool value) { if(!value)std::cerr << "Motion/performance failure at check " << motionPerformanceChecks+1 << "\n"; assert(value); ++checks; ++motionPerformanceChecks; };
    for (const char *name : {"天气大字钟", "纯时钟"}) {
        Config c = quiet(); c.seconds = false; persist(c); weather = Weather{};
        HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 8, 28) + 12; clock(name);
        for (unsigned direction = 0; direction < 2; ++direction) {
            std::vector<lv_obj_t *> digits; collectDigits(root, digits);
            auto *digit = digits.front(); const int before = lv_img_get_zoom(digit);
            const uint32_t clickedAt = lv_tick_get();
            sample(160, 120, true); sample(160, 120, false);
            const int release = lv_img_get_zoom(digit);
            verify(DashboardClockPage::debugLayoutMoving());
            verify(std::abs(release - before) < 24);
            advance(240); const int middle = lv_img_get_zoom(digit);
            verify(DashboardClockPage::debugLayoutMoving());
            advance(300); const int end = lv_img_get_zoom(digit);
            verify(!DashboardClockPage::debugLayoutMoving());
            verify(before != end && middle != before && middle != end);
            verify(std::min(before, end) < middle && middle < std::max(before, end));
            verify(DashboardClockPage::status().seconds == (direction == 0));
            if (!first) evidence << ','; first = false;
            evidence << "{\"form\":\"" << (!strcmp(name,"纯时钟")?"minimal":"dashboard")
                     << "\",\"show_seconds\":" << (direction == 0 ? "true" : "false")
                     << ",\"before_zoom\":" << before << ",\"release_zoom\":" << release
                     << ",\"middle_zoom\":" << middle << ",\"end_zoom\":" << end
                     << ",\"sample_end_ms\":" << lv_tick_get()-clickedAt << '}';
            save(std::string("motion-layout-") + (!strcmp(name,"纯时钟")?"minimal":"dashboard") + (direction?"-hhmm":"-hhmmss"));
        }
    }
    evidence << "],\"hhmm_second_tilt\":["; first = true;
    for (const char *name : {"天气大字钟", "纯时钟"}) {
        Config c = quiet(); c.seconds = false; c.tilt = true; persist(c);
        weather = Weather{}; HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 8, 28) + 12; clock(name);
        const uint32_t hidden4 = DashboardClockPage::debugPaintCount(4), hidden5 = DashboardClockPage::debugPaintCount(5);
        const auto startingPixels = clockPixels();
        for (unsigned second = 0; second < 4; ++second) {
            int previous[4]; for (unsigned digit = 0; digit < 4; ++digit) previous[digit] = DashboardClockPage::digitAngle(digit);
            ++HostEpoch; DashboardClockPage::refresh(); advance(650);
            unsigned changed = 0;
            for (unsigned digit = 0; digit < 4; ++digit) {
                changed += previous[digit] != DashboardClockPage::digitAngle(digit);
                verify(std::abs(DashboardClockPage::digitAngle(digit)) <= 60);
            }
            verify(changed > 0); verify(!strcmp(DashboardClockPage::status().time, "08:28"));
            verify(DashboardClockPage::debugPaintCount(4) == hidden4 && DashboardClockPage::debugPaintCount(5) == hidden5);
            if (!first) evidence << ','; first = false;
            evidence << "{\"form\":\"" << (!strcmp(name,"纯时钟")?"minimal":"dashboard") << "\",\"second\":" << second+1
                     << ",\"changed_angles\":" << changed << ",\"hidden_seconds_repainted\":false}";
            advance(350);
        }
        verify(!same(startingPixels, clockPixels()));
    }
    evidence << "],\"static_refreshes\":["; first = true;
    for (const char *name : {"天气大字钟", "纯时钟", "紧凑看板"}) {
        Config c = quiet(); c.seconds = false; c.tilt = !strcmp(name,"紧凑看板"); persist(c);
        weatherFixture(0); weather.day=false; HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 8, 28) + 12; clock(name); advance(1000);
        const unsigned beforeFlush = flushCalls; const auto before = clockPixels();
        uint32_t paints[6];for(unsigned digit=0;digit<6;++digit)paints[digit]=DashboardClockPage::debugPaintCount(digit);
        for (unsigned second = 0; second < 4; ++second) {
            ++HostEpoch; DashboardClockPage::refresh(); advance(1000);
            for(unsigned digit=0;digit<6;++digit){
                verify(DashboardClockPage::digitAngle(digit)==0);
                verify(DashboardClockPage::debugPaintCount(digit)==paints[digit]);
            }
        }
        verify(same(before,clockPixels())); verify(flushCalls==beforeFlush);
        verify(DashboardClockPage::debugIconInvalidations()==0);
        if (!first) evidence << ',';first=false;
        evidence << "{\"form\":\"" << (!strcmp(name,"纯时钟")?"minimal":!strcmp(name,"紧凑看板")?"compact":"dashboard")
                 << "\",\"seconds_observed\":4,\"extra_flushes\":" << flushCalls-beforeFlush << ",\"digit_repaints\":0}";
    }
    // Unknown weather, moon and fog have no moving icon parts. Daytime sun and
    // clouds retain their actual icon animation; updated weather is not frozen.
    evidence << "],\"static_icons\":["; first=true;
    for (int code : {-1,0,45}) {
        Config c=quiet();persist(c);weatherFixture(code);weather.day=false;if(code<0)weather=Weather{};
        clock("天气大字钟");advance(1000);const unsigned beforeFlush=flushCalls;
        ++HostEpoch;DashboardClockPage::refresh();advance(1000);
        verify(DashboardClockPage::debugIconInvalidations()==0);if(code>=0)verify(flushCalls==beforeFlush);
        if(!first)evidence<<',';first=false;
        evidence<<"{\"weather_code\":"<<code<<",\"icon_invalidations\":0,\"extra_flushes\":"<<flushCalls-beforeFlush<<'}';
    }
    for(int code:{0,2}) {
        weatherFixture(code);clock("天气大字钟");advance(300);
        verify(DashboardClockPage::debugIconInvalidations()>=2);
    }
    evidence << "],\"passed\":true,\"checks\":" << motionPerformanceChecks << "}\n";
}

static void clockTests(unsigned baseTimers) {
    Config c = quiet(); persist(c); weatherFixture();
    picker(); assert(label("天气大字钟") && label("纯时钟") && label("紧凑看板"));
    save("dashboard-picker"); click("天气大字钟");
    assert(!strcmp(DashboardClockPage::status().preset, "dashboard"));
    assert(DashboardClockPage::status().rasterReady && !DashboardClockPage::status().movingDigits);
    timeIs("09:59", false); save("dashboard-hhmm");
    tap(160, 120); advance(550);
    assert(DashboardClockConfig::load().seconds); timeIs("09:59:59", true); save("dashboard-hhmmss");
    auto before = clockPixels(); ++HostEpoch; DashboardClockPage::refresh();
    timeIs("10:00:00", true); assert(DashboardClockPage::status().movingDigits == 6);
    save("dashboard-roll-000");
    for (int i = 0; i < 20; ++i) DashboardClockPage::refresh();
    assert(DashboardClockPage::status().movingDigits == 6);
    advance(200); save("dashboard-roll-200"); assert(!same(before, clockPixels()));
    advance(300); save("dashboard-roll-500");
    advance(400); save("dashboard-roll-900");
    advance(450); save("dashboard-roll-1350"); assert(!DashboardClockPage::status().movingDigits);
    const int targets[] = {1, 0, 0, 0, 0, 0};
    for (unsigned i = 0; i < 6; ++i) assert(DashboardClockPage::digitShown(i) == targets[i]);
    // The settled animation must draw the same digits as entering at that time.
    auto settled = clockPixels(); clock("天气大字钟"); assert(same(settled, clockPixels()));
    // Large forward corrections and backward NTP steps snap to current time.
    HostEpoch += 617; DashboardClockPage::refresh(); timeIs("10:10:17", true);
    assert(!DashboardClockPage::status().movingDigits);
    HostEpoch -= 3600; DashboardClockPage::refresh(); timeIs("09:10:17", true);
    assert(!DashboardClockPage::status().movingDigits);
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 13, 23, 59) + 59; DashboardClockPage::refresh();
    ++HostEpoch; DashboardClockPage::refresh(); advance(1400); timeIs("00:00:00", true); save("dashboard-midnight");
    HostEpoch = 0; DashboardClockPage::refresh(); timeIs("--:--:--", true);
    assert(!DashboardClockPage::status().movingDigits); save("dashboard-no-time");
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 8, 28); DashboardClockPage::refresh();
    timeIs("08:28:00", true); assert(!DashboardClockPage::status().movingDigits); save("dashboard-restored");
    clock("纯时钟"); timeIs("08:28:00", true); save("minimal-hhmmss");
    tap(160, 120); advance(550); timeIs("08:28", false); save("minimal-hhmm"); exportContainerBounds("minimal-container-bounds");
    clock("紧凑看板"); timeIs("08:28", false); assert(!DashboardClockPage::status().tilt); save("compact-hhmm");
    tap(100, 120); advance(600); timeIs("08:28", false);
    c = quiet(); c.seconds = true; c.hour24 = false; persist(c);
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 0, 4); clock("纯时钟"); timeIs("12:04:00", true); save("minimal-12hour-midnight");
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 13, 4); DashboardClockPage::refresh(); timeIs("01:04:00", true);
    // The hour tens appears as a fresh current digit at 9:59 -> 10:00 in 12h
    // mode; an initially hidden zero must never flash before the new one.
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 9, 59) + 59; clock("纯时钟");
    ++HostEpoch; DashboardClockPage::refresh(); timeIs("10:00:00", true);
    assert(DashboardClockPage::digitShown(0) == 1 && DashboardClockPage::digitTarget(0) == 1);
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 12, 59) + 59; DashboardClockPage::refresh();
    ++HostEpoch; DashboardClockPage::refresh(); timeIs("01:00:00", true);
    assert(DashboardClockPage::digitShown(0) == 0 && DashboardClockPage::digitTarget(0) == 0);
    checks += 2;
    // Lunar and festival text must remain legible in both information layouts.
    // These solar dates were checked against the upstream lunar calendar table.
    c = quiet(); persist(c);
    HostEpoch = DeskLogic::beijingEpoch(2026, 2, 17, 10, 24); clock("天气大字钟");
    assert(containsText(root, "春节") && containsText(root, "初一") && containsText(root, "正月")); save("dashboard-spring-festival");
    clock("紧凑看板"); assert(containsText(root, "春节") && containsText(root, "正月初一")); save("compact-spring-festival");
    HostEpoch = DeskLogic::beijingEpoch(2026, 8, 19, 10, 24); clock("天气大字钟");
    assert(containsText(root, "七夕")); save("dashboard-qixi");
    HostEpoch = DeskLogic::beijingEpoch(2026, 12, 22, 10, 24); clock("天气大字钟");
    assert(containsText(root, "冬至")); save("dashboard-winter-solstice");
    HostEpoch = DeskLogic::beijingEpoch(2026, 2, 4, 10, 24); clock("天气大字钟");
    assert(containsText(root, "立春") && containsText(root, "丙午(蛇)年")); save("dashboard-lichun");
    c.lunar = c.festival = false; persist(c);
    HostEpoch = DeskLogic::beijingEpoch(2026, 2, 17, 10, 24); clock("天气大字钟");
    assert(!containsText(root, "春节") && !containsText(root, "正月初一") && !containsText(root, "丙午")); save("dashboard-calendar-hidden");
    checks += 6;
    // Every font weight and preset color uses production draw paths.
    c = quiet(); c.color = 0x659fe8;
    for (int weight = 100; weight <= 900; weight += 100) {
        c.weight = weight; persist(c); clock("天气大字钟");
        assert(DashboardClockPage::status().rasterReady); save("dashboard-weight-" + std::to_string(weight));
    }
    const uint32_t colors[] = {0xffffff, 0x659fe8, 0xef7993, 0xf2a666, 0x8cdb92, 0x68cdd7};
    c.weight = 700;
    for (unsigned i = 0; i < 6; ++i) { c.color = colors[i]; persist(c); clock("天气大字钟"); save("dashboard-color-" + std::to_string(i)); }
    // Export a continuous sequence from the same production draw/timer pipeline
    // for visual review. Its 40 ms sampling is a host preview, not device FPS.
    c = quiet(); c.color = 0x659fe8; c.tilt = true; persist(c);
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 14, 9, 59) + 59; clock("天气大字钟");
    for (unsigned f = 0; f < 60; ++f) {
        if (f == 10) { ++HostEpoch; DashboardClockPage::refresh(); }
        char name[32]; snprintf(name, sizeof(name), "motion-%03u", f); save(name); advance(40);
    }
    // Tilt remains bounded through repeated minute/second triggers.
    c = quiet(); c.tilt = true; c.seconds = true; c.color = 0x659fe8; persist(c); clock("天气大字钟");
    for (unsigned second = 0; second < 8; ++second) {
        ++HostEpoch; DashboardClockPage::refresh(); advance(650);
        for (unsigned i = 0; i < 6; ++i) assert(std::abs(DashboardClockPage::digitAngle(i)) <= 60);
    }
    save("dashboard-tilt");
    // Animated weather is exercised with actual fixture codes and switches.
    c = quiet(); c.rain = c.snow = c.thunder = true; persist(c);
    for (int code : {65, 75, 95}) {
        weatherFixture(code); clock("天气大字钟"); advance(500);
        const std::string name = code == 65 ? "rain" : code == 75 ? "snow" : "thunder";
        save("dashboard-weather-" + name); auto weatherBefore = clockPixels();
        advance(240); save("dashboard-weather-" + name + "-next");
        if (code != 95) assert(!same(weatherBefore, clockPixels()));
    }
    weather = Weather{}; clock("天气大字钟"); save("dashboard-weather-unavailable");
    DashboardClockPage::debugFailRaster(true); clock("天气大字钟");
    assert(!DashboardClockPage::status().rasterReady); save("dashboard-allocation-fallback");
    ++HostEpoch; DashboardClockPage::refresh();
    assert(!DashboardClockPage::status().movingDigits);
    DashboardClockPage::debugFailRaster(false); clock("天气大字钟");
    assert(DashboardClockPage::status().rasterReady); ++checks;
    // Exiting during delayed rolls/rotation/particles must release all timers.
    for (unsigned i = 0; i < 36; ++i) {
        weatherFixture(i % 3 == 0 ? 65 : i % 3 == 1 ? 75 : 95);
        clock(i % 3 == 0 ? "天气大字钟" : i % 3 == 1 ? "纯时钟" : "紧凑看板");
        ++HostEpoch; DashboardClockPage::refresh(); advance(100); clean();
        assert(timers() == baseTimers); advance(500);
    }
    picker(); click("钟面设置"); clean(); assert(timers() == baseTimers);
    checks += 24;
}

int main(int argc, char **argv) {
    std::string mode, store;
    out = "build/dashboard-clock-20261113/host";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
        else if (!strcmp(argv[i], "--store") && i + 1 < argc) store = argv[++i];
        else out = argv[i];
    }
    std::filesystem::create_directories(out);
    LittleFS.directory = store.empty() ? out + "/store" : store;
    std::filesystem::create_directories(LittleFS.directory);
    lv_init(); static lv_disp_draw_buf_t buf; lv_disp_draw_buf_init(&buf, buffer, nullptr, 320 * 40);
    static lv_disp_drv_t drv; lv_disp_drv_init(&drv); drv.hor_res = 320; drv.ver_res = 240;
    drv.draw_buf = &buf; drv.flush_cb = flush; lv_disp_drv_register(&drv);
    root = lv_scr_act(); lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE); Ui::setRoot(root);
    static lv_indev_drv_t input; lv_indev_drv_init(&input); input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = readPointer; pointer = lv_indev_drv_register(&input);
    Ui::setActionHandler([](lv_event_t *e) {
        if ((uintptr_t)lv_event_get_user_data(e) == Ui::Back) GiftUI::back();
    });
    assert(GiftData::begin());
    if (!mode.empty()) { restartMode(mode); return 0; }
    const auto baseTimers = timers();
    clockTests(baseTimers); motionPerformanceTests(); settingTests(); clippingTests(); overshootTests(); colonClearanceTests(); referenceLayoutTests(); weatherBoundaryTests(); compactInformationTests(); clean(); assert(timers() == baseTimers);
    std::ofstream report(out + "/runner-verification.json");
    report << "{\"passed\":true,\"checks\":" << checks << ",\"frames\":" << frames
           << ",\"lifecycle_cycles\":36,\"clipping_scenes\":" << clippingScenes
           << ",\"clipping_glyphs\":" << clippingGlyphs << ",\"overshoot_scenes\":" << overshootScenes
           << ",\"compact_information_scenes\":" << compactInfoScenes
           << ",\"rounded_crown_glyphs\":" << roundedCrownGlyphs
           << ",\"colon_clearance_scenes\":" << colonClearanceScenes
           << ",\"colon_minimum_clear_pixels\":" << colonMinimumClearPixels
           << ",\"motion_performance_checks\":" << motionPerformanceChecks
           << ",\"pointer_driver\":true,\"firmware_flashed\":false}\n";
    std::cout << "PASS: all clock layouts, five time forms, rollovers, synchronization, invalid time, "
                 "colors, nine weights, weather, pointer settings, atomic save faults and lifecycle cleanup.\n";
}
