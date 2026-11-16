#pragma once
// Contract between the hand-written page modules (src/pages/*.cpp) and the rest
// of the built-in UI.
//
// Every page module implements the same entry points:
//
//   build(parent)      create the whole page under `parent` (the LVGL layer that
//                      gift_ui.cpp passes in); called once per page build.
//   refresh(force)     update live values (clock / weather / battery / data).
//                      Called from the 1 Hz live pass; `force` skips the throttle.
//                      Must only touch the pointers cached by build().
//   ready()            true once build() created every widget refresh() needs.
//                      Currently only HomePage::ready() is consumed (by
//                      GiftUI::buildHome()); the other modules' ready() are kept
//                      as contract stubs for a future rebuild fallback.
//   cancel()           stop every animation that still points at a widget.
//                      Called after desk.cpp has cleaned the layer, so it must
//                      only call lv_anim_del()/reset cached pointers and must
//                      never touch widget contents.
//   back()             handle the back gesture. Return true when the module
//                      consumed it (e.g. closed a detail overlay); false lets the
//                      firmware fall back to normal page navigation.
//   canBack()          pure query with exactly the same condition as back(),
//                      without side effects. Used by GiftUI::canBack() so the
//                      swipe router can decide "is there a level to go back to"
//                      before a gesture is executed (V2 Core).
//   debugVariant(n)    host-renderer hook only (hardware/tests/host/pages_runner.cpp):
//                      select a page state for off-screen screenshots, 0 = default.
//                      Device code never calls it; writing to it is harmless.
//
// Cross-page navigation and network refresh requests go through GiftPages::fire()
// (implemented in desk.cpp, where navigation and the job queue live). A module
// must never change `page`/`app`; after an in-page interaction it may request a
// rebuild of itself with `renderPending = true; lastInputAt = millis();`
// (app_state.h) -- that is what DailyPage::requestRebuild() does.
#include <lvgl.h>

namespace GiftPages {

// Actions a page may request. Values are stable; extend at the end.
enum Action {
    OpenHome = 1,
    OpenWeather,    // also starts a weather refresh
    OpenReading,
    OpenDaily,
    OpenSchedule,
    OpenApps,
    OpenCalendar,
    OpenCountdown,
    OpenClock,
    OpenSettings,
    OpenPhone,
    RefreshWeather, // manual weather update
    RefreshQuote,   // re-fetch the daily quote
    RefreshPoem     // re-fetch the daily poem
};

// Fire an action from a widget callback. Safe to call from any LVGL event.
void fire(int action);

// ---- unified navigation entry points (V2 Core) ----------------------------
// Implemented in desk.cpp; hardware/tests/host/support.cpp provides the offscreen
// runners' stubs (desk.cpp is not part of the host build).
//
// Every App entry goes through openApp() so the per-app origin table records where
// the user came from (audit P0-1/P0-2). App values are (int)::App, see app_state.h.
// openApp() applies the existing entry side effects (Voice launch, Countdown /
// Calendar state reset); callers keep their own pre-state writes and any extra
// work (e.g. WifiSetup::requestScan()).
void openApp(int targetApp);
// Switches to one of the seven root pages (App::None) and clears the page-internal
// state, exactly like the old fire() page cases.
void openPage(int targetPage);
// True when back() has a level to dismiss: a page detail/section, the Wi-Fi
// keyboard (editing -> list), or an App with a usable origin.
bool canGoBack();

}  // namespace GiftPages

namespace HomePage {
void build(lv_obj_t *parent);
void refresh(bool force = false);
// Battery-only refresh used by the cached-home path so the <=5% blink keeps its
// 500 ms phase instead of being sampled at 1 Hz (A5). Does not touch layout.
void refreshStatus();
bool ready();
void cancel();
bool back();
bool canBack();  // pure query, same condition as back(); HomePage has no detail level
void debugVariant(int which);
}  // namespace HomePage

struct Weather;  // app_state.h

namespace WeatherPage {
void build(lv_obj_t *parent);
void refresh(bool force = false);
// Persist the last good reading into /weather_cache.txt from the data layer (A3),
// so a fetch received while another page is visible still survives a reboot.
// Safe to call outside the weather page; never touches LVGL.
void storeCache(const Weather &reading);
bool ready();
void cancel();
bool back();
bool canBack();  // pure query, same condition as back(); WeatherPage has no detail level
void debugVariant(int which);
}  // namespace WeatherPage

namespace DailyPage {
void build(lv_obj_t *parent);
void refresh(bool force = false);
bool ready();
void cancel();
bool back();
bool canBack();  // pure query: view != View::Main
void debugVariant(int which);
}  // namespace DailyPage

namespace ReadingPage {
void build(lv_obj_t *parent);
void refresh(bool force = false);
bool ready();
void cancel();
bool back();
bool canBack();  // pure query: view != 0 (quote/poem detail)
void debugVariant(int which);
}  // namespace ReadingPage
