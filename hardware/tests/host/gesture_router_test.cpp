// Host regression: the pure gesture -> GestureIntent router from desk_logic.h.
//
// The rules are frozen in 代码审查/V2实施-接口契约.md §3. This test deliberately
// stays free of Arduino/LVGL (only desk_logic.h + the C++ standard library) so it
// runs in the same host bucket as optimization_regression. It pins the boundary
// values that the device cannot reproduce reliably: dx 45/46, x0 19/20 and the
// |dy| <= min(20, |dx|/2) cap at 20/21.
#include "desk_logic.h"

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

bool expectIntent(const char *what, GestureIntent got, GestureIntent want) {
    ++checks;
    if (got != want) {
        ++failures;
        std::printf("FAIL %s: got %d, want %d\n", what, (int)got, (int)want);
        return false;
    }
    return true;
}

GestureContext context(bool transitioning, bool inputActive, bool appOpen, bool canGoBack,
                       bool gameOwnsGestures = false) {
    GestureContext result{};
    result.transitioning = transitioning;
    result.inputActive = inputActive;
    result.appOpen = appOpen;
    result.canGoBack = canGoBack;
    result.gameOwnsGestures = gameOwnsGestures;
    return result;
}

const GestureContext root = context(false, false, false, false);      // 7-page root
const GestureContext appBack = context(false, false, true, true);     // app with an origin
const GestureContext appNoBack = context(false, false, true, false);  // app, no level left
const GestureContext input = context(false, true, false, false);      // dragging a slider

// Every tap (dx within +-45, whatever dy is) must stay out of the swipe router.
void tapNeverRoutes() {
    expectIntent("tap dx=0", gestureIntent(100, 120, 0, 0, root), GestureIntent::None);
    expectIntent("tap dx=10", gestureIntent(100, 120, 10, 3, root), GestureIntent::None);
    expectIntent("tap dx=-45", gestureIntent(100, 120, -45, 0, root), GestureIntent::None);
    expectIntent("tap dx=45 dy=40", gestureIntent(100, 120, 45, 40, root), GestureIntent::None);
    expectIntent("tap on edge x0=5", gestureIntent(5, 120, 45, 0, appBack), GestureIntent::None);
}

// touch.cpp: abs(dx) > 45 is a swipe, abs(dx) == 45 stays a tap.
void horizontalThreshold() {
    expectIntent("dx=45 is not a swipe", gestureIntent(100, 120, 45, 0, root), GestureIntent::None);
    expectIntent("dx=46 is a swipe", gestureIntent(100, 120, 46, 0, root), GestureIntent::PageNext);
    expectIntent("dx=-45 is not a swipe", gestureIntent(100, 120, -45, 0, root), GestureIntent::None);
    expectIntent("dx=-46 is a swipe", gestureIntent(100, 120, -46, 0, root), GestureIntent::PagePrev);
}

// Edge zone is the left 20 px: x0 == 19 is inside, x0 == 20 is not.
void edgeXBoundary() {
    expectIntent("x0=0 edge right -> Back", gestureIntent(0, 120, 46, 0, appBack), GestureIntent::Back);
    expectIntent("x0=19 edge right -> Back", gestureIntent(19, 120, 46, 0, appBack), GestureIntent::Back);
    expectIntent("x0=20 not edge -> app no action", gestureIntent(20, 120, 46, 0, appBack), GestureIntent::None);
    expectIntent("x0=20 root page still advances", gestureIntent(20, 120, 46, 0, root), GestureIntent::PageNext);
}

// Root page edge back has nowhere to go, so it falls through to the normal page
// turn instead of swallowing the gesture.
void edgeRootFallthrough() {
    expectIntent("root edge dx=46 -> PageNext", gestureIntent(0, 120, 46, 0, root), GestureIntent::PageNext);
    expectIntent("root edge dx=200 -> PageNext", gestureIntent(5, 120, 200, 0, root), GestureIntent::PageNext);
    expectIntent("root edge far from corner -> PageNext", gestureIntent(19, 200, 60, 0, root), GestureIntent::PageNext);
}

