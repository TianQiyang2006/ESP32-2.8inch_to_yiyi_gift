#pragma once
#include "timer_engine.h"

namespace TimerService {
enum class Action { Start, Pause, Reset, Dismiss, Save, Replay };
struct Snapshot {
    TimerEngine::Engine engine;
    bool storageReady = false, writeFailed = false, ringing = false;
};
void begin();
// Called by the existing 100 ms background timer task; never touches LVGL.
void tick();
Snapshot snapshot();
bool act(Action action, const TimerEngine::Settings *settings = nullptr, const char *sessionName = nullptr);
bool busy();
bool ringing();
void preempt();
} // namespace TimerService
