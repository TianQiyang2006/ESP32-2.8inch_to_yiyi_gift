// Host regression: the per-App navigation origin table from src/nav_logic.h.
//
// The return model is frozen in 代码审查/V2实施-接口契约.md §4 and fixes audit
// P0-1: one (returnApp, returnPage) pair cannot represent page6 -> Settings ->
// Wi-Fi, so the second back press re-entered Settings. This test pins the pure
// table transform only; desk.cpp applies it. It deliberately avoids
// Arduino/LVGL so it runs in the same host bucket as optimization_regression.
#include "src/nav_logic.h"

#include <cstdint>
#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

bool check(const char *what, bool condition) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
    return condition;
}

// App index values from src/app_state.h. Repeated here so the host test stays free
// of the Arduino/LVGL headers; the last case asserts the table covers all of them.
constexpr int AppNone = -1;  // nav convention: -1 = App::None
constexpr int AppCalendar = 4;
constexpr int AppCountdown = 5;
constexpr int AppSettings = 6;
constexpr int AppPhone = 7;
constexpr int AppVoice = 10;
constexpr int AppWifi = 11;
constexpr int AppClock = 12;
constexpr int AppAlarm = 13;
constexpr int AppPomodoro = 14;
constexpr int AppTetris = 15;
constexpr int AppMusic = 16;
constexpr int AppTimer = 17;
constexpr int AppSnake = 18;
constexpr int AppJump = 19;
constexpr int AppSheep = 20;
constexpr int AppMiner = 21;
constexpr int AppPlane = 22;
constexpr int AppBreakout = 23;
constexpr int AppMines = 24;
constexpr int AppPin = 25;
constexpr int AppMemory = 26;
constexpr int AppDays = 27;
constexpr int AppRadio = 28;
constexpr int AppGames = 29;

bool expectOrigin(const char *what, const NavOrigin table[NavAppCount], int currentApp,
                  int wantApp, int wantPage) {
    int app = -2, page = -2;
    const bool ok = navResolve(table, currentApp, &app, &page);
    ++checks;
    if (!ok || app != wantApp || page != wantPage) {
        ++failures;
        std::printf("FAIL %s: resolved=%d app=%d page=%d (want %d,%d)\n", what, ok ? 1 : 0,
                    app, page, wantApp, wantPage);
        return false;
    }
    return true;
}

bool expectNoOrigin(const char *what, const NavOrigin table[NavAppCount], int currentApp) {
    int app = -2, page = -2;
    const bool ok = navResolve(table, currentApp, &app, &page);
    ++checks;
    if (ok) {
        ++failures;
        std::printf("FAIL %s: unexpectedly resolved to app=%d page=%d\n", what, app, page);
        return false;
    }
    return true;
}

// ---- cases ----------------------------------------------------------------

// A fresh table holds no origin: every App must fall back to the root page.
void freshTableHasNoOrigin() {
    NavOrigin table[NavAppCount] = {};
    for (int target = 0; target < NavAppCount; ++target) {
        const char *what = "fresh table has no origin";
        int app = -2, page = -2;
        ++checks;
        if (navResolve(table, target, &app, &page)) {
            ++failures;
            std::printf("FAIL %s: app %d resolved app=%d page=%d\n", what, target, app, page);
        }
    }
    // The sentinel layout is what makes "never recorded" distinguishable from a
    // genuine App::None origin: originApp may be -1, but originPage must be unset.
    check("fresh originApp sentinel", table[0].originApp == -1);
    check("fresh originPage sentinel", table[0].originPage == -1);
}

// First level: page 6 -> Settings records (App::None, 6).
void recordAndResolveFirstLevel() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppSettings, AppNone, 6);
    expectOrigin("page6 -> Settings", table, AppSettings, AppNone, 6);
}

// The recorded page must survive for every legal page index 0..6, and an illegal
// page must never be recorded (the firmware would truncate it to another view).
void originPageRangeIsPreserved() {
    for (int page = 0; page <= 6; ++page) {
        NavOrigin table[NavAppCount] = {};
        navRecord(table, AppClock, AppNone, page);
        int app = -2, got = -2;
        ++checks;
        if (!navResolve(table, AppClock, &app, &got) || app != AppNone || got != page) {
            ++failures;
            std::printf("FAIL origin page preserved: want page %d, got %d\n", page, got);
        }
    }
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppClock, AppNone, -1);
    navRecord(table, AppClock, AppNone, 7);
    expectNoOrigin("invalid origin page ignored", table, AppClock);
}

