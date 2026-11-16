#pragma once
// Per-App navigation origin table (V2 Core, audit P0-1).
//
// The firmware used to keep one (phoneReturn, phoneReturnPage) pair for the App
// layer. A single pair cannot represent the real navigation graph: page 6 ->
// Settings -> Wi-Fi overwrites the Settings origin, so the second back press
// re-enters Settings (a return self-loop), and Calendar -> Countdown(confirm) ->
// Calendar overwrites the Calendar origin the same way. One origin slot per App
// keeps every level independent.
//
// This header is deliberately free of Arduino/LVGL: it is a pure data transform so
// the host regression (hardware/tests/host/nav_origin_test.cpp) can pin the return
// model without compiling desk.cpp.
//
// Conventions (frozen in 代码审查/V2实施-接口契约.md §4):
//   originApp  = (int)App of the caller; -1 means App::None (one of the 7 pages).
//   originPage = page of the caller 0..6; -1 means "no origin recorded yet".
#include <stdint.h>

struct NavOrigin {
    int8_t originApp = -1;   // -1 = App::None, otherwise (int)App
    int8_t originPage = -1;  // -1 = not recorded, otherwise 0..6
};

// App::None .. App::Games; existing numeric app IDs remain stable.
constexpr int NavAppCount = 30;

// Records "targetApp was entered from (originApp, originPage)".
// Invalid indexes and self entries are ignored: recording target->target would
// resolve to a no-op back press, which is exactly the bug this table prevents.
inline void navRecord(NavOrigin table[NavAppCount], int targetApp, int originApp,
                      int originPage) {
    if (!table || targetApp < 0 || targetApp >= NavAppCount) return;
    if (originApp < -1 || originApp >= NavAppCount) return;
    if (originApp == targetApp) return;  // never record a self origin
    if (originPage < 0 || originPage > 6) return;
    table[targetApp].originApp = (int8_t)originApp;
    table[targetApp].originPage = (int8_t)originPage;
}

// Resolves the recorded origin for currentApp.
// Returns false when there is no usable origin: never recorded, stale/invalid
// values, or a self-loop (originApp == currentApp). The caller then falls back to
// the root page (App::None, page 6) instead of re-entering the same App.
inline bool navResolve(const NavOrigin table[NavAppCount], int currentApp,
                       int *originApp, int *originPage) {
    if (!table || !originApp || !originPage) return false;
    if (currentApp < 0 || currentApp >= NavAppCount) return false;
    const NavOrigin origin = table[currentApp];
    if (origin.originApp < -1 || origin.originApp >= NavAppCount) return false;
    if (origin.originPage < 0 || origin.originPage > 6) return false;
    if (origin.originApp == currentApp) return false;  // self-loop guard
    *originApp = origin.originApp;
    *originPage = origin.originPage;
    return true;
}