void edgeBackWhenCanGoBack() {
    expectIntent("edge right with origin -> Back", gestureIntent(5, 120, 80, 0, appBack), GestureIntent::Back);
    expectIntent("edge right detail level -> Back", gestureIntent(10, 120, 60, 0, context(false, false, false, true)), GestureIntent::Back);
}

// A dragged slider/keyboard owns the touch: an edge swipe must not leave the page.
void edgeInputActiveBlocksBack() {
    expectIntent("input active blocks edge Back", gestureIntent(5, 120, 80, 0, context(false, true, true, true)), GestureIntent::None);
    expectIntent("input active blocks root edge turn", gestureIntent(0, 120, 80, 0, input), GestureIntent::None);
}

// |dy| <= min(20, |dx|/2): at the cap an edge right swipe still goes Back.
void edgeDyLimitInside() {
    expectIntent("dy=20 within cap -> Back", gestureIntent(5, 120, 46, 20, appBack), GestureIntent::Back);
    expectIntent("dy=-20 within cap -> Back", gestureIntent(5, 120, 46, -20, appBack), GestureIntent::Back);
    expectIntent("dy=0 -> Back", gestureIntent(5, 120, 46, 0, appBack), GestureIntent::Back);
}

// One pixel past the cap is a plain diagonal swipe, not an edge back.
void edgeDyLimitOutsideFallsThrough() {
    expectIntent("dy=21 over cap -> app None", gestureIntent(5, 120, 46, 21, appBack), GestureIntent::None);
    expectIntent("dy=-21 over cap -> app None", gestureIntent(5, 120, 46, -21, appBack), GestureIntent::None);
    expectIntent("dy=21 on root still advances", gestureIntent(5, 120, 60, 21, root), GestureIntent::PageNext);
}

// The cap is 20 even when |dx|/2 would allow more.
void dyCapIsTwenty() {
    expectIntent("dx=100 dy=20 -> Back", gestureIntent(5, 120, 100, 20, appBack), GestureIntent::Back);
    expectIntent("dx=100 dy=21 -> None", gestureIntent(5, 120, 100, 21, appBack), GestureIntent::None);
}

// The extra edge restriction only applies to right swipes.
void edgeLeftSwipeDirection() {
    expectIntent("root edge left -> PagePrev", gestureIntent(5, 120, -60, 0, root), GestureIntent::PagePrev);
    expectIntent("app edge left -> None", gestureIntent(5, 120, -60, 0, appBack), GestureIntent::None);
    expectIntent("input edge left -> None", gestureIntent(5, 120, -60, 0, input), GestureIntent::None);
}

void transitioningBlocksEdgeBack() {
    const GestureContext busy = context(true, false, true, true);
    expectIntent("transitioning + edge + origin -> None", gestureIntent(0, 120, 80, 0, busy), GestureIntent::None);
    expectIntent("transitioning + edge + input -> None", gestureIntent(0, 120, 80, 0, context(true, true, true, true)), GestureIntent::None);
}

void transitioningBlocksPageSwipe() {
    const GestureContext busy = context(true, false, false, false);
    expectIntent("transitioning root right -> None", gestureIntent(160, 120, 80, 0, busy), GestureIntent::None);
    expectIntent("transitioning root left -> None", gestureIntent(160, 120, -80, 0, busy), GestureIntent::None);
}

// Same 3:2 proportion rule as touch.cpp: 60/40 is exactly on the line (None).
void horizontalProportion() {
    expectIntent("dx=60 dy=40 exactly 3:2 -> None", gestureIntent(100, 120, 60, 40, root), GestureIntent::None);
    expectIntent("dx=61 dy=40 above 3:2 -> PageNext", gestureIntent(100, 120, 61, 40, root), GestureIntent::PageNext);
    expectIntent("dx=50 dy=100 vertical drag -> None", gestureIntent(100, 120, 50, 100, root), GestureIntent::None);
    expectIntent("dx=100 dy=40 mostly horizontal -> PageNext", gestureIntent(100, 120, 100, 40, root), GestureIntent::PageNext);
}