// Second level: page6 -> Settings -> Wi-Fi. Recording Wi-Fi must not disturb the
// Settings origin, which is the whole point of per-App slots (audit P0-1).
void secondLevelKeepsFirstLevelOrigin() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppSettings, AppNone, 6);   // page6 -> Settings
    navRecord(table, AppWifi, AppSettings, 6);   // Settings -> Wi-Fi
    expectOrigin("wi-fi returns to Settings", table, AppWifi, AppSettings, 6);
    expectOrigin("Settings origin untouched by Wi-Fi", table, AppSettings, AppNone, 6);
}

// The two back presses of page6 -> Settings -> Wi-Fi -> Settings -> page6.
void twoLevelBackWalk() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppSettings, AppNone, 6);
    navRecord(table, AppWifi, AppSettings, 6);
    int app = -2, page = -2;
    check("wi-fi resolves", navResolve(table, AppWifi, &app, &page));
    check("first back lands Settings", app == AppSettings && page == 6);
    // The firmware then behaves as if page 6 -> Settings had just happened, so the
    // Settings row still resolves; no self-loop on the second back.
    app = page = -2;
    check("Settings resolves to the root", navResolve(table, AppSettings, &app, &page));
    check("second back lands page 6", app == AppNone && page == 6);
}

// A same-App entry is a self origin and must never be recorded (navRecord guard).
void selfOriginNotRecorded() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppSettings, AppSettings, 3);
    expectNoOrigin("self origin is not recorded", table, AppSettings);
}

// A stale self entry already in the table (e.g. from a future caller bug) must be
// rejected by navResolve instead of re-entering the same App.
void injectedSelfOriginRejected() {
    NavOrigin table[NavAppCount] = {};
    table[AppSettings].originApp = (int8_t)AppSettings;
    table[AppSettings].originPage = 3;
    expectNoOrigin("stale self origin rejected", table, AppSettings);
}

// Apps that were never entered have no origin even when other slots are populated.
void unrecordedAppFallsBack() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppSettings, AppNone, 6);
    navRecord(table, AppWifi, AppSettings, 6);
    expectNoOrigin("unrecorded Calendar falls back", table, AppCalendar);
    expectNoOrigin("unrecorded Voice falls back", table, AppVoice);
}

// Re-entering the same App from a different place updates (never keeps) the origin.
void overwriteTakesLatestOrigin() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppSettings, AppNone, 6);  // Apps page
    expectOrigin("Settings from Apps", table, AppSettings, AppNone, 6);
    navRecord(table, AppSettings, AppNone, 0);  // home page entry
    expectOrigin("Settings re-entered from home", table, AppSettings, AppNone, 0);
}

// Home -> Countdown -> Calendar (via "choose date") and back twice. The confirm
// sub-state is a state transition in desk.cpp, so it never records Countdown again;
// this is what prevents the Calendar <-> Countdown ping-pong.
void calendarFlowHasNoSelfLoop() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppCountdown, AppNone, 0);      // home -> Countdown
    navRecord(table, AppCalendar, AppCountdown, 0);  // Countdown -> Calendar
    // The confirm page switches to App::Countdown without calling navRecord, so the
    // Countdown origin stays the home page.
    expectOrigin("Calendar back returns to Countdown", table, AppCalendar, AppCountdown, 0);
    expectOrigin("Countdown origin is still the home page", table, AppCountdown, AppNone, 0);
    // Confirm success sets app=Calendar directly; the Calendar origin therefore
    // resolves through the Countdown layer, never back to Calendar itself.
    int app = -2, page = -2;
    check("Calendar resolves after confirm", navResolve(table, AppCalendar, &app, &page));
    check("Calendar back does not self-loop", app != AppCalendar);
    check("Calendar back lands Countdown", app == AppCountdown && page == 0);
    app = page = -2;
    check("Countdown resolves home", navResolve(table, AppCountdown, &app, &page));
    check("Countdown back lands home", app == AppNone && page == 0);
}

// Apps page -> Calendar -> (confirm) -> Calendar keeps the Calendar origin it was
// entered with, so the next back press returns to page 6 (the inspect_device stress
// expectation) without an App self-loop.
void calendarConfirmFromAppsPage() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppCalendar, AppNone, 6);  // page 6 -> Calendar
    // CalendarConfirm records nothing: App::Countdown is a sub-state here.
    expectOrigin("Calendar keeps Apps page origin", table, AppCalendar, AppNone, 6);
    expectNoOrigin("confirm sub-state has no origin", table, AppCountdown);
}

