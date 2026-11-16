#pragma once
#include <lvgl.h>
#include <Arduino.h>
namespace GiftUI {
bool render(lv_obj_t *root);
void refresh(bool force = false);
bool inputActive();
bool swipeApps(int dx);
unsigned appsPageIndex();
void setAppsPage(unsigned value);
// Page-level back only: hand-written page details and the legacy `section` views.
// App-level return (per-App origin table) is desk.cpp's back(); canBack() is the
// pure query with the same order, consumed by desk.cpp's GiftPages::canGoBack().
bool back();
bool canBack();
void reset();
// Cached-home returns switch layers without calling render(): stop the current
// view's animations and forget which page module owns the layer.
void leaveView();
String viewKey();

// ---- cached home view (P1-3) ---------------------------------------------
// The home page (page 0, app None) is built exactly once into a permanent layer.
// HomeRefs caches every widget the 1 Hz update touches, so the home view can be
// refreshed without the SavedDesign name index (which is invalidated whenever
// another page is created) and without rebuilding any widget.
struct HomeRefs {
    lv_obj_t *layer = nullptr;
    lv_obj_t *clockHour = nullptr, *clockMinute = nullptr, *clockDate = nullptr, *clockWeekday = nullptr;
    lv_obj_t *homeTemperature = nullptr, *homeTemperatureUnit = nullptr, *homeTemperatureUnitText = nullptr;
    lv_obj_t *homeCity = nullptr, *homeCondition = nullptr;
    lv_obj_t *calendarTitle = nullptr, *calendarToday = nullptr;
    lv_obj_t *calendar[31] = {};
    lv_obj_t *calendarExtra[11] = {};  // labels for months that need 6 rows
    lv_obj_t *weekDay[3] = {}, *weekNumber[3] = {};
    lv_obj_t *text79 = nullptr, *icon80 = nullptr, *icon85 = nullptr, *powerStatus = nullptr;
    lv_obj_t *statusWifi = nullptr, *statusBt = nullptr, *statusBattery = nullptr;
    lv_obj_t *statusOnline = nullptr, *statusBtLabel = nullptr, *statusPercent = nullptr;
    lv_obj_t *wifiCross = nullptr;
    // False until every required widget above was resolved; callers keep the
    // normal rebuild path when this is false.
    bool valid = false;
};

// Builds the home page into `layer` and fills `refs`. Resets the normal view
// pointer globals first (same contract as render()), so a later page render()
// cannot see pointers into the cached tree.
void buildHome(lv_obj_t *layer, HomeRefs &refs);
// Refreshes the cached home through `refs` only. 1 Hz throttle + 100 ms
// PowerStatus polling are kept; `force` skips the throttle.
void refreshHome(const HomeRefs &refs, bool force = false);
// True once buildHome() succeeded (the cache is permanent for the session).
bool homeCached();
}