// Clock decision A: an open app ignores non-edge swipes (no accidental dismiss).
void nonEdgeAppNoAction() {
    expectIntent("app non-edge right -> None", gestureIntent(100, 120, 200, 0, appBack), GestureIntent::None);
    expectIntent("app non-edge left -> None", gestureIntent(100, 120, -200, 0, appBack), GestureIntent::None);
    expectIntent("app non-edge right, no origin -> None", gestureIntent(100, 120, 200, 0, appNoBack), GestureIntent::None);
}

void nonEdgeInputNoAction() {
    expectIntent("input non-edge right -> None", gestureIntent(160, 120, 80, 0, input), GestureIntent::None);
    expectIntent("input non-edge left -> None", gestureIntent(160, 120, -80, 0, input), GestureIntent::None);
}

void nonEdgeRootPages() {
    expectIntent("root non-edge right -> PageNext", gestureIntent(160, 120, 80, 0, root), GestureIntent::PageNext);
    expectIntent("root non-edge left -> PagePrev", gestureIntent(160, 120, -80, 0, root), GestureIntent::PagePrev);
}

// Swipe right advances through the seven pages; the router's direction wording
// must agree with nextPage() for every page (including the 6 -> 0 wrap).
void sevenPageForwardCycle() {
    for (int page = 0; page < 7; ++page) {
        char label[80];
        std::snprintf(label, sizeof(label), "page %d right -> page %d", page, (page + 1) % 7);
        check(label, DeskLogic::nextPage((uint8_t)page, 46) == (page + 1) % 7);
        check("forward swipe routes to PageNext", gestureIntent(160, 120, 46, 0, root) == GestureIntent::PageNext);
    }
}

void sevenPageBackwardCycle() {
    for (int page = 0; page < 7; ++page) {
        char label[80];
        std::snprintf(label, sizeof(label), "page %d left -> page %d", page, (page + 6) % 7);
        check(label, DeskLogic::nextPage((uint8_t)page, -46) == (page + 6) % 7);
        check("backward swipe routes to PagePrev", gestureIntent(160, 120, -46, 0, root) == GestureIntent::PagePrev);
    }
}

void pageCycleBoundary() {
    check("page 6 right wraps to 0", DeskLogic::nextPage(6, 46) == 0);
    check("page 0 left wraps to 6", DeskLogic::nextPage(0, -46) == 6);
    expectIntent("page 6 edge fallthrough -> PageNext", gestureIntent(0, 120, 46, 0, root), GestureIntent::PageNext);
}

// Rule ordering: transitioning wins over everything else.
void priorityTransitionFirst() {
    expectIntent("transition + input + origin -> None", gestureIntent(0, 120, 80, 0, context(true, true, true, true)), GestureIntent::None);
    expectIntent("transition + app no origin -> None", gestureIntent(0, 120, 80, 0, context(true, false, true, false)), GestureIntent::None);
}

// Rule ordering: inputActive is checked before canGoBack/appOpen.
void priorityInputOverBack() {
    expectIntent("input + origin edge -> None", gestureIntent(0, 120, 80, 0, context(false, true, true, true)), GestureIntent::None);
    expectIntent("input + no origin non-edge -> None", gestureIntent(160, 120, 80, 0, context(false, true, false, false)), GestureIntent::None);
}

void edgeLargeRightSwipe() {
    expectIntent("edge dx=200 -> Back", gestureIntent(5, 120, 200, 0, appBack), GestureIntent::Back);
    expectIntent("edge dx=46 -> Back", gestureIntent(5, 120, 46, 0, appBack), GestureIntent::Back);
    expectIntent("edge dx=45 -> None", gestureIntent(5, 120, 45, 0, appBack), GestureIntent::None);
}

// The same travel means different things on a root page and inside an app.
void rootVersusAppSameSwipe() {
    expectIntent("root non-edge right -> PageNext", gestureIntent(160, 120, 80, 0, root), GestureIntent::PageNext);
    expectIntent("app non-edge right -> None", gestureIntent(160, 120, 80, 0, appBack), GestureIntent::None);
    expectIntent("root non-edge left -> PagePrev", gestureIntent(160, 120, -80, 0, root), GestureIntent::PagePrev);
    expectIntent("app non-edge left -> None", gestureIntent(160, 120, -80, 0, appBack), GestureIntent::None);
}