// Every App slot is independent: filling all of them and overwriting one leaves the
// others intact.
void allSlotsAreIndependent() {
    NavOrigin table[NavAppCount] = {};
    for (int target = 0; target < NavAppCount; ++target) {
        navRecord(table, target, AppNone, target % 7);
    }
    for (int target = 0; target < NavAppCount; ++target) {
        int app = -2, page = -2;
        ++checks;
        if (!navResolve(table, target, &app, &page) || app != AppNone || page != target % 7) {
            ++failures;
            std::printf("FAIL all slots independent: app %d resolved app=%d page=%d\n", target,
                        app, page);
        }
    }
    navRecord(table, AppWifi, AppSettings, 2);
    expectOrigin("overwritten Wi-Fi", table, AppWifi, AppSettings, 2);
    expectOrigin("Settings untouched by Wi-Fi overwrite", table, AppSettings, AppNone,
                 AppSettings % 7);
}

// Out-of-range App indexes are ignored by both functions (the caller validates App
// values, but a corrupt table must not make back() jump to garbage).
void invalidIndexesIgnored() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, -1, AppNone, 3);
    navRecord(table, NavAppCount, AppNone, 3);
    navRecord(table, AppClock, -2, 3);
    navRecord(table, AppClock, NavAppCount, 3);
    expectNoOrigin("out-of-range targets ignored", table, AppClock);
    int app = -2, page = -2;
    check("resolve rejects app -1", !navResolve(table, -1, &app, &page));
    check("resolve rejects app >= NavAppCount", !navResolve(table, NavAppCount, &app, &page));
    check("resolve rejects null outputs", !navResolve(table, AppClock, nullptr, &page));
    check("resolve rejects null table", !navResolve(nullptr, AppClock, &app, &page));
    navRecord(nullptr, AppClock, AppNone, 3);  // must not crash
    check("record tolerates null table", true);
}

// App::None is a legal origin (it means "one of the seven pages"), not "missing".
void appNoneIsAValidOrigin() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppClock, AppNone, 0);  // home clock card
    expectOrigin("clock returns to home", table, AppClock, AppNone, 0);
    navRecord(table, AppVoice, AppNone, 6);  // Apps page voice tile
    expectOrigin("voice returns to Apps page", table, AppVoice, AppNone, 6);
}

