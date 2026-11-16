// Page 0 - 主页 / 桌面. Hand-written LVGL page for the locked home-v1 design.
//
// Design sources (locked): build/design/home/final-20261027-home-v1/
//   home-final-page.png (640x480 = 2x), 主页设计稿.md, 主页上半部分设计稿.md
// Spec: UI实现规格.md §3. Pixel values measured from the locked PNG win over the
// approximate spec text (see build/impl/团队约定.md §6 and the Lead decision).
//
// The home view is a cached layer on device: build() runs once, then
// HomePage::refresh(true) is called from the 1 Hz pass and must update every
// live value (clock/date/weather/week/calendar/battery/Wi-Fi) through the
// pointers cached here. build() therefore resets all caches first.
//
// All layout below is in screen coordinates (320x240). "card-local" comments are
// relative to the card they belong to.
#include "page_api.h"
#include "../ui_widgets.h"
#include "../app_state.h"
#include "../power_status.h"
#include "../clock_source.h"
#include "../text_util.h"
#include "../../desk_logic.h"
#include "../saved_design/saved_resources.h"
#include "../gift_assets/gift_fonts.h"
#include "assets/home_postcard_image.h"
#include "assets/font_clock56.h"
#include "assets/ui_status_icons.h"
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace HomePage {
namespace {

// ---- palette (sampled from home-final-page.png) --------------------------
constexpr uint32_t Cream = 0xE7EFD7;       // page background
constexpr uint32_t White = 0xFFFFFF;       // postcard / week card
constexpr uint32_t DotGreen = 0xD5DFBD;    // perforation dots
constexpr uint32_t ClockGreen = 0xACC976;  // square clock card
constexpr uint32_t ClockHour = 0x354F1D;   // 09 deep green
constexpr uint32_t ClockCold = 0x86A35C;   // "--" placeholder while NTP has no time
constexpr uint32_t ClockMin = 0xFFFBEA;    // 56 warm white
constexpr uint32_t WeatherBg = 0x486738;
constexpr uint32_t WeatherBorder = 0xE3E8B9;
constexpr uint32_t ArtShadow = 0x1E2A1E;   // text shadow on the weather artwork
constexpr uint32_t WeekYellow = 0xFFE49A;  // 3-day strip (as rendered in the PNG)
constexpr uint32_t WeekInk = 0x7A5B2E;     // weekday labels
constexpr uint32_t WeekTodayInk = 0xC9871F;
constexpr uint32_t WeekDate = 0x292929;
constexpr uint32_t TodayPill = 0xF2B84B;
constexpr uint32_t CalBg = 0x26352A;       // dark calendar card
constexpr uint32_t CalTitle = 0xF6F7EE;
constexpr uint32_t CalHead = 0x9EAF9E;
constexpr uint32_t CalDay = 0xF4F5EC;
constexpr uint32_t CalOut = 0x788A7A;      // other-month numbers
constexpr uint32_t CalToday = 0x8FABCC;
constexpr uint32_t CalMark = 0xF2B84B;     // expected-day dot
constexpr uint32_t StatusInk = 0x283028;
constexpr uint32_t ShadowInk = 0x47543B;

// ---- cached widgets (owned by this module, reset by build()) -------------
lv_obj_t *hourText, *minuteText, *clockDateText, *clockCard;
bool clockValidShown = false, clockColorInit = false;
lv_obj_t *cityText, *cityShadow, *tempText, *tempShadow;
lv_obj_t *unitText, *unitShadow, *condText, *condShadow;
lv_obj_t *weekName[3], *weekDate[3];
lv_obj_t *calTitle, *calHead[7], *calDays[42], *calTodayCircle, *calMarkDot;
lv_obj_t *wifiIcon, *onlineText;
Ui::BatteryView batView;  // shared locked 24x15 battery component
bool built = false;
// Host-renderer / bench switch: force the "time not synced" placeholder without
// waiting for a cold boot where the RTC has no time yet.
bool forceClockInvalid = false;

// Cache for the calendar grid: only recomputed when the date or target changes.
int shownYear = -1, shownMonth = -1, shownDay = -1;
int shownTargetDay = -1;
bool shownValid = false;

// ---- small builders ------------------------------------------------------
// UTF-8 weekday names, index 0 = Sunday. Three bytes per character.
const char *const kWeekDays = "日一二三四五六";

int weekdayOf(const DeskLogic::Civil &d) {
    const int64_t days = DeskLogic::civilDays(d.year, d.month, d.day);
    return (int)((days + 4) % 7 + 7) % 7;  // 0 = Sunday
}

// A borderless rectangle with an explicit radius (bg opaque unless changed).
lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    auto *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *transparent(lv_obj_t *parent, int x, int y, int w, int h, int radius) {
    auto *o = plain(parent, x, y, w, h, 0x000000, radius);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}

lv_obj_t *labelAt(lv_obj_t *parent, int x, int y, int w, const lv_font_t *font, uint32_t color,
                  lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    auto *o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, align, 0);
    lv_obj_set_style_text_letter_space(o, 0, 0);
    lv_obj_set_style_text_line_space(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// Image at an exact size. Our assets are authored at the target size, but the
// zoom path keeps the page correct if an asset ever changes.
void imageAt(lv_obj_t *parent, const lv_img_dsc_t *src, int x, int y, int w, int h) {
    auto *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    const int sw = src->header.w, sh = src->header.h;
    if (sw > 0 && sh > 0 && (sw != w || sh != h)) {
        const int zw = (w * 256) / sw, zh = (h * 256) / sh;
        const int zoom = zw < zh ? zw : zh;
        lv_img_set_zoom(img, (uint16_t)zoom);
        const int dw = sw * zoom / 256, dh = sh * zoom / 256;
        lv_obj_set_pos(img, x + (w - dw) / 2, y + (h - dh) / 2);
        lv_obj_set_size(img, dw > 0 ? dw : 1, dh > 0 ? dh : 1);
    } else {
        lv_obj_set_pos(img, x, y);
        lv_obj_set_size(img, w, h);
    }
}

void clickTo(lv_obj_t *card, int action) {
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, [](lv_event_t *e) { GiftPages::fire((int)(uintptr_t)lv_event_get_user_data(e)); },
                        LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    Ui::attachPressFeedback(card);
}

// Defined with the live-value helpers below; declared here so the shadowed label
// setters above it can skip unchanged text as well.
bool setLabelText(lv_obj_t *label, const char *text);

void setText2(lv_obj_t *main, lv_obj_t *shadow, const char *text) {
    setLabelText(main, text);
    setLabelText(shadow, text);
}

void setFont2(lv_obj_t *main, lv_obj_t *shadow, const lv_font_t *font) {
    lv_obj_set_style_text_font(main, font, 0);
    lv_obj_set_style_text_font(shadow, font, 0);
}

// Shadowed art label pair, offset by the 1.5 px soft shadow of the design.
void artTextPair(lv_obj_t *parent, int x, int y, int w, const lv_font_t *font, uint32_t color,
                 lv_obj_t **mainOut, lv_obj_t **shadowOut) {
    auto *shadow = labelAt(parent, x + 1, y + 1, w, font, ArtShadow);
    lv_obj_set_style_text_opa(shadow, 160, 0);
    auto *main = labelAt(parent, x, y, w, font, color);
    *shadowOut = shadow;
    *mainOut = main;
}

// 3x3 dot used for the postcard perforation and the expected-day marker.
void dotAt(lv_obj_t *parent, int x, int y, int size, uint32_t color) {
    auto *dot = plain(parent, x, y, size, size, color, LV_RADIUS_CIRCLE);
    (void)dot;
}

// ---- sections ------------------------------------------------------------
void buildPostcard(lv_obj_t *page) {
    // Flat shadow: a copy of the card shifted 3 px down, ~10 % ink.
    auto *shadow = plain(page, 7, 11, 306, 126, ShadowInk, 3);
    lv_obj_set_style_bg_opa(shadow, 26, 0);

    auto *card = plain(page, 7, 8, 306, 126, White, 3);

    // Perforation dots. Coordinates are card-local, from make_home_top_design.py.
    for (int x = 10; x <= 296; x += 11) {
        dotAt(card, x - 1, 6 - 1, 3, DotGreen);
        dotAt(card, x - 1, 119 - 1, 3, DotGreen);
    }
    for (int y = 16; y <= 104; y += 11) {
        dotAt(card, 5 - 1, y - 1, 3, DotGreen);
        dotAt(card, 300 - 1, y - 1, 3, DotGreen);
    }

    // Photo 194x116 with 7 px rounded corners at card-local (5,5).
    auto *holder = transparent(card, 5, 5, 194, 116, 7);
    lv_obj_set_style_clip_corner(holder, true, 0);
    imageAt(holder, &home_postcard_image, 0, 0, 194, 116);

    // Square clock card (213,8) 100x126, drawn on top of the postcard.
    auto *clock = plain(page, 213, 8, 100, 126, ClockGreen, 0);
    clockCard = clock;
    // The design renders "09"/"56" with MS YaHei Bold50 whose digit ink is
    // 61x41 px. Noto Sans SC Bold50 would draw only 53x36 px, so the digits-only
    // face is generated at 56 px instead (ink 59x43); glyph ink starts at the
    // label top, and the labels are centred on the design's ink centres.
    // User-tuned vertical rhythm (2026-10-27): the digits sat only 3 px below
    // the card's top edge in the locked mock, which reads as "touching" on the
    // real panel. Hour/minute move down 4 px and the date 2 px so the top gap
    // becomes 7 px while the internal gaps stay balanced (bottom gap 11 px).
    hourText = labelAt(clock, 0, 6, 100, &font_clock56, ClockHour, LV_TEXT_ALIGN_CENTER);
    minuteText = labelAt(clock, 0, 57, 100, &font_clock56, ClockMin, LV_TEXT_ALIGN_CENTER);
    clockDateText = labelAt(clock, 0, 103, 100, &fd_sans_bold_12, ClockMin, LV_TEXT_ALIGN_CENTER);
    clickTo(clock, GiftPages::OpenClock);
}

void buildWeatherCard(lv_obj_t *page) {
    auto *card = plain(page, 5, 144, 106, 73, WeatherBg, 15);
    lv_obj_set_style_border_width(card, 4, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(WeatherBorder), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);

    // Children are placed relative to the card's content area, i.e. the 4 px
    // frame is already excluded, so the design's card-local coordinates are
    // shifted by -4 here.
    auto *holder = transparent(card, 0, 0, 98, 65, 11);
    lv_obj_set_style_clip_corner(holder, true, 0);
    imageAt(holder, saved_gift_weather_art, 0, 0, 98, 65);

    // Text over the artwork keeps the design's soft dark shadow.
    artTextPair(card, 5, 4, 86, saved_gift_font_bold_13_0, White, &cityText, &cityShadow);
    artTextPair(card, 5, 22, 60, &fd_sans_bold_16, White, &tempText, &tempShadow);
    artTextPair(card, 33, 27, 30, saved_gift_font_bold_11_0, White, &unitText, &unitShadow);
    lv_label_set_text(unitText, "°C");
    lv_label_set_text(unitShadow, "°C");
    artTextPair(card, 5, 45, 92, saved_gift_font_bold_13_0, White, &condText, &condShadow);
    clickTo(card, GiftPages::OpenWeather);
}

void buildWeekCard(lv_obj_t *page) {
    auto *card = plain(page, 113, 145, 91, 74, White, 12);  // PNG corner radius
    lv_obj_set_style_clip_corner(card, true, 0);

    imageAt(card, saved_gift_cat, 10, 3, 71, 43);

    // Pale 3-day strip: rounded top half (8,42) 75x13 r8 + squared bottom fill.
    auto *strip = plain(card, 8, 42, 75, 13, WeekYellow, 8);
    (void)strip;
    auto *stripFill = plain(card, 8, 49, 75, 6, WeekYellow, 0);
    (void)stripFill;

    // Today capsule, behind the date labels.
    plain(card, 35, 55, 21, 14, TodayPill, 6);

    for (int i = 0; i < 3; ++i) {
        const int center = (int)lroundf((float)(8 + 12.5 + i * 25));
        weekName[i] = labelAt(card, center - 8, 43, 16, saved_gift_font_bold_7_0, WeekInk,
                              LV_TEXT_ALIGN_CENTER);
        weekDate[i] = labelAt(card, center - 8, 56, 16, saved_gift_font_bold_10_0, WeekDate,
                              LV_TEXT_ALIGN_CENTER);
    }
    clickTo(card, GiftPages::OpenCountdown);
}

void buildCalendarCard(lv_obj_t *page) {
    auto *card = plain(page, 208, 144, 108, 79, CalBg, 12);  // PNG corner radius
    calTitle = labelAt(card, 6, 4, 96, saved_gift_font_bold_11_0, CalTitle);
    for (int i = 0; i < 7; ++i) {
        const int center = 7 + (int)lroundf(i * 13.6f);
        calHead[i] = labelAt(card, center - 7, 20, 14, saved_gift_font_bold_6_0, CalHead,
                             LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(calHead[i], i == 0 ? "一" : i == 1 ? "二" : i == 2 ? "三"
                                   : i == 3 ? "四" : i == 4 ? "五" : i == 5 ? "六" : "日");
    }
    calTodayCircle = plain(card, 0, 0, 11, 11, CalToday, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(calTodayCircle, LV_OBJ_FLAG_HIDDEN);
    calMarkDot = plain(card, 0, 0, 3, 3, CalMark, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(calMarkDot, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 42; ++i) {
        const int center = 7 + (int)lroundf((i % 7) * 13.6f);
        calDays[i] = labelAt(card, center - 7, 0, 14, saved_gift_font_bold_7_0, CalDay,
                             LV_TEXT_ALIGN_CENTER);
        lv_obj_add_flag(calDays[i], LV_OBJ_FLAG_HIDDEN);
    }
    clickTo(card, GiftPages::OpenCalendar);
}

void buildStatusBar(lv_obj_t *page) {
    // Full 17x17 dark status glyph, generated from the locked 其它WIFI.png so it
    // is never zoomed/clipped (same asset as Ui::statusCluster()).
    wifiIcon = lv_img_create(page);
    lv_img_set_src(wifiIcon, &ui_status_wifi);
    lv_obj_set_pos(wifiIcon, 12, 219);
    lv_obj_clear_flag(wifiIcon, LV_OBJ_FLAG_CLICKABLE);

    onlineText = labelAt(page, 33, 222, 74, saved_gift_font_bold_13_0, StatusInk);

    // Locked 24x15 battery: 21x15 body + 3x7 cap outside it, four fixed cells
    // (3/4/3/4 @2/6/11/15) and the Bold11 percentage right-aligned to x=309.
    Ui::batteryCreate(batView, page, 246, 225, 309, 232.5f);
}

// ---- live values ---------------------------------------------------------

// Sets a label only when the text actually changed; lv_label_set_text() always
// invalidates (even for identical text), which repainted the clock labels every
// refresh and could leave a visible flash/black patch on the panel.
bool setLabelText(lv_obj_t *label, const char *text) {
    if (!label) return false;
    const char *current = lv_label_get_text(label);
    if (current && strcmp(current, text) == 0) return false;
    lv_label_set_text(label, text);
    return true;
}

void updateClock(bool valid, const DeskLogic::Civil &d, int wd) {
    char hour[8], minute[8], date[32];
    if (valid) {
        snprintf(hour, sizeof hour, "%02d", d.hour);
        snprintf(minute, sizeof minute, "%02d", d.minute);
        char weekday[4] = {0};
        memcpy(weekday, kWeekDays + wd * 3, 3);  // "%s" needs a terminated string
        snprintf(date, sizeof date, "%02d/%02d 星期%s", d.month, d.day, weekday);
    } else {
        // Cold-boot placeholder: mid-green dashes instead of the near-black
        // #354F1D hour colour, with the "等待校时" line explaining the state.
        snprintf(hour, sizeof hour, "--");
        snprintf(minute, sizeof minute, "--");
        snprintf(date, sizeof date, "等待校时");
    }

    bool changed = false;
    changed |= setLabelText(hourText, hour);
    changed |= setLabelText(minuteText, minute);
    changed |= setLabelText(clockDateText, date);
    if (!clockColorInit || clockValidShown != valid) {
        clockColorInit = true;
        clockValidShown = valid;
        lv_obj_set_style_text_color(hourText, lv_color_hex(valid ? ClockHour : ClockCold), 0);
        changed = true;
    }
    // Repaint the whole card, never just the label: the card is drawn on top of
    // the postcard, so a label-only redraw could show the layer underneath.
    if (changed && clockCard) lv_obj_invalidate(clockCard);
}

void updateWeather() {
    const lv_font_t *cityFonts[] = {saved_gift_font_bold_13_0, saved_gift_font_bold_11_0,
                                    saved_gift_font_bold_10_0};
    String city = settings.city.isEmpty() ? String("未设置城市") : settings.city;
    int pick = 0;
    for (int i = 0; i < 2; ++i) {
        if (lv_txt_get_width(city.c_str(), city.length(), cityFonts[i], 0, LV_TEXT_FLAG_NONE) <= 86) break;
        pick = i + 1;
    }
    if (lv_obj_get_style_text_font(cityText, 0) != cityFonts[pick]) {
        setFont2(cityText, cityShadow, cityFonts[pick]);
        const lv_label_long_mode_t mode = pick == 2 ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_CLIP;
        lv_label_set_long_mode(cityText, mode);
        lv_label_set_long_mode(cityShadow, mode);  // keep the 1px shadow truncation in sync
    }
    setText2(cityText, cityShadow, city.c_str());

    const String temperature = weather.valid ? TextUtil::number(weather.temperature) : String("--");
    setText2(tempText, tempShadow, temperature.c_str());
    const int tempW = lv_txt_get_width(temperature.c_str(), temperature.length(), &fd_sans_bold_16, 0,
                                       LV_TEXT_FLAG_NONE);
    const int unitX = 5 + tempW + 4;  // card content coords (design x=-4)
    lv_obj_set_pos(unitText, unitX, 27);
    lv_obj_set_pos(unitShadow, unitX + 1, 28);

    String condition = weather.valid ? weather.condition : String("暂无");
    if (condition.isEmpty()) condition = "暂无";
    setText2(condText, condShadow, condition.c_str());
}

void updateWeek(bool valid, int64_t now) {
    for (int i = 0; i < 3; ++i) {
        char name[4] = {0};
        char date[8];
        if (valid) {
            auto day = DeskLogic::beijingDate(now + (i - 1) * 86400);
            memcpy(name, kWeekDays + weekdayOf(day) * 3, 3);
            snprintf(date, sizeof date, "%d", day.day);
        } else {
            snprintf(date, sizeof date, "--");
        }
        setLabelText(weekName[i], valid ? name : "-");
        setLabelText(weekDate[i], date);
        const uint32_t nameInk = i == 1 ? WeekTodayInk : WeekInk;
        const uint32_t dateInk = i == 1 ? White : WeekDate;
        if (lv_obj_get_style_text_color(weekName[i], 0).full != lv_color_hex(nameInk).full)
            lv_obj_set_style_text_color(weekName[i], lv_color_hex(nameInk), 0);
        if (lv_obj_get_style_text_color(weekDate[i], 0).full != lv_color_hex(dateInk).full)
            lv_obj_set_style_text_color(weekDate[i], lv_color_hex(dateInk), 0);
    }
}

void updateCalendar(bool valid, const DeskLogic::Civil &d) {
    int targetDay = -1;
    if (settings.hasTarget) {
        auto target = DeskLogic::beijingDate(settings.target);
        if (target.year == d.year && target.month == d.month) targetDay = target.day;
    }
    if (shownValid == valid && shownYear == d.year && shownMonth == d.month && shownDay == d.day &&
        shownTargetDay == targetDay)
        return;  // the grid only depends on the day and the target date
    shownValid = valid;
    shownYear = d.year;
    shownMonth = d.month;
    shownDay = d.day;
    shownTargetDay = targetDay;

    if (calTitle) {
        char title[24];
        if (valid) snprintf(title, sizeof title, "%d月%d日", d.month, d.day);
        else snprintf(title, sizeof title, "未校时");
        lv_label_set_text(calTitle, title);
    }

    // Monday-first grid, copied from the locked design's make_home_design.py.
    const int offset = valid ? (weekdayOf({d.year, d.month, 1, 0, 0, 0}) + 6) % 7 : 0;
    const int days = valid ? DeskLogic::daysInMonth(d.year, d.month) : 0;
    const int rows = valid ? (offset + days + 6) / 7 : 0;
    const int prevMonth = d.month == 1 ? 12 : d.month - 1;
    const int prevYear = d.month == 1 ? d.year - 1 : d.year;
    const int prevDays = valid ? DeskLogic::daysInMonth(prevYear, prevMonth) : 0;
    const float startY = 28.5f;
    const float step = rows == 6 ? 6.6f : 7.9f;

    int todaySlot = -1;
    int markSlot = -1;
    for (int slot = 0; slot < 42; ++slot) {
        auto *cell = calDays[slot];
        int day = 0;
        bool current = false;
        if (!valid || slot >= rows * 7) {
            lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (slot < offset) {
            day = prevDays - offset + slot + 1;
        } else if (slot < offset + days) {
            day = slot - offset + 1;
            current = true;
        } else {
            day = slot - offset - days + 1;
        }
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(cell, "%d", day);
        const int row = slot / 7;
        lv_obj_set_pos(cell, 7 + (int)lroundf((slot % 7) * 13.6f) - 7,
                       (int)lroundf(startY + row * step) - 1);
        const bool isToday = current && day == d.day;
        lv_obj_set_style_text_color(cell,
                                    lv_color_hex(!current ? CalOut : isToday ? White : CalDay), 0);
        if (isToday) todaySlot = slot;
        if (current && day == targetDay && !isToday) markSlot = slot;
    }

    // Today pill (the day label is raised above it so the number stays visible).
    if (todaySlot >= 0 && calTodayCircle) {
        const int centerX = 7 + (int)lroundf((todaySlot % 7) * 13.6f);
        const int centerY = (int)lroundf(startY + (todaySlot / 7) * step) + 3;
        lv_obj_set_pos(calTodayCircle, centerX - 5, centerY - 5);
        lv_obj_clear_flag(calTodayCircle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(calTodayCircle);
        lv_obj_move_foreground(calDays[todaySlot]);
    } else if (calTodayCircle) {
        lv_obj_add_flag(calTodayCircle, LV_OBJ_FLAG_HIDDEN);
    }

    // Expected-day dot below the number (today wins on overlap).
    if (markSlot >= 0 && calMarkDot) {
        const int centerX = 7 + (int)lroundf((markSlot % 7) * 13.6f);
        const int dotY = (int)lroundf(startY + (markSlot / 7) * step + 8.7f);
        lv_obj_set_pos(calMarkDot, centerX - 1, dotY - 1);
        lv_obj_clear_flag(calMarkDot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(calMarkDot);
    } else if (calMarkDot) {
        lv_obj_add_flag(calMarkDot, LV_OBJ_FLAG_HIDDEN);
    }
}

void updateStatus() {
    const bool online = WiFi.status() == WL_CONNECTED;
    if (wifiIcon) {
        const auto *src = online ? &ui_status_wifi : &ui_status_nowifi;
        if (lv_img_get_src(wifiIcon) != (const void *)src) lv_img_set_src(wifiIcon, src);
    }
    if (onlineText) {
        // The PNG renders "Online" at 12 px; the offline word 离线 needs the
        // 13 px face because the 12 px subset has no 线 glyph.
        const lv_font_t *font = online ? &fd_sans_bold_12 : saved_gift_font_bold_13_0;
        if (lv_obj_get_style_text_font(onlineText, 0) != font)
            lv_obj_set_style_text_font(onlineText, font, 0);
        setLabelText(onlineText, online ? "Online" : "离线");
    }

    // Shared locked battery component: colours, 4-cell fill, charging bolt,
    // <=5 % blinking frame and the right-aligned Bold11 percentage.
    Ui::batteryRefresh(batView);
}

}  // namespace

// ---- page_api.h contract -------------------------------------------------
// A5: status-only pass for the cached-home path (Wi-Fi icon / online text /
// battery + percentage), called at 500 ms so the <=5 % blink keeps its phase.
// Never rebuilds, creates or moves a widget.
void refreshStatus() {
    if (!built) return;
    updateStatus();
}

void build(lv_obj_t *parent) {
    // Reset every cache first: the host renderer rebuilds pages often and the
    // device rebuilds after lv_obj_clean().
    hourText = minuteText = clockDateText = nullptr;
    clockCard = nullptr;
    clockValidShown = false;
    clockColorInit = false;
    cityText = cityShadow = tempText = tempShadow = nullptr;
    unitText = unitShadow = condText = condShadow = nullptr;
    for (auto *&o : weekName) o = nullptr;
    for (auto *&o : weekDate) o = nullptr;
    calTitle = nullptr;
    calTodayCircle = calMarkDot = nullptr;
    for (auto *&o : calHead) o = nullptr;
    for (auto *&o : calDays) o = nullptr;
    wifiIcon = onlineText = nullptr;
    Ui::batteryClear(batView);
    built = false;
    shownYear = shownMonth = shownDay = -1;
    shownTargetDay = -1;
    shownValid = false;

    lv_obj_set_style_bg_color(parent, lv_color_hex(Cream), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    buildPostcard(parent);
    buildWeatherCard(parent);
    buildWeekCard(parent);
    buildCalendarCard(parent);
    buildStatusBar(parent);

    built = hourText && minuteText && clockDateText && cityText && tempText && unitText &&
            condText && weekName[0] && weekDate[0] && calTitle && calDays[0] && onlineText &&
            batView.wrapper && batView.percent;
    refresh(true);  // fill live values before the first flush
}

void refresh(bool force) {
    (void)force;
    if (!built) return;
    const int64_t now = ClockSource::now();
    const bool valid = !forceClockInvalid && DeskLogic::timeValid(now);
    const DeskLogic::Civil d = DeskLogic::beijingDate(now);
    const int wd = valid ? weekdayOf(d) : 0;

    updateClock(valid, d, wd);
    updateWeather();
    updateWeek(valid, now);
    updateCalendar(valid, d);
    updateStatus();
}

bool ready() { return built; }

void cancel() {}

bool back() { return false; }

// No detail level on the home page: only the App/origin layer can go back.
bool canBack() { return false; }

void debugVariant(int which) { forceClockInvalid = (which == 1); }

}  // namespace HomePage