// A page detail (reading quote/poem, daily mood/water/wood) is closed by the same
// horizontal swipe in either direction, and the same gesture never also flips the
// page (legacy GiftUI::back()-first behaviour).
void detailSwipeClosesFirst() {
    const GestureContext detail = context(false, false, false, true);  // root page, detail open
    expectIntent("detail non-edge right -> Back", gestureIntent(160, 120, 80, 0, detail), GestureIntent::Back);
    expectIntent("detail non-edge left -> Back", gestureIntent(160, 120, -80, 0, detail), GestureIntent::Back);
    expectIntent("detail edge right -> Back", gestureIntent(5, 120, 80, 0, detail), GestureIntent::Back);
    // App pages keep the Clock decision: a non-edge swipe never acts even though
    // an origin exists.
    expectIntent("app canGoBack non-edge right -> None", gestureIntent(160, 120, 80, 0, appBack), GestureIntent::None);
    // Root with nothing to dismiss still pages.
    expectIntent("root no detail non-edge right -> PageNext", gestureIntent(160, 120, 80, 0, root), GestureIntent::PageNext);
}

void snakeOwnsDirectionalGestures() {
    const auto snake = context(false, false, true, true, true);
    expectIntent("snake left-edge right swipe stays in game", gestureIntent(0, 100, 100, 0, snake), GestureIntent::None);
    expectIntent("snake edge boundary stays in game", gestureIntent(19, 100, 46, 0, snake), GestureIntent::None);
    expectIntent("snake inner right swipe stays in game", gestureIntent(100, 100, 80, 0, snake), GestureIntent::None);
    expectIntent("snake left swipe stays in game", gestureIntent(200, 100, -80, 0, snake), GestureIntent::None);
    expectIntent("snake upward swipe stays in game", gestureIntent(100, 150, 0, -80, snake), GestureIntent::None);
    expectIntent("snake downward swipe stays in game", gestureIntent(100, 50, 0, 80, snake), GestureIntent::None);
    expectIntent("snake diagonal edge swipe stays in game", gestureIntent(0, 50, 100, 21, snake), GestureIntent::None);
    // Explicit ownership must also suppress root fallthrough when the game has
    // no back level, rather than relying on canGoBack to block navigation.
    expectIntent("snake no-back edge cannot advance root", gestureIntent(0, 100, 100, 0,
                 context(false, false, true, false, true)), GestureIntent::None);
    expectIntent("other apps retain edge Back", gestureIntent(0, 100, 100, 0, appBack), GestureIntent::Back);
    expectIntent("root retains edge page navigation", gestureIntent(0, 100, 100, 0, root), GestureIntent::PageNext);
}

void jumpOwnsChargeTouchSequence() {
    const auto jump = context(false, false, true, true, true);
    expectIntent("jump hold has no global action", gestureIntent(100, 100, 0, 0, jump), GestureIntent::None);
    expectIntent("jump left-edge drag cannot return", gestureIntent(0, 100, 100, 0, jump), GestureIntent::None);
    expectIntent("jump edge boundary drag cannot return", gestureIntent(19, 100, 46, 0, jump), GestureIntent::None);
    expectIntent("jump right drag cannot page", gestureIntent(150, 100, 80, 0, jump), GestureIntent::None);
    expectIntent("jump left drag cannot page", gestureIntent(150, 100, -80, 0, jump), GestureIntent::None);
    expectIntent("jump vertical drag cannot page", gestureIntent(150, 100, 0, -80, jump), GestureIntent::None);
    expectIntent("jump no-back edge cannot advance root", gestureIntent(0, 100, 80, 0,
                 context(false, false, true, false, true)), GestureIntent::None);
    expectIntent("normal app edge back remains available", gestureIntent(0, 100, 80, 0, appBack), GestureIntent::Back);
}

