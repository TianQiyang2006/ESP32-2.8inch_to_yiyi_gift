#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../desk_logic.h"

// Shared by the ESP32 task and native tests. No UI, flash or audio calls here.
namespace FocusEngine {
constexpr unsigned AlarmCount=5, DailyCount=8, PendingCount=16;
constexpr int64_t CatchUp=60;
enum class Tone:uint8_t { Classic=0, Gentle=1, Progressive=2 };
enum class Stage:uint8_t { Ready, FocusRun, FocusPause, FocusDone, RestRun, RestPause, RestDone, Complete, Interrupted };
enum class Phase:uint8_t { Focus, ShortRest, LongRest };
struct Alarm {
    uint32_t id=0;
    uint16_t minute=450;
    uint8_t days=0, tone=0, volume=20, snoozeMinutes=10, ringMinutes=3;
    bool used=false, enabled=false, once=true, fade=true, missed=false;
    int64_t onceEpoch=0, lastFired=0, snoozeEpoch=0, armedAt=0;
    uint32_t delayMs=0;
};
struct Preferences {
    uint8_t focus=25, shortRest=5, longRest=15, rounds=4, tone=1, volume=20;
    bool autoRest=false, autoFocus=false;
};
// App-wide output levels, independent of per-alarm and per-session settings.
struct ReminderVolumes { uint8_t alarm=20, pomodoro=20; };
struct Session {
    Preferences settings;
    Stage stage=Stage::Ready;
    Phase phase=Phase::Focus;
    uint8_t round=1, completed=0, countedRound=0;
    uint32_t id=0, minutes=0;
    uint64_t remaining=25ULL*60000, total=25ULL*60000;
    bool active=false;
};
struct Daily { int32_t day=0; uint32_t count=0, minutes=0; };
struct Pending { uint32_t boot=0; uint64_t mono=0; uint16_t minutes=0; };
struct State {
    uint32_t nextAlarmId=1, nextSessionId=1, bootId=0;
    int64_t lastSync=0;
    Alarm alarms[AlarmCount];
    Preferences preferences;
    Session session;
    Daily daily[DailyCount];
    Pending pending[PendingCount];
    uint32_t unassignedCount=0, unassignedMinutes=0;
    ReminderVolumes audio;
};
inline bool validTime(int64_t epoch){return DeskLogic::timeValid(epoch);}
inline int64_t midnight(int64_t epoch){auto d=DeskLogic::beijingDate(epoch);return DeskLogic::beijingEpoch(d.year,d.month,d.day);}
inline int weekday(int64_t day){return int(((day+28800)/86400+4)%7);}
inline int32_t dateKey(int64_t epoch){auto d=DeskLogic::beijingDate(epoch);return int32_t(DeskLogic::civilDays(d.year,d.month,d.day));}
inline bool valid(const Preferences &p){return p.focus>=5&&p.focus<=90&&p.shortRest>=1&&p.shortRest<=30&&p.longRest>=5&&p.longRest<=60&&p.rounds>=2&&p.rounds<=8&&p.tone<3&&p.volume<=100;}
inline bool valid(const ReminderVolumes &v){return v.alarm<=100&&v.pomodoro<=100;}
inline bool valid(const Alarm &a){return !a.used||(a.id&&a.minute<1440&&a.days<=127&&a.tone<3&&a.volume>=5&&a.volume<=100&&(a.snoozeMinutes==5||a.snoozeMinutes==10||a.snoozeMinutes==15)&&(a.ringMinutes==1||a.ringMinutes==3||a.ringMinutes==5)&&(!a.enabled||a.once||a.days)&&(!a.onceEpoch||validTime(a.onceEpoch))&&(!a.lastFired||validTime(a.lastFired))&&(!a.snoozeEpoch||validTime(a.snoozeEpoch))&&(!a.armedAt||validTime(a.armedAt)));}
inline int64_t next(const Alarm &a,int64_t epoch){
    if(!a.used||!a.enabled||!validTime(epoch))return 0;
    if(a.once){if(a.onceEpoch)return a.onceEpoch>epoch&&a.onceEpoch>a.lastFired?a.onceEpoch:0;int64_t t=midnight(epoch)+a.minute*60;return t>epoch?t:t+86400;}
    int64_t day=midnight(epoch);
    for(int i=0;i<8;++i){int64_t d=day+i*86400,t=d+a.minute*60;if(t>epoch&&t>a.lastFired&&t>=a.armedAt&&(a.days&(1u<<weekday(d))))return t;}
    return 0;
}
inline int64_t recent(const Alarm &a,int64_t epoch){
    if(!a.used||!a.enabled||!validTime(epoch))return 0;
    if(a.once)return a.onceEpoch&&a.onceEpoch<=epoch&&a.onceEpoch>a.lastFired?a.onceEpoch:0;
    int64_t day=midnight(epoch);
    for(int i=0;i<8;++i){int64_t d=day-i*86400,t=d+a.minute*60;if(t>epoch)continue;if(t<=a.lastFired||t<a.armedAt)return 0;if(a.days&(1u<<weekday(d)))return t;}
    return 0;
}
struct Runtime {
    uint8_t ringing=0;
    uint64_t ringUntil[AlarmCount]{}, snoozeUntil[AlarmCount]{}, started=0, deadline=0;
    int64_t occurrences[AlarmCount]{};
    uint32_t ringRevision=0, notificationRevision=0;
    bool dirty=false;
};
class Engine {
public:
    State state;
    Runtime runtime;
    void changed(){runtime.dirty=true;}
    void restore(){
        ++state.bootId;
        runtime=Runtime{};
        if(state.session.active)state.session.stage=Stage::Interrupted;
        changed();
    }
    unsigned count()const{unsigned n=0;for(auto &a:state.alarms)n+=a.used;return n;}
    int freeAlarm()const{for(unsigned i=0;i<AlarmCount;++i)if(!state.alarms[i].used)return int(i);return -1;}
    bool saveAlarm(unsigned index,Alarm draft,int64_t epoch){
        if(index>=AlarmCount)return false;
        draft.used=true;
        if(!draft.id)draft.id=state.nextAlarmId;
        if(!valid(draft))return false;
        if(!state.alarms[index].used)++state.nextAlarmId;
        draft.snoozeEpoch=0;draft.missed=false;draft.onceEpoch=0;draft.armedAt=validTime(epoch)?epoch:0;
        if(draft.once&&draft.enabled&&validTime(epoch))draft.onceEpoch=next(draft,epoch);
        state.alarms[index]=draft;
        cancelRuntime(index);changed();return true;
    }
    bool toggle(unsigned index,int64_t epoch){
        if(index>=AlarmCount||!state.alarms[index].used)return false;
        auto &a=state.alarms[index];a.enabled=!(a.enabled||a.snoozeEpoch);a.snoozeEpoch=0;a.missed=false;
        if(a.enabled){a.armedAt=validTime(epoch)?epoch:0;a.onceEpoch=0;if(a.once&&validTime(epoch))a.onceEpoch=next(a,epoch);}
        cancelRuntime(index);changed();return true;
    }
    void erase(unsigned index){if(index<AlarmCount){state.alarms[index]=Alarm{};cancelRuntime(index);changed();}}
    void stop(){
        uint8_t mask=runtime.ringing;
        for(unsigned i=0;i<AlarmCount;++i)if(mask&(1u<<i)){state.alarms[i].snoozeEpoch=0;runtime.snoozeUntil[i]=0;}
        runtime.ringing=0;++runtime.ringRevision;changed();
    }
    bool snooze(int64_t epoch,uint64_t mono){
        if(!runtime.ringing||!validTime(epoch))return false;
        for(unsigned i=0;i<AlarmCount;++i)if(runtime.ringing&(1u<<i)){
            auto &a=state.alarms[i];a.snoozeEpoch=epoch+a.snoozeMinutes*60;
            runtime.snoozeUntil[i]=mono+uint64_t(a.snoozeMinutes)*60000;
        }
        runtime.ringing=0;++runtime.ringRevision;changed();return true;
    }
    void cancelSnooze(unsigned index){if(index<AlarmCount){state.alarms[index].snoozeEpoch=0;runtime.snoozeUntil[index]=0;changed();}}
    unsigned ringingCount()const{unsigned n=0;for(unsigned i=0;i<AlarmCount;++i)n+=(runtime.ringing>>i)&1;return n;}
    int leadAlarm()const{int lead=-1;for(unsigned i=0;i<AlarmCount;++i)if(runtime.ringing&(1u<<i))if(lead<0||runtime.occurrences[i]<runtime.occurrences[lead])lead=int(i);return lead;}
    unsigned ringVolume()const{unsigned volume=0;for(unsigned i=0;i<AlarmCount;++i)if((runtime.ringing&(1u<<i))&&state.alarms[i].volume>volume)volume=state.alarms[i].volume;return volume;}
    uint64_t remaining(uint64_t mono)const{
        auto &s=state.session;
        if(s.stage==Stage::FocusRun||s.stage==Stage::RestRun)return runtime.deadline>mono?runtime.deadline-mono:0;
        return s.remaining;
    }
    bool configure(Preferences settings){if(!valid(settings))return false;state.preferences=settings;if(!state.session.active){state.session.settings=settings;state.session.total=state.session.remaining=uint64_t(settings.focus)*60000;}changed();return true;}
    bool start(uint64_t mono){
        auto &s=state.session;
        if(s.stage==Stage::FocusRun||s.stage==Stage::RestRun)return false;
        if(s.stage==Stage::FocusPause||s.stage==Stage::RestPause){runtime.deadline=mono+s.remaining;s.stage=s.phase==Phase::Focus?Stage::FocusRun:Stage::RestRun;changed();return true;}
        if(s.stage==Stage::Ready||s.stage==Stage::Complete){s=Session{};s.id=state.nextSessionId++;s.settings=state.preferences;s.total=s.remaining=uint64_t(s.settings.focus)*60000;}
        if(s.stage==Stage::Interrupted){s.remaining=s.total;}
        s.active=true;s.stage=s.phase==Phase::Focus?Stage::FocusRun:Stage::RestRun;
        runtime.deadline=mono+s.remaining;changed();return true;
    }
    bool pause(uint64_t mono){auto &s=state.session;if(s.stage!=Stage::FocusRun&&s.stage!=Stage::RestRun)return false;s.remaining=remaining(mono);s.stage=s.phase==Phase::Focus?Stage::FocusPause:Stage::RestPause;changed();return true;}
    void end(){state.session=Session{};state.session.settings=state.preferences;state.session.remaining=state.session.total=uint64_t(state.preferences.focus)*60000;runtime.deadline=0;changed();}
    void skipRest(){
        auto &s=state.session;if(s.phase==Phase::Focus)return;
        if(s.phase==Phase::LongRest){s.stage=Stage::Complete;s.active=false;s.remaining=0;}
        else{++s.round;s.phase=Phase::Focus;s.stage=Stage::RestDone;s.total=s.remaining=uint64_t(s.settings.focus)*60000;}
        runtime.deadline=0;changed();
    }
    Daily today(int64_t epoch)const{Daily empty;if(!validTime(epoch))return empty;int32_t key=dateKey(epoch);for(auto &d:state.daily)if(d.day==key)return d;return empty;}
    unsigned pendingCount()const{unsigned n=state.unassignedCount;for(auto &p:state.pending)if(p.minutes)++n;return n;}
    void tick(int64_t epoch,uint64_t mono){
        resolveDates(epoch,mono);
        for(unsigned i=0;i<AlarmCount;++i){
            auto &a=state.alarms[i];if(!a.used)continue;
            if((runtime.ringing&(1u<<i))&&mono>=runtime.ringUntil[i]){runtime.ringing&=uint8_t(~(1u<<i));a.missed=true;++runtime.ringRevision;changed();}
            if(a.snoozeEpoch){
                if(!runtime.snoozeUntil[i]&&validTime(epoch)){
                    int64_t delta=a.snoozeEpoch-epoch;
                    if(delta>0)runtime.snoozeUntil[i]=mono+uint64_t(delta)*1000;
                    else if(-delta<=CatchUp){fire(i,a.snoozeEpoch,epoch,mono,false);continue;}
                    else{a.snoozeEpoch=0;a.missed=true;changed();}
                }
                if(runtime.snoozeUntil[i]&&mono>=runtime.snoozeUntil[i]){fire(i,a.snoozeEpoch,epoch,mono,false);continue;}
                if(a.snoozeEpoch)continue;
            }
            if(!a.enabled||!validTime(epoch))continue;
            if(a.once&&!a.onceEpoch){a.onceEpoch=next(a,epoch);changed();}
            int64_t target=recent(a,epoch);
            if(target){
                if(epoch-target<=CatchUp)fire(i,target,epoch,mono,true);
                else{a.lastFired=target;a.missed=true;if(a.once)a.enabled=false;changed();}
            }
        }
        auto &s=state.session;
        if((s.stage!=Stage::FocusRun&&s.stage!=Stage::RestRun)||mono<runtime.deadline)return;
        if(mono-runtime.deadline>60000){s.stage=Stage::Interrupted;s.remaining=s.total;runtime.deadline=0;changed();return;}
        uint64_t boundary=runtime.deadline;
        if(s.phase==Phase::Focus){
            if(s.countedRound!=s.round){s.countedRound=s.round;++s.completed;s.minutes+=s.settings.focus;addCompletion(epoch,mono,s.settings.focus);}
            s.phase=s.round>=s.settings.rounds?Phase::LongRest:Phase::ShortRest;
            s.total=s.remaining=uint64_t(s.phase==Phase::LongRest?s.settings.longRest:s.settings.shortRest)*60000;
            s.stage=Stage::FocusDone;
            if(s.settings.autoRest){s.stage=Stage::RestRun;runtime.deadline=boundary+s.total;}
        }else if(s.phase==Phase::LongRest){s.stage=Stage::Complete;s.active=false;s.remaining=0;runtime.deadline=0;}
        else{++s.round;s.phase=Phase::Focus;s.total=s.remaining=uint64_t(s.settings.focus)*60000;s.stage=Stage::RestDone;if(s.settings.autoFocus){s.stage=Stage::FocusRun;runtime.deadline=boundary+s.total;}}
        ++runtime.notificationRevision;changed();
    }
private:
    void cancelRuntime(unsigned index){if(runtime.ringing&(1u<<index)){runtime.ringing&=uint8_t(~(1u<<index));++runtime.ringRevision;}runtime.snoozeUntil[index]=0;}
    void fire(unsigned index,int64_t target,int64_t epoch,uint64_t mono,bool scheduled){
        auto &a=state.alarms[index];
        if(scheduled){a.lastFired=target;if(a.once)a.enabled=false;}
        a.snoozeEpoch=0;runtime.snoozeUntil[index]=0;a.missed=false;
        a.delayMs=validTime(epoch)&&epoch>target?uint32_t((epoch-target)*1000):0;
        if(!runtime.ringing)runtime.started=mono;
        runtime.ringing|=uint8_t(1u<<index);runtime.occurrences[index]=target;
        runtime.ringUntil[index]=mono+uint64_t(a.ringMinutes)*60000;++runtime.ringRevision;changed();
    }
    void addDay(int64_t epoch,unsigned minutes){
        int32_t key=dateKey(epoch);unsigned slot=0;
        for(unsigned i=0;i<DailyCount;++i){if(state.daily[i].day==key){slot=i;break;}if(!state.daily[i].day||state.daily[i].day<state.daily[slot].day)slot=i;}
        auto &d=state.daily[slot];if(d.day!=key){d=Daily{};d.day=key;}++d.count;d.minutes+=minutes;
    }
    void addCompletion(int64_t epoch,uint64_t mono,unsigned minutes){
        if(validTime(epoch))addDay(epoch,minutes);
        else{bool stored=false;for(auto &p:state.pending)if(!p.minutes){p.boot=state.bootId;p.mono=mono;p.minutes=uint16_t(minutes);stored=true;break;}if(!stored){++state.unassignedCount;state.unassignedMinutes+=minutes;}}
    }
    void resolveDates(int64_t epoch,uint64_t mono){
        if(!validTime(epoch))return;
        for(auto &p:state.pending)if(p.minutes&&p.boot==state.bootId&&mono>=p.mono){int64_t completedAt=epoch-int64_t((mono-p.mono)/1000);if(validTime(completedAt)){addDay(completedAt,p.minutes);p=Pending{};changed();}}
    }
};
} // namespace FocusEngine
