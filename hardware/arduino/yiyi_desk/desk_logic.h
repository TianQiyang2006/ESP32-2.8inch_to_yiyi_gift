#pragma once
#include <stdint.h>
#include <ctype.h>
#include <string.h>

namespace DeskLogic {
// Rotation 1 of the native 240x320 CTP, plus optional landscape mirror bits.
// Mode 0 is the board profile. These modes are not four LCD rotations.
inline bool touchToLandscape(int rx, int ry, uint8_t mode, int16_t &x, int16_t &y) {
    if (rx < 0 || rx >= 240 || ry < 0 || ry >= 320) return false;
    x = (mode & 1) ? 319 - ry : ry;
    y = (mode & 2) ? rx : 239 - rx;
    return true;
}
inline uint8_t nextPage(uint8_t page, int dx) {
    // The user's direction: swipe RIGHT advances through all seven pages.
    return (page + (dx > 0 ? 1 : 6)) % 7;
}
// The clock counts as trustworthy once NTP has moved it past late 2023. Kept here
// so every module shares one threshold instead of repeating the constant.
constexpr int64_t ValidEpochThreshold = 1700000000LL;
inline bool timeValid(int64_t epoch) { return epoch > ValidEpochThreshold; }
inline bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
inline int daysInMonth(int year, int month) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return month >= 1 && month <= 12 ? days[month-1] + (month == 2 && leap(year)) : 0;
}
inline bool validDate(int y, int m, int d, int h = 0, int min = 0, int s = 0) {
    return y >= 1900 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 &&
           d <= daysInMonth(y,m) && h >= 0 && h < 24 && min >= 0 && min < 60 && s >= 0 && s < 60;
}
// Civil date math uses int64_t rather than the ESP-IDF 4.x 32-bit time_t.
// This preserves the promised 1900..2099 target range, including dates after 2038.
inline int64_t civilDays(int year, int month, int day) {
    year -= month <= 2;
    int era = (year >= 0 ? year : year-399)/400;
    unsigned yoe = year-era*400;
    unsigned doy = (153*(month+(month>2?-3:9))+2)/5+day-1;
    unsigned doe = yoe*365+yoe/4-yoe/100+doy;
    return (int64_t)era*146097+doe-719468;
}
inline int64_t beijingEpoch(int y, int m, int d, int h=0, int min=0) {
    return civilDays(y,m,d)*86400+h*3600+min*60-8*3600;
}
struct Civil { int year, month, day, hour, minute, second; };
inline Civil beijingDate(int64_t epoch) {
    epoch += 8*3600;
    int64_t z = epoch/86400, seconds = epoch%86400;
    if (seconds<0) { --z; seconds+=86400; }
    z += 719468;
    int64_t era = (z>=0 ? z : z-146096)/146097;
    unsigned doe = z-era*146097;
    unsigned yoe = (doe-doe/1460+doe/36524-doe/146096)/365;
    int year = yoe+era*400;
    unsigned doy = doe-(365*yoe+yoe/4-yoe/100), mp = (5*doy+2)/153;
    int day = doy-(153*mp+2)/5+1, month = mp+(mp<10?3:-9);
    year += month<=2;
    return {year,month,day,(int)(seconds/3600),(int)(seconds/60%60),(int)(seconds%60)};
}
inline int naturalCompare(const char *a, const char *b) {
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            const char *sa = a, *sb = b;
            while (*a == '0') ++a;
            while (*b == '0') ++b;
            const char *ea = a, *eb = b;
            while (isdigit((unsigned char)*ea)) ++ea;
            while (isdigit((unsigned char)*eb)) ++eb;
            int la = ea-a, lb = eb-b;
            if (la != lb) return la < lb ? -1 : 1;
            int c = strncmp(a,b,la);
            if (c) return c;
            int zerosA = a-sa, zerosB = b-sb;
            if (zerosA != zerosB) return zerosA < zerosB ? -1 : 1;
            a = ea; b = eb;
        } else {
            int c = tolower((unsigned char)*a) - tolower((unsigned char)*b);
            if (c) return c;
            ++a; ++b;
        }
    }
    return (unsigned char)*a - (unsigned char)*b;
}
// A strict UTF-8 validator also counts codepoints for configuration length limits.
inline int utf8Length(const char *text) {
    int count = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t cp = *p++;
        if (cp < 0x80) { ++count; continue; }
        int n; uint32_t min;
        if (cp >= 0xC2 && cp <= 0xDF) { n=1; cp &= 0x1F; min=0x80; }
        else if (cp >= 0xE0 && cp <= 0xEF) { n=2; cp &= 0x0F; min=0x800; }
        else if (cp >= 0xF0 && cp <= 0xF4) { n=3; cp &= 0x07; min=0x10000; }
        else return -1;
        while (n--) { if ((*p & 0xC0) != 0x80) return -1; cp = (cp<<6) | (*p++ & 0x3F); }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
        ++count;
    }
    return count;
}
inline bool selfTest() {
    int16_t x,y;
    bool ok = touchToLandscape(0,0,0,x,y) && x == 0 && y == 239;
    ok &= touchToLandscape(239,319,0,x,y) && x == 319 && y == 0;
    ok &= touchToLandscape(0,319,1,x,y) && x == 0 && y == 239;
    ok &= touchToLandscape(239,0,2,x,y) && x == 0 && y == 239;
    ok &= !touchToLandscape(240,319,0,x,y);
    ok &= nextPage(6,40) == 0 && nextPage(0,-40) == 6;
    ok &= leap(2000) && !leap(1900) && validDate(2028,2,29) && !validDate(2026,2,29);
    ok &= !validDate(2100,1,1) && !validDate(2026,1,1,24);
    ok &= naturalCompare("002.jpg","010.jpg") < 0 && naturalCompare("9.jpg","10.jpg") < 0;
    ok &= utf8Length("给依依") == 3 && utf8Length("\xc0\xaf") == -1;
    Civil future=beijingDate(beijingEpoch(2099,12,31,23,59));
    Civil past=beijingDate(beijingEpoch(1900,1,1,0,0));
    ok &= future.year==2099 && future.month==12 && future.day==31 && future.hour==23 && future.minute==59;
    ok &= past.year==1900 && past.month==1 && past.day==1;
    ok &= beijingEpoch(1970,1,1,8,0)==0;
    return ok;
}
}

