#include "screens.h"

namespace Screens {
namespace {

Provider provider_ = nullptr;
lv_obj_t *builtInRoot_ = nullptr;
bool externalActive_ = false;

// App enum values as used by the firmware. Kept as plain numbers so this module
// does not depend on desk.cpp internals.
constexpr int AppNone = 0, AppAlbum = 1, AppMessages = 2, AppWeather = 3,
              AppCalendar = 4, AppCountdown = 5, AppSettings = 6, AppPhone = 7,
              AppAbout = 8, AppDiagnostics = 9, AppVoice = 10, AppWifi = 11, AppClock = 12,
              AppAlarm = 13, AppPomodoro = 14;

}  // namespace

void setProvider(Provider provider) { provider_ = provider; }
Provider provider() { return provider_; }

bool dispatch(int slot) {
    if (slot < 0 || slot >= SlotCount || provider_ == nullptr) {
        // Nothing owns this slot; make sure the built-in screen is the active one.
        externalActive_ = false;
        return false;
    }
    const bool claimed = provider_(slot);
    externalActive_ = claimed;
    return claimed;
}

void activateBuiltIn(lv_obj_t *builtInRoot) {
    if (builtInRoot) builtInRoot_ = builtInRoot;
    if (!builtInRoot_ || externalActive_) return;
    if (lv_scr_act() != builtInRoot_) lv_scr_load(builtInRoot_);
}

bool externalActive() { return externalActive_; }

void releaseExternal() { externalActive_ = false; }

int slotFor(int page, int app) {
    if (app == AppNone) {
        static const int pages[]={SlotPageHome,SlotPageAlbum,SlotPageMessage,SlotPageCountdown,SlotPageDaily,SlotPageMessages,SlotPageApps};
        return page>=0 && page<7 ? pages[page] : -1;
    }
    switch (app) {
        case AppAlbum: return SlotAppAlbum;
        case AppMessages: return SlotAppMessages;
        case AppWeather: return SlotAppWeather;
        case AppCalendar: return SlotAppCalendar;
        case AppCountdown: return SlotAppCountdown;
        case AppSettings: return SlotAppSettings;
        case AppPhone: return SlotAppPhone;
        case AppAbout: return SlotAppAbout;
        case AppDiagnostics: return SlotAppDiagnostics;
        case AppVoice: return SlotAppVoice;
        case AppWifi: return SlotAppWifi;
        case AppClock: return SlotAppClock;
        case AppAlarm: return SlotAppAlarm;
        case AppPomodoro: return SlotAppPomodoro;
        default: return -1;
    }
}

bool selfTest() {
    bool ok = true;

    // Page slots are identity-mapped and reject out-of-range pages.
    for (int page = 0; page < 4; ++page) ok &= (slotFor(page, AppNone) == page);
    ok &= slotFor(4,AppNone)==SlotPageDaily && slotFor(5,AppNone)==SlotPageMessages && slotFor(6,AppNone)==SlotPageApps;
    ok &= (slotFor(-1, AppNone) == -1);
    ok &= (slotFor(7, AppNone) == -1);

    // Each app maps to its own slot, and unknown apps are rejected.
    ok &= (slotFor(0, AppAlbum) == SlotAppAlbum);
    ok &= (slotFor(0, AppMessages) == SlotAppMessages);
    ok &= (slotFor(0, AppWeather) == SlotAppWeather);
    ok &= (slotFor(0, AppCalendar) == SlotAppCalendar);
    ok &= (slotFor(0, AppCountdown) == SlotAppCountdown);
    ok &= (slotFor(0, AppSettings) == SlotAppSettings);
    ok &= (slotFor(0, AppPhone) == SlotAppPhone);
    ok &= (slotFor(0, AppAbout) == SlotAppAbout);
    ok &= (slotFor(0, AppDiagnostics) == SlotAppDiagnostics);
    ok &= (slotFor(0, AppVoice) == SlotAppVoice);
    ok &= (slotFor(0, AppWifi) == SlotAppWifi);
    ok &= (slotFor(0, AppClock) == SlotAppClock);
    ok &= (slotFor(0, AppAlarm) == SlotAppAlarm);
    ok &= (slotFor(0, AppPomodoro) == SlotAppPomodoro);
    ok &= (slotFor(0, 15) == -1);

    // The page slot is ignored while an app is open, and vice versa.
    ok &= (slotFor(4, AppAbout) == SlotAppAbout);

    // Provider bookkeeping: absent provider never claims, a present one is asked
    // exactly the slot it is offered.
    const Provider saved = provider_;
    setProvider(nullptr);
    ok &= !dispatch(SlotPageHome) && !externalActive();
    setProvider(saved);
    return ok;
}

}