void sheepOwnsTileTouchSequence() {
    const auto sheep = context(false, false, true, true, true);
    expectIntent("sheep tile tap has no global action", gestureIntent(100, 100, 0, 0, sheep), GestureIntent::None);
    expectIntent("sheep left-edge drag cannot return", gestureIntent(0, 100, 100, 0, sheep), GestureIntent::None);
    expectIntent("sheep edge boundary drag cannot return", gestureIntent(19, 100, 46, 0, sheep), GestureIntent::None);
    expectIntent("sheep right drag cannot page", gestureIntent(150, 100, 80, 0, sheep), GestureIntent::None);
    expectIntent("sheep left drag cannot page", gestureIntent(150, 100, -80, 0, sheep), GestureIntent::None);
    expectIntent("sheep vertical drag cannot page", gestureIntent(150, 100, 0, -80, sheep), GestureIntent::None);
    expectIntent("sheep no-back edge cannot advance root", gestureIntent(0, 100, 80, 0,
                 context(false, false, true, false, true)), GestureIntent::None);
    expectIntent("normal app edge back remains available with sheep added", gestureIntent(0, 100, 80, 0, appBack), GestureIntent::Back);
}

void minerOwnsCastTouchSequence() {
    const auto miner = context(false, false, true, true, true);
    expectIntent("miner cast tap has no global action", gestureIntent(160, 120, 0, 0, miner), GestureIntent::None);
    expectIntent("miner left-edge drag cannot return", gestureIntent(0, 120, 100, 0, miner), GestureIntent::None);
    expectIntent("miner diagonal drag cannot page", gestureIntent(19, 120, 100, 30, miner), GestureIntent::None);
    expectIntent("miner top boundary drag cannot navigate", gestureIntent(120, 32, -100, -31, miner), GestureIntent::None);
    expectIntent("miner no-back edge stays in game", gestureIntent(0, 120, 80, 0,
                 context(false, false, true, false, true)), GestureIntent::None);
    expectIntent("other app back remains available after miner", gestureIntent(0, 120, 80, 0, appBack), GestureIntent::Back);
}

void planeOwnsRelativeDragSequence() {
    const auto plane = context(false, false, true, true, true);
    expectIntent("plane finger down stays in game", gestureIntent(140, 200, 0, 0, plane), GestureIntent::None);
    expectIntent("plane drag from left edge stays in game", gestureIntent(0, 200, 120, 0, plane), GestureIntent::None);
    expectIntent("plane horizontal drag stays in game", gestureIntent(180, 200, -160, 0, plane), GestureIntent::None);
    expectIntent("plane upward drag to header stays in game", gestureIntent(140, 160, 0, -160, plane), GestureIntent::None);
    expectIntent("plane downwards drag stays in game", gestureIntent(140, 40, 0, 190, plane), GestureIntent::None);
    expectIntent("plane no-back edge stays in game", gestureIntent(0, 120, 80, 0,
                 context(false, false, true, false, true)), GestureIntent::None);
    expectIntent("root paging remains available after plane", gestureIntent(0, 120, 80, 0, root), GestureIntent::PageNext);
}