// ---- gesture routing -------------------------------------------------------
// Pure routing for a finished swipe, shared by the UI layer and the host tests.
// The rules are frozen in 代码审查/V2实施-接口契约.md §3; touch.cpp's swipe
// threshold (abs(dx) > 45 && abs(dx) > abs(dy)*3/2) must stay in sync with it.
enum class GestureIntent { None, Back, PageNext, PagePrev };

struct GestureContext {
    bool transitioning;  // Display::transitioning(): a page animation is running
    bool inputActive;    // GiftUI::inputActive(): a slider/textarea holds input
    bool appOpen;        // app != App::None
    bool canGoBack;      // some level (detail/app/Wi-Fi editing) can be dismissed
    bool gameOwnsGestures;  // directional game input excludes global navigation
};

inline GestureIntent gestureIntent(int16_t x0, int16_t y0, int16_t dx, int16_t dy,
                                   const GestureContext &ctx) {
    (void)y0;  // reserved: vertical edge gestures are not part of this contract
    if (ctx.transitioning || ctx.gameOwnsGestures) return GestureIntent::None;
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    // The same horizontal test as touch.cpp: strictly more than 45 px and clearly
    // horizontal, so a vertical drag never reaches the page/app branches.
    if (!(ax > 45 && ax > ay * 3 / 2)) return GestureIntent::None;
    // Left 20 px of the landscape panel (320 wide); only a right swipe can go back.
    // The |dy| cap keeps a diagonal drag from being treated as an edge back.
    const int edgeDyLimit = ax / 2 < 20 ? ax / 2 : 20;
    const bool edge = x0 <= 19 && dx > 0 && ay <= edgeDyLimit;
    if (edge) {
        if (ctx.inputActive) return GestureIntent::None;  // never leave a dragged slider
        if (ctx.canGoBack) return GestureIntent::Back;
        // Root page fallthrough: an edge swipe must never be swallowed.
        return GestureIntent::PageNext;
    }
    if (ctx.inputActive) return GestureIntent::None;
    // V2: a page-internal detail (reading/daily) or a legacy section is dismissed
    // by the same horizontal swipe in either direction, and that gesture must not
    // also flip the page (legacy onTouchGesture called GiftUI::back() first). Only
    // page-level details qualify: app pages (appOpen) never page on a non-edge
    // swipe, and app-level back is the edge path above.
    if (!ctx.appOpen && ctx.canGoBack) return GestureIntent::Back;
    if (ctx.appOpen) return GestureIntent::None;  // Clock decision A: non-edge never acts
    return dx > 0 ? GestureIntent::PageNext : GestureIntent::PagePrev;
}
