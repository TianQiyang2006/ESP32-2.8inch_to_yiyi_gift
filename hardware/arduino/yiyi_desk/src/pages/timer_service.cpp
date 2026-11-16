#include "timer_service.h"
#include "timer_storage.h"
#include "focus_service.h"
#include "../audio_board.h"
#ifndef GIFT_HOST_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace TimerService {
namespace {
::Preferences store;
TimerStorage::Journal journal(store);
TimerEngine::Engine engine;
bool initialized = false, storageReady = false, writeFailed = false;
bool sounding = false, pending = false;
uint64_t ringUntil = 0;
uint32_t finishedRevision = 0;
#ifndef GIFT_HOST_TEST
SemaphoreHandle_t mutex = nullptr;
void lock() { if (mutex) xSemaphoreTake(mutex, portMAX_DELAY); }
void unlock() { if (mutex) xSemaphoreGive(mutex); }
#else
void lock() {}
void unlock() {}
#endif
void silence() {
    AudioBoard::stopRingtone(2, AudioBoard::Reminder::Timer);
    sounding = pending = false;
}
} // namespace
void begin() {
    if (initialized) return;
    initialized = true;
#ifndef GIFT_HOST_TEST
    mutex = xSemaphoreCreateMutex();
    if (!mutex) return;
#endif
    storageReady = store.begin("countdown", false) && journal.load(engine.settings);
}
void tick() {
    begin(); lock();
    uint64_t now = FocusService::monotonic();
    engine.tick(now);
    if (engine.stage == TimerEngine::Stage::Finished && engine.revision != finishedRevision) {
        finishedRevision = engine.revision;
        pending = true;
        ringUntil = now + 60000;
    }
    if (pending && now >= ringUntil) silence();
    if (pending && !sounding && engine.volume) {
        sounding = AudioBoard::ringtone(engine.tone, engine.volume,
                    uint32_t(ringUntil - now), 2, 0, AudioBoard::Reminder::Timer);
    }
    unlock();
}
Snapshot snapshot() {
    begin(); lock();
    Snapshot s; s.engine = engine; s.storageReady = storageReady;
    s.writeFailed = writeFailed; s.ringing = pending;
    unlock(); return s;
}
bool act(Action action, const TimerEngine::Settings *settings, const char *sessionName) {
    begin(); lock();
    if (action == Action::Start && engine.stage == TimerEngine::Stage::Running) { unlock(); return false; }
    bool accepted = true;
    if (action == Action::Save || (action == Action::Start && engine.stage != TimerEngine::Stage::Paused)) {
        accepted = storageReady && settings && TimerEngine::valid(*settings);
        if (accepted) {
            accepted = journal.save(*settings); writeFailed = !accepted;
            if (accepted) engine.settings = *settings;
        }
    }
    if (accepted) {
        switch (action) {
            case Action::Start: silence(); accepted = engine.start(FocusService::monotonic(), sessionName); break;
            case Action::Pause: accepted = engine.pause(FocusService::monotonic()) || engine.stage == TimerEngine::Stage::Finished; break;
            case Action::Reset: silence(); engine.reset(); break;
            case Action::Dismiss: silence(); finishedRevision = engine.revision; break;
            case Action::Save: break;
            case Action::Replay: silence(); accepted = engine.replay(FocusService::monotonic()); break;
        }
    }
    unlock(); return accepted;
}
bool busy() { auto s = snapshot(); return s.engine.stage == TimerEngine::Stage::Running || s.engine.stage == TimerEngine::Stage::Paused || s.ringing; }
bool ringing() { return snapshot().ringing; }
void preempt() { begin(); lock(); sounding = false; AudioBoard::stopRingtone(2, AudioBoard::Reminder::Timer); unlock(); }
} // namespace TimerService
