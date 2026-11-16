#pragma once
#include <lvgl.h>

namespace GifClockPage {
void build(lv_obj_t *parent);
void refresh();
void cancel();
bool active();
bool playing();
unsigned selection();
unsigned frameIndex();
}

namespace PixelClockPage {
void build(lv_obj_t *parent);
void refresh();
void cancel();
bool active();
bool snakeActive();
// Deterministic entry point for the host animation checks.
void startSnake();
}

namespace FlipClockPage {
void build(lv_obj_t *parent);
void refresh();
void cancel();
bool active();
int movingDigits();
bool darkMode();
bool storageFailed();
}

namespace BarbarClockPage {
void build(lv_obj_t *parent);
void refresh();
void cancel();
bool active();
bool showSeconds();
bool storageFailed();
void createPreview(lv_obj_t *parent);
}

namespace FocusApps {
void begin();
void poll();
void buildAlarm(lv_obj_t *parent);
void buildPomodoro(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
}

namespace BreakoutEngine { struct Game; }
namespace BreakoutPage {
struct Status {
    const char *state;
    uint32_t score, best;
    uint8_t level, lives;
    uint16_t remaining;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
Status status();
uint32_t bestScore();
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
const BreakoutEngine::Game &debugGame();
#endif
}

namespace MinesEngine { struct Game; }
namespace MinesPage {
struct Status {
    const char *state;
    uint32_t score, best, elapsedMs;
    uint8_t remaining, flags;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
Status status();
uint32_t bestScore();
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
const MinesEngine::Game &debugGame();
#endif
}

namespace PinEngine { struct Game; }
namespace PinPage {
struct Status {
    const char *state;
    uint32_t score, best;
    uint8_t level, remaining, attached;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
Status status();
uint32_t bestScore();
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
const PinEngine::Game &debugGame();
#endif
}

namespace MemoryEngine { struct Game; }
namespace MemoryPage {
struct Status {
    const char *state;
    uint32_t score, best, elapsedMs;
    uint16_t moves;
    uint8_t level, pairs, totalPairs;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
Status status();
uint32_t bestScore();
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
const MemoryEngine::Game &debugGame();
#endif
}

namespace MinerEngine { struct Game; }
namespace MinerPage {
struct Status {
    const char *state;
    uint32_t score, best;
    uint16_t secondsLeft, target;
    uint8_t level, remaining;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
Status status();
uint32_t bestScore();
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
const MinerEngine::Game &debugGame();
#endif
}

namespace PlaneEngine { struct Game; }
namespace PlanePage {
struct Status {
    const char *state;
    uint32_t score, best;
    uint16_t kills;
    uint8_t lives, wave;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
Status status();
uint32_t bestScore();
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
const PlaneEngine::Game &debugGame();
#endif
}

namespace SheepEngine { struct Game; }
namespace SheepPage {
struct Status {
    const char *state;
    uint32_t score, best;
    uint16_t remaining;
    uint8_t level, trayCount;
    bool undoAvailable, shuffleAvailable;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
Status status();
uint32_t bestScore();
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
const SheepEngine::Game &debugGame();
#endif
}

namespace JumpPage {
struct Status {
    const char *state;
    uint32_t score, best, jumps, combo;
    uint16_t chargeMs, targetDistance, flightElapsedMs;
    uint8_t chargePercent;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
Status status();
uint32_t bestScore();
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
#endif
}

namespace SnakePage {
struct Status {
    const char *state;
    uint32_t score, best;
    uint16_t length, intervalMs;
    uint8_t speed;
    int8_t headX, headY;
    uint8_t direction;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
Status status();
uint32_t bestScore();
// Small vector icon; no bitmap or additional font resource is required.
void createIcon(lv_obj_t *parent, int x, int y);
#ifdef GIFT_HOST_TEST
void debugVariant(int which);
#endif
}

namespace TetrisPage {
// Telemetry returned by the device `status` command.
struct Status {
    const char *state;
    uint32_t score, best;
    uint16_t lines;
    uint8_t level;
};
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
Status status();
// Reads the stored best score without requiring the page to have been opened;
// used by the device `status` telemetry.
uint32_t bestScore();
// Host-renderer hook only (device code never calls it): freezes a deterministic
// screenshot scenario, 0 = fresh game.
void debugVariant(int which);
}