// Existing game IDs stay stable; all added games have independent slots.
void tableCoversEveryApp() {
    check("Snake follows Timer without shifting existing app IDs", AppSnake == AppTimer + 1);
    check("Jump follows Snake without shifting existing app IDs", AppJump == AppSnake + 1);
    check("Sheep follows Jump without shifting existing app IDs", AppSheep == AppJump + 1);
    check("Miner follows Sheep without shifting existing app IDs", AppMiner == AppSheep + 1);
    check("Plane follows Miner without shifting existing app IDs", AppPlane == AppMiner + 1);
    check("Breakout follows Plane without shifting existing app IDs", AppBreakout == AppPlane + 1);
    check("Mines follows Breakout without shifting existing app IDs", AppMines == AppBreakout + 1);
    check("Pin follows Mines without shifting existing app IDs", AppPin == AppMines + 1);
    check("Memory follows Pin without shifting existing app IDs", AppMemory == AppPin + 1);
    check("Days follows Memory without shifting existing app IDs", AppDays == AppMemory + 1);
    check("Radio app appends after Days", AppRadio == AppDays + 1);
    check("Games app appends after Radio", AppGames == AppRadio + 1);
    check("NavAppCount ends after Games", NavAppCount == AppGames + 1);
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppAlarm, AppNone, 6);
    navRecord(table, AppPomodoro, AppNone, 6);
    navRecord(table, AppTetris, AppNone, 6);
    navRecord(table, AppMusic, AppNone, 6);
    navRecord(table, AppTimer, AppNone, 6);
    navRecord(table, AppSnake, AppNone, 6);
    navRecord(table, AppJump, AppNone, 6);
    navRecord(table, AppSheep, AppNone, 6);
    navRecord(table, AppMiner, AppNone, 6);
    navRecord(table, AppPlane, AppNone, 6);
    navRecord(table, AppBreakout, AppNone, 6);
    navRecord(table, AppMines, AppNone, 6);
    navRecord(table, AppPin, AppNone, 6);
    navRecord(table, AppMemory, AppNone, 6);
    navRecord(table, AppDays, AppNone, 6);
    navRecord(table, AppRadio, AppNone, 6);
    navRecord(table, AppGames, AppNone, 6);
    expectOrigin("alarm has an independent app origin", table, AppAlarm, AppNone, 6);
    expectOrigin("pomodoro has an independent app origin", table, AppPomodoro, AppNone, 6);
    expectOrigin("tetris has an independent app origin", table, AppTetris, AppNone, 6);
    expectOrigin("music has an independent app origin", table, AppMusic, AppNone, 6);
    expectOrigin("timer has an independent app origin", table, AppTimer, AppNone, 6);
    expectOrigin("snake has an independent app origin", table, AppSnake, AppNone, 6);
    expectOrigin("jump has an independent app origin", table, AppJump, AppNone, 6);
    expectOrigin("sheep has an independent app origin", table, AppSheep, AppNone, 6);
    expectOrigin("miner has an independent app origin", table, AppMiner, AppNone, 6);
    expectOrigin("plane has an independent app origin", table, AppPlane, AppNone, 6);
    expectOrigin("breakout has an independent app origin", table, AppBreakout, AppNone, 6);
    expectOrigin("mines has an independent app origin", table, AppMines, AppNone, 6);
    expectOrigin("pin has an independent app origin", table, AppPin, AppNone, 6);
    expectOrigin("memory has an independent app origin", table, AppMemory, AppNone, 6);
    expectOrigin("days has an independent app origin", table, AppDays, AppNone, 6);
    expectOrigin("radio has an independent app origin", table, AppRadio, AppNone, 6);
    expectOrigin("games has an independent app origin", table, AppGames, AppNone, 6);
    navRecord(table, NavAppCount, AppNone, 6);
    expectNoOrigin("out-of-range target cannot resolve", table, NavAppCount);
    navRecord(table, AppTetris, NavAppCount, 6);
    expectOrigin("out-of-range origin cannot overwrite Tetris", table, AppTetris, AppNone, 6);
    table[AppTimer].originApp = (int8_t)NavAppCount;
    expectNoOrigin("out-of-range stored origin is rejected", table, AppTimer);
    navRecord(table, AppTimer, AppNone, 6);
    navRecord(table, NavAppCount - 1, AppNone, 6);
    int app = -2, page = -2;
    check("last App slot resolves",
          navResolve(table, NavAppCount - 1, &app, &page) && app == AppNone && page == 6);
}

void snakeOriginDoesNotOverwriteExistingApps() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppTetris, AppNone, 6);
    navRecord(table, AppTimer, AppNone, 6);
    navRecord(table, AppSnake, AppNone, 6);
    navRecord(table, AppSnake, AppSettings, 1);
    expectOrigin("snake remembers its latest origin", table, AppSnake, AppSettings, 1);
    expectOrigin("snake does not alter Tetris origin", table, AppTetris, AppNone, 6);
    expectOrigin("snake does not alter Timer origin", table, AppTimer, AppNone, 6);
    navRecord(table, AppSnake, AppSnake, 1);
    expectOrigin("snake self entry preserves valid origin", table, AppSnake, AppSettings, 1);
}

void jumpOriginDoesNotOverwriteExistingGames() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppTetris, AppNone, 6);
    navRecord(table, AppSnake, AppNone, 6);
    navRecord(table, AppJump, AppNone, 6);
    expectOrigin("jump returns to Apps root", table, AppJump, AppNone, 6);
    navRecord(table, AppJump, AppSettings, 1);
    expectOrigin("jump remembers its latest origin", table, AppJump, AppSettings, 1);
    expectOrigin("jump does not alter Tetris origin", table, AppTetris, AppNone, 6);
    expectOrigin("jump does not alter Snake origin", table, AppSnake, AppNone, 6);
    navRecord(table, AppJump, AppJump, 1);
    expectOrigin("jump self entry preserves valid origin", table, AppJump, AppSettings, 1);
}