void checkOwnedGameBoundary(const char *gameName) {
    struct Drag { const char *name; int16_t x, y, dx, dy; };
    const Drag samples[] = {
        {"stationary contact", 160, 120, 0, 0},
        {"left edge threshold", 0, 120, 46, 0},
        {"last edge pixel", 19, 120, 100, 0},
        {"first interior pixel", 20, 120, 100, 0},
        {"right edge reverse drag", 319, 120, -300, 0},
        {"bottom edge across board", 0, 239, 300, 0},
        {"board top release in header", 160, 32, -140, -32},
        {"diagonal edge release", 0, 220, 300, -180},
        {"vertical field travel", 160, 239, 0, -207},
    };
    char name[128];
    for (const auto &sample : samples) {
        std::snprintf(name, sizeof(name), "%s owns %s", gameName, sample.name);
        expectIntent(name, gestureIntent(sample.x, sample.y, sample.dx, sample.dy,
                     context(false, false, true, true, true)), GestureIntent::None);
    }
    std::snprintf(name, sizeof(name), "%s no-back edge cannot reach root", gameName);
    expectIntent(name, gestureIntent(0, 120, 100, 0,
                 context(false, false, true, false, true)), GestureIntent::None);
    std::snprintf(name, sizeof(name), "%s active contact cannot navigate", gameName);
    expectIntent(name, gestureIntent(19, 239, 100, 0,
                 context(false, true, true, true, true)), GestureIntent::None);
    std::snprintf(name, sizeof(name), "%s transition contact cannot navigate", gameName);
    expectIntent(name, gestureIntent(0, 120, 100, 0,
                 context(true, false, true, true, true)), GestureIntent::None);
    std::snprintf(name, sizeof(name), "%s preserves normal app edge return", gameName);
    expectIntent(name, gestureIntent(0, 120, 100, 0, appBack), GestureIntent::Back);
    std::snprintf(name, sizeof(name), "%s preserves normal root paging", gameName);
    expectIntent(name, gestureIntent(0, 120, 100, 0, root), GestureIntent::PageNext);
}
void breakoutOwnsPaddleTouch(){checkOwnedGameBoundary("breakout");}
void minesOwnsRevealAndFlagTouch(){checkOwnedGameBoundary("mines");}
void pinOwnsShotTouch(){checkOwnedGameBoundary("pin");}
void memoryOwnsCardTouch(){checkOwnedGameBoundary("memory");}

struct Case {
    const char *name;
    void (*run)();
};

const Case cases[] = {
    {"tap never routes", tapNeverRoutes},
    {"horizontal threshold 45/46", horizontalThreshold},
    {"edge x boundary 19/20", edgeXBoundary},
    {"edge root fallthrough", edgeRootFallthrough},
    {"edge back when canGoBack", edgeBackWhenCanGoBack},
    {"edge inputActive blocks back", edgeInputActiveBlocksBack},
    {"edge |dy| cap inside", edgeDyLimitInside},
    {"edge |dy| cap outside falls through", edgeDyLimitOutsideFallsThrough},
    {"edge |dy| cap is 20", dyCapIsTwenty},
    {"edge left swipe direction", edgeLeftSwipeDirection},
    {"transitioning blocks edge back", transitioningBlocksEdgeBack},
    {"transitioning blocks page swipe", transitioningBlocksPageSwipe},
    {"horizontal proportion 3:2", horizontalProportion},
    {"non-edge app no action", nonEdgeAppNoAction},
    {"non-edge input no action", nonEdgeInputNoAction},
    {"non-edge root pages", nonEdgeRootPages},
    {"seven page forward cycle", sevenPageForwardCycle},
    {"seven page backward cycle", sevenPageBackwardCycle},
    {"page cycle boundary 6<->0", pageCycleBoundary},
    {"priority transitioning first", priorityTransitionFirst},
    {"priority inputActive over back", priorityInputOverBack},
    {"edge large right swipe", edgeLargeRightSwipe},
    {"detail swipe closes first", detailSwipeClosesFirst},
    {"root versus app same swipe", rootVersusAppSameSwipe},
    {"snake owns directional gestures", snakeOwnsDirectionalGestures},
    {"jump owns charge touch sequence", jumpOwnsChargeTouchSequence},
    {"sheep owns tile touch sequence", sheepOwnsTileTouchSequence},
    {"miner owns cast touch sequence", minerOwnsCastTouchSequence},
    {"plane owns relative drag sequence", planeOwnsRelativeDragSequence},
    {"breakout owns paddle touch", breakoutOwnsPaddleTouch},
    {"mines owns reveal and flag touch", minesOwnsRevealAndFlagTouch},
    {"pin owns shot touch", pinOwnsShotTouch},
    {"memory owns card touch", memoryOwnsCardTouch},
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
    std::printf("GESTURE ROUTER: %d/%d cases passed, %d checks, %d failures\n",
                passed, total, checks, failures);
    return failures == 0 ? 0 : 1;
}
