#pragma once
// The seam that lets an AiDoGUI / SquareLine style export take over any subset of
// the firmware's views without touching the rest of the program.
//
// Design notes
// ------------
// The built-in UI draws every view into one LVGL screen (`root`) and rebuilds its
// children on each navigation. Generated designer output works the other way
// around: it creates its own `lv_obj_t` screens and switches with `lv_scr_load()`.
// Both models can therefore not own the screen at once.
//
// This module keeps the built-in model as the default and lets generated code
// claim individual slots. `dispatch()` is asked first; if a provider claims the
// slot it has already made its own screen current and the built-in renderer must
// be skipped completely. If nothing claims it, `activateBuiltIn()` restores the
// built-in screen (a provider may have switched away on an earlier navigation)
// and the built-in renderer runs as before.
//
// Services (WiFi, NTP, weather, NVS, touch) are unaffected either way: they live
// outside the screen model.
#include <lvgl.h>

namespace Screens {

// Every addressable view. Generated code may claim any subset; anything not
// claimed keeps using the built-in implementation.
enum Slot {
    SlotPageHome = 0,
    SlotPageAlbum,
    SlotPageMessage,
    SlotPageCountdown,
    SlotPageApps,
    SlotAppAlbum,
    SlotAppMessages,
    SlotAppWeather,
    SlotAppCalendar,
    SlotAppCountdown,
    SlotAppSettings,
    SlotAppPhone,
    SlotAppAbout,
    SlotAppDiagnostics,
    SlotAppVoice,
    SlotAppWifi,
    SlotPageDaily,
    SlotPageMessages,
    SlotAppClock,
    SlotAppAlarm,
    SlotAppPomodoro,
    SlotCount
};

// A provider returns true only after it has made its own screen the active one
// (typically via lv_scr_load()). Returning false means "not mine, render yours".
using Provider = bool (*)(int slot);

void setProvider(Provider provider);
Provider provider();

// Offers `slot` to the registered provider. True means it owns the screen now.
bool dispatch(int slot);

// Makes the built-in screen active again. Safe to call when it already is.
void activateBuiltIn(lv_obj_t *builtInRoot);

// Clears the "a provider owns the screen" flag. Needed by test code that claims a
// slot and then wants the built-in screen restored.
void releaseExternal();

// True while a provider-owned screen is current.
bool externalActive();

// Maps the firmware's (page, app) state onto a Slot, or -1 when out of range.
// `app` uses the firmware's App enum as an integer; 0 means "no app open".
int slotFor(int page, int app);

// Verifies slot mapping and provider bookkeeping.
bool selfTest();

}