void sheepOriginDoesNotOverwriteExistingGames() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppTetris, AppNone, 6);
    navRecord(table, AppSnake, AppNone, 6);
    navRecord(table, AppJump, AppNone, 6);
    navRecord(table, AppSheep, AppNone, 6);
    expectOrigin("sheep returns to Apps root", table, AppSheep, AppNone, 6);
    navRecord(table, AppSheep, AppSettings, 1);
    expectOrigin("sheep remembers its latest origin", table, AppSheep, AppSettings, 1);
    expectOrigin("sheep does not alter Tetris origin", table, AppTetris, AppNone, 6);
    expectOrigin("sheep does not alter Snake origin", table, AppSnake, AppNone, 6);
    expectOrigin("sheep does not alter Jump origin", table, AppJump, AppNone, 6);
    navRecord(table, AppSheep, AppSheep, 1);
    expectOrigin("sheep self entry preserves valid origin", table, AppSheep, AppSettings, 1);
}

void lastTwoGamesKeepNestedReturnsIndependent() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppSheep, AppNone, 6);
    navRecord(table, AppMiner, AppNone, 6);
    navRecord(table, AppPlane, AppMiner, 6);
    expectOrigin("plane nested return reaches miner", table, AppPlane, AppMiner, 6);
    expectOrigin("miner returns to Apps after plane", table, AppMiner, AppNone, 6);
    expectOrigin("last two games preserve sheep return", table, AppSheep, AppNone, 6);
    navRecord(table, AppPlane, AppNone, 2);
    expectOrigin("plane re-entry replaces nested origin", table, AppPlane, AppNone, 2);
    expectOrigin("plane re-entry preserves miner origin", table, AppMiner, AppNone, 6);
    navRecord(table, AppMiner, AppMiner, 2);
    navRecord(table, AppPlane, AppPlane, 2);
    expectOrigin("miner self-entry preserves Apps return", table, AppMiner, AppNone, 6);
    expectOrigin("plane self-entry preserves latest return", table, AppPlane, AppNone, 2);
}

void fourNewGamesKeepEveryOriginIndependent() {
    const int oldGames[] = {AppTetris, AppSnake, AppJump, AppSheep, AppMiner, AppPlane};
    const int newGames[] = {AppBreakout, AppMines, AppPin, AppMemory};
    NavOrigin table[NavAppCount] = {};
    for (int target : oldGames) navRecord(table, target, AppNone, 6);
    for (int target : newGames) navRecord(table, target, AppNone, 6);
    for (int target : newGames) {
        navRecord(table, target, AppSettings, 1);
        expectOrigin("new game remembers latest origin", table, target, AppSettings, 1);
        for (int other : oldGames)
            expectOrigin("new game preserves each existing game origin", table, other, AppNone, 6);
        for (int other : newGames) if (other != target)
            expectOrigin("new game preserves each sibling origin", table, other, AppNone, 6);
        navRecord(table, target, target, 2);
        expectOrigin("new game self entry preserves valid origin", table, target, AppSettings, 1);
        navRecord(table, target, AppNone, 6);
    }
}

void fourNewGamesNestedBackWalk() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppBreakout, AppNone, 6);
    navRecord(table, AppMines, AppBreakout, 6);
    navRecord(table, AppPin, AppMines, 6);
    navRecord(table, AppMemory, AppPin, 6);
    expectOrigin("memory nested back reaches pin", table, AppMemory, AppPin, 6);
    expectOrigin("pin nested back reaches mines", table, AppPin, AppMines, 6);
    expectOrigin("mines nested back reaches breakout", table, AppMines, AppBreakout, 6);
    expectOrigin("breakout final back reaches launcher", table, AppBreakout, AppNone, 6);
    navRecord(table, AppMemory, AppNone, 2);
    expectOrigin("memory re-entry replaces its nested origin", table, AppMemory, AppNone, 2);
    expectOrigin("memory re-entry preserves nested pin origin", table, AppPin, AppMines, 6);
    expectOrigin("memory re-entry preserves nested mines origin", table, AppMines, AppBreakout, 6);
    navRecord(table, AppMines, AppSettings, 5);
    expectOrigin("mines re-entry replaces its nested origin", table, AppMines, AppSettings, 5);
    expectOrigin("mines re-entry preserves memory origin", table, AppMemory, AppNone, 2);
    expectOrigin("mines re-entry preserves breakout launcher origin", table, AppBreakout, AppNone, 6);
    table[AppMemory].originApp = (int8_t)NavAppCount;
    expectNoOrigin("new last game rejects out-of-range stored origin", table, AppMemory);
    expectOrigin("corrupt last origin does not affect neighbouring game", table, AppPin, AppMines, 6);
}

struct Case {
    const char *name;
    void (*run)();
};

