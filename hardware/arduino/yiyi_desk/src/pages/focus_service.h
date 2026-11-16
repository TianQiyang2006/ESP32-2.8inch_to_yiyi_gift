#pragma once
#include "focus_engine.h"
namespace FocusService {
enum class Action { SaveAlarm, ToggleAlarm, DeleteAlarm, CancelSnooze, StopAlarm, SnoozeAlarm, Configure, Start, Pause, End, SkipRest, AlarmMasterVolume, PomodoroMasterVolume };
struct Snapshot {FocusEngine::Engine engine;bool storageReady=false,writeFailed=false,schedulerReady=false;};
void begin();
void poll();
uint64_t monotonic();
Snapshot snapshot();
bool act(Action action,unsigned index=0,const FocusEngine::Alarm *alarm=nullptr,const FocusEngine::Preferences *preferences=nullptr);
bool busy();
#ifdef GIFT_HOST_TEST
void debugSet(const FocusEngine::Engine &engine);
#endif
}
