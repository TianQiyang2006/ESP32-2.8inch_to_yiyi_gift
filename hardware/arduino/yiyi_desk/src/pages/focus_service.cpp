#include "focus_service.h"
#include "focus_storage.h"
#include "timer_service.h"
#include "../audio_board.h"
#include "../clock_source.h"
#include "../power_status.h"
#ifndef GIFT_HOST_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#include <esp_sntp.h>
#endif

namespace FocusService {
namespace {
::Preferences preferences;
FocusStorage::Journal journal(preferences);
FocusEngine::Engine engine;
bool initialized=false,storageReady=false,writeFailed=false,schedulerReady=false;
uint64_t retryAt=0;
uint32_t playedRing=0,notified=0;
bool pendingChime=false,wasRinging=false;
int64_t synced=0;
void syncAudioVolumes(){AudioBoard::setReminderVolume(AudioBoard::Reminder::Alarm,engine.state.audio.alarm);AudioBoard::setReminderVolume(AudioBoard::Reminder::Pomodoro,engine.state.audio.pomodoro);}
#ifndef GIFT_HOST_TEST
SemaphoreHandle_t mutex=nullptr;
TaskHandle_t task=nullptr;
portMUX_TYPE syncMux=portMUX_INITIALIZER_UNLOCKED;
void lock(){if(mutex)xSemaphoreTake(mutex,portMAX_DELAY);}
void unlock(){if(mutex)xSemaphoreGive(mutex);}
void syncCallback(struct timeval *time){portENTER_CRITICAL(&syncMux);synced=int64_t(time->tv_sec);portEXIT_CRITICAL(&syncMux);}
#else
void lock(){}
void unlock(){}
#endif
void dispatchAudio(uint64_t mono){
    auto &r=engine.runtime;
    if(r.notificationRevision!=notified){notified=r.notificationRevision;pendingChime=true;}
    if(r.ringing){
        TimerService::preempt();
        if(r.ringRevision!=playedRing){
            playedRing=r.ringRevision;int lead=engine.leadAlarm();uint64_t until=mono;
            for(unsigned i=0;i<FocusEngine::AlarmCount;++i)if((r.ringing&(1u<<i))&&r.ringUntil[i]>until)until=r.ringUntil[i];
            if(lead>=0){auto &a=engine.state.alarms[lead];AudioBoard::ringtone(a.tone,engine.ringVolume(),uint32_t(until-mono),3,a.fade?10000:0,AudioBoard::Reminder::Alarm);}
        }
        wasRinging=true;
    }else{
        if(wasRinging){AudioBoard::stopRingtone(3);wasRinging=false;}
        if(pendingChime&&!TimerService::ringing()){auto &p=engine.state.session.settings;if(p.volume)AudioBoard::ringtone(p.tone,p.volume,3000,2,0,AudioBoard::Reminder::Pomodoro);pendingChime=false;}
    }
}
void tick(){
    const uint64_t mono=monotonic();const int64_t epoch=ClockSource::now();
    lock();
    int64_t sync;
#ifndef GIFT_HOST_TEST
    portENTER_CRITICAL(&syncMux);sync=synced;portEXIT_CRITICAL(&syncMux);
#else
    sync=synced;
#endif
    if(sync&&sync!=engine.state.lastSync){engine.state.lastSync=sync;engine.changed();}
    if(storageReady)engine.tick(epoch,mono);
    // Start audio before flash I/O; a slow commit must not be on the sound path.
    dispatchAudio(mono);
    if(storageReady&&engine.runtime.dirty&&mono>=retryAt){
        bool saved=journal.save(engine.state);writeFailed=!saved;
        if(saved)engine.runtime.dirty=false;else retryAt=mono+2000;
    }
    unlock();
}
#ifndef GIFT_HOST_TEST
void worker(void *){TickType_t wake=xTaskGetTickCount();for(;;){tick();TimerService::tick();vTaskDelayUntil(&wake,pdMS_TO_TICKS(100));}}
#endif
}
uint64_t monotonic(){
#ifndef GIFT_HOST_TEST
    return uint64_t(esp_timer_get_time())/1000;
#else
    return uint64_t(millis());
#endif
}
void begin(){
    if(initialized)return;initialized=true;
    TimerService::begin();
#ifndef GIFT_HOST_TEST
    mutex=xSemaphoreCreateMutex();
    if(!mutex)return;
    sntp_set_time_sync_notification_cb(syncCallback);
#endif
    if(preferences.begin("focus",false)){
        auto result=journal.load(engine.state);
        if(result!=FocusStorage::Result::Corrupt){
            storageReady=result!=FocusStorage::Result::Empty||FocusStorage::migrate(preferences,engine.state);
            if(storageReady){engine.restore();writeFailed=!journal.save(engine.state);if(!writeFailed)engine.runtime.dirty=false;}
        }
    }
    syncAudioVolumes();
#ifndef GIFT_HOST_TEST
    schedulerReady=xTaskCreatePinnedToCore(worker,"gift-timers",8192,nullptr,2,&task,0)==pdPASS;
#else
    schedulerReady=true;
#endif
}
Snapshot snapshot(){begin();lock();Snapshot s;s.engine=engine;s.storageReady=storageReady;s.writeFailed=writeFailed;s.schedulerReady=schedulerReady;unlock();return s;}
void poll(){
    begin();
    auto power=PowerStatus::get();bool blocked=power.installed&&power.sampled&&(power.protection.critical||power.protection.high||power.protection.sensorFault);
    AudioBoard::protect(!blocked,100);
#ifdef GIFT_HOST_TEST
    tick();
    TimerService::tick();
#endif
}
bool act(Action action,unsigned index,const FocusEngine::Alarm *alarm,const FocusEngine::Preferences *settings){
    begin();lock();
    if(!storageReady||!schedulerReady){unlock();return false;}
    auto proposed=engine;bool accepted=true;
    const int64_t epoch=ClockSource::now();const uint64_t mono=monotonic();
    switch(action){
        case Action::SaveAlarm:accepted=alarm&&proposed.saveAlarm(index,*alarm,epoch);break;
        case Action::ToggleAlarm:accepted=proposed.toggle(index,epoch);break;
        case Action::DeleteAlarm:accepted=index<FocusEngine::AlarmCount&&proposed.state.alarms[index].used;if(accepted)proposed.erase(index);break;
        case Action::CancelSnooze:proposed.cancelSnooze(index);break;
        case Action::StopAlarm:proposed.stop();break;
        case Action::SnoozeAlarm:accepted=proposed.snooze(epoch,mono);break;
        case Action::Configure:accepted=settings&&proposed.configure(*settings);break;
        case Action::Start:accepted=proposed.start(mono);break;
        case Action::Pause:accepted=proposed.pause(mono);break;
        case Action::End:proposed.end();break;
        case Action::SkipRest:proposed.skipRest();break;
        case Action::AlarmMasterVolume:accepted=index<=100;if(accepted){proposed.state.audio.alarm=uint8_t(index);proposed.changed();}break;
        case Action::PomodoroMasterVolume:accepted=index<=100;if(accepted){proposed.state.audio.pomodoro=uint8_t(index);proposed.changed();}break;
    }
    if(!accepted){unlock();return false;}
    bool saved=journal.save(proposed.state);writeFailed=!saved;
    // A failed flash write must never trap the user in an audible alarm.
    // Keep the stopped event dirty and retry its persistence in the worker.
    if(!saved&&action==Action::StopAlarm){engine=proposed;engine.changed();retryAt=mono+2000;dispatchAudio(mono);}
    if(saved){proposed.runtime.dirty=false;engine=proposed;syncAudioVolumes();
        if(action==Action::Start||action==Action::End||action==Action::SkipRest){pendingChime=false;notified=engine.runtime.notificationRevision;AudioBoard::stopRingtone(2,AudioBoard::Reminder::Pomodoro);}
        dispatchAudio(mono);
    }
    unlock();return saved;
}
bool busy(){auto s=snapshot();if(TimerService::busy()||s.engine.state.session.active)return true;for(auto &a:s.engine.state.alarms)if(a.used&&(a.enabled||a.snoozeEpoch))return true;return s.engine.runtime.ringing!=0;}
#ifdef GIFT_HOST_TEST
void debugSet(const FocusEngine::Engine &value){begin();lock();engine=value;engine.runtime.dirty=false;storageReady=schedulerReady=true;writeFailed=false;playedRing=engine.runtime.ringRevision;notified=engine.runtime.notificationRevision;pendingChime=false;wasRinging=false;syncAudioVolumes();unlock();}
#endif
} // namespace FocusService