void daysPhoneReturnKeepsLauncherOrigin() {
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppDays, AppNone, 6);
    navRecord(table, AppPhone, AppDays, 6);
    expectOrigin("phone returns to days", table, AppPhone, AppDays, 6);
    expectOrigin("days still returns to launcher after phone", table, AppDays, AppNone, 6);
    navRecord(table, AppDays, AppDays, 6);
    expectOrigin("days self re-entry preserves launcher origin", table, AppDays, AppNone, 6);
    navRecord(table, AppPhone, AppSettings, 6);
    expectOrigin("phone re-entry records its latest caller", table, AppPhone, AppSettings, 6);
    expectOrigin("phone re-entry leaves days origin intact", table, AppDays, AppNone, 6);
}

// Every game returns to the shared Games app, which keeps its own Apps-page
// origin. Visiting another game or re-entering the current game cannot replace it.
void sharedGamesLauncherBackWalk() {
    const int games[] = {AppTetris, AppSnake, AppJump, AppSheep, AppMiner, AppPlane,
                         AppBreakout, AppMines, AppPin, AppMemory};
    NavOrigin table[NavAppCount] = {};
    navRecord(table, AppGames, AppNone, 6);
    for (int game : games) {
        navRecord(table, game, AppGames, 6);
        expectOrigin("game back returns to shared Games launcher", table, game, AppGames, 6);
        expectOrigin("Games keeps Apps-page origin after opening a game", table, AppGames, AppNone, 6);
        navRecord(table, game, game, 6);
        expectOrigin("game self entry preserves Games return", table, game, AppGames, 6);
    }
    for (int game : games)
        expectOrigin("opening sibling games preserves every Games return", table, game, AppGames, 6);
    navRecord(table, AppGames, AppGames, 6);
    expectOrigin("Games self entry preserves Apps-page return", table, AppGames, AppNone, 6);
    navRecord(table, AppGames, AppNone, 0);
    expectOrigin("Games records latest desktop entry independently", table, AppGames, AppNone, 0);
    for (int game : games)
        expectOrigin("Games re-entry preserves each game's return", table, game, AppGames, 6);
}

const Case cases[] = {
    {"fresh table has no origin", freshTableHasNoOrigin},
    {"record and resolve first level", recordAndResolveFirstLevel},
    {"origin page range preserved", originPageRangeIsPreserved},
    {"second level keeps first level origin", secondLevelKeepsFirstLevelOrigin},
    {"two-level back walk", twoLevelBackWalk},
    {"self origin not recorded", selfOriginNotRecorded},
    {"injected self origin rejected", injectedSelfOriginRejected},
    {"unrecorded app falls back", unrecordedAppFallsBack},
    {"overwrite takes latest origin", overwriteTakesLatestOrigin},
    {"calendar flow has no self loop", calendarFlowHasNoSelfLoop},
    {"calendar confirm from Apps page", calendarConfirmFromAppsPage},
    {"all slots are independent", allSlotsAreIndependent},
    {"invalid indexes ignored", invalidIndexesIgnored},
    {"app None is a valid origin", appNoneIsAValidOrigin},
    {"table covers every app", tableCoversEveryApp},
    {"snake origin keeps existing apps independent", snakeOriginDoesNotOverwriteExistingApps},
    {"jump origin keeps existing games independent", jumpOriginDoesNotOverwriteExistingGames},
    {"sheep origin keeps existing games independent", sheepOriginDoesNotOverwriteExistingGames},
    {"last two games keep nested returns independent", lastTwoGamesKeepNestedReturnsIndependent},
    {"four new games keep every origin independent", fourNewGamesKeepEveryOriginIndependent},
    {"four new games nested back walk", fourNewGamesNestedBackWalk},
    {"days phone return keeps launcher origin", daysPhoneReturnKeepsLauncherOrigin},
    {"shared Games launcher back walk", sharedGamesLauncherBackWalk},
};

}  // namespace

int main() {
    const int total = (int)(sizeof(cases) / sizeof(cases[0]));
    int passed = 0;
    for (const Case &item : cases) {
        const int before = failures;
        item.run();
        if (failures == before) {
            ++passed;
            std::printf("PASS %s\n", item.name);
        } else {
            std::printf("FAIL %s\n", item.name);
        }
    }
    std::printf("NAV ORIGIN: %d/%d cases passed, %d checks, %d failures\n", passed, total,
                checks, failures);
    return failures == 0 ? 0 : 1;
}
