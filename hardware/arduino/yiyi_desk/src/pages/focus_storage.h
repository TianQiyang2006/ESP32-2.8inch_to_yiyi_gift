#pragma once
#include <Preferences.h>
#include "focus_engine.h"
#include "alarm_persistence.h"

namespace FocusStorage {
using namespace FocusEngine;
enum class Result { Empty, Ready, Recovered, Corrupt };
struct Writer {
    uint8_t *bytes;size_t at=0;
    explicit Writer(uint8_t *data):bytes(data){}
    void put(uint64_t value,unsigned count){for(unsigned i=0;i<count;++i)bytes[at++]=uint8_t(value>>(i*8));}
};
struct Reader {
    const uint8_t *bytes;size_t size,at=0;bool ok=true;
    Reader(const uint8_t *data,size_t length):bytes(data),size(length){}
    uint64_t get(unsigned count){if(at+count>size){ok=false;return 0;}uint64_t value=0;for(unsigned i=0;i<count;++i)value|=uint64_t(bytes[at++])<<(i*8);return value;}
};
inline uint32_t crc(const uint8_t *data,size_t size){uint32_t c=0xffffffff;for(size_t i=0;i<size;++i){c^=data[i];for(int bit=0;bit<8;++bit)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return c^0xffffffff;}
inline void encodePrefs(Writer &w,const FocusEngine::Preferences &p){w.put(p.focus,1);w.put(p.shortRest,1);w.put(p.longRest,1);w.put(p.rounds,1);w.put(p.tone,1);w.put(p.volume,1);w.put((p.autoRest?1:0)|(p.autoFocus?2:0),1);}
inline void decodePrefs(Reader &r,FocusEngine::Preferences &p){p.focus=r.get(1);p.shortRest=r.get(1);p.longRest=r.get(1);p.rounds=r.get(1);p.tone=r.get(1);p.volume=r.get(1);uint8_t flags=r.get(1);p.autoRest=flags&1;p.autoFocus=flags&2;if(flags&~3)r.ok=false;}
class Journal {
public:
    static constexpr size_t Capacity=768;
    explicit Journal(::Preferences &store):store_(store){}
    Result load(State &state){
        uint8_t a[Capacity]{},b[Capacity]{};State sa,sb;uint32_t qa=0,qb=0;size_t na=0,nb=0;
        bool ea=store_.isKey("focus0"),eb=store_.isKey("focus1");
        bool va=read("focus0",a,na)&&decode(a,na,sa,qa),vb=read("focus1",b,nb)&&decode(b,nb,sb,qb);
        if(!va&&!vb){loaded_=false;slot_=-1;sequence_=0;return ea||eb?Result::Corrupt:Result::Empty;}
        bool useB=vb&&(!va||int32_t(qb-qa)>0);slot_=useB?1:0;sequence_=useB?qb:qa;state=useB?sb:sa;
        savedSize_=useB?nb:na;memcpy(saved_,useB?b:a,savedSize_);loaded_=true;
        return (ea&&!va)||(eb&&!vb)?Result::Recovered:Result::Ready;
    }
    bool save(const State &state){
        uint8_t data[Capacity]{},check[Capacity]{};size_t length=encode(state,sequence_+1,data);
        State verified;uint32_t sequence=0;
        if(!decode(data,length,verified,sequence))return false;
        if(loaded_&&length==savedSize_&&!memcmp(data+16,saved_+16,length-20))return true;
        const char *key=slot_==0?"focus1":"focus0";
        if(store_.putBytes(key,data,length)!=length)return false;
        size_t checked=0;if(!read(key,check,checked)||checked!=length||memcmp(data,check,length)||!decode(check,checked,verified,sequence))return false;
        slot_=slot_==0?1:0;sequence_=sequence;loaded_=true;savedSize_=length;memcpy(saved_,data,length);return true;
    }
    static size_t encode(const State &s,uint32_t sequence,uint8_t *out){
        Writer w{out};w.put(0x32534346,4);w.put(3,4);w.put(sequence,4);w.put(0,4);
        w.put(s.nextAlarmId,4);w.put(s.nextSessionId,4);w.put(s.bootId,4);w.put(uint64_t(s.lastSync),8);
        for(auto &a:s.alarms){
            w.put(a.id,4);w.put(a.minute,2);w.put(a.days,1);w.put(a.tone,1);w.put(a.volume,1);w.put(a.snoozeMinutes,1);w.put(a.ringMinutes,1);
            w.put((a.used?1:0)|(a.enabled?2:0)|(a.once?4:0)|(a.fade?8:0)|(a.missed?16:0),1);
            w.put(uint64_t(a.onceEpoch),8);w.put(uint64_t(a.lastFired),8);w.put(uint64_t(a.snoozeEpoch),8);w.put(uint64_t(a.armedAt),8);w.put(a.delayMs,4);
        }
        encodePrefs(w,s.preferences);encodePrefs(w,s.session.settings);
        auto &p=s.session;w.put(uint8_t(p.stage),1);w.put(uint8_t(p.phase),1);w.put(p.round,1);w.put(p.completed,1);w.put(p.countedRound,1);
        w.put(p.id,4);w.put(p.minutes,4);w.put(p.remaining,8);w.put(p.total,8);w.put(p.active?1:0,1);
        for(auto &d:s.daily){w.put(uint32_t(d.day),4);w.put(d.count,4);w.put(d.minutes,4);}
        for(auto &pending:s.pending){w.put(pending.boot,4);w.put(pending.mono,8);w.put(pending.minutes,2);}
        w.put(s.unassignedCount,4);w.put(s.unassignedMinutes,4);
        w.put(s.audio.alarm,1);w.put(s.audio.pomodoro,1);
        size_t payloadEnd=w.at;Writer length{out+12};length.put(payloadEnd+4,4);w.put(crc(out,payloadEnd),4);return w.at;
    }
    static bool decode(const uint8_t *data,size_t length,State &s,uint32_t &sequence){
        if(length<20||length>Capacity)return false;
        Reader r{data,length};if(r.get(4)!=0x32534346)return false;uint32_t version=r.get(4);if(version!=2&&version!=3)return false;sequence=uint32_t(r.get(4));if(r.get(4)!=length)return false;
        Reader tail{data+length-4,4};if(tail.get(4)!=crc(data,length-4))return false;
        s=State{};s.nextAlarmId=r.get(4);s.nextSessionId=r.get(4);s.bootId=r.get(4);s.lastSync=int64_t(r.get(8));
        for(auto &a:s.alarms){
            a.id=r.get(4);a.minute=r.get(2);a.days=r.get(1);a.tone=r.get(1);a.volume=r.get(1);a.snoozeMinutes=r.get(1);a.ringMinutes=r.get(1);
            uint8_t f=r.get(1);a.used=f&1;a.enabled=f&2;a.once=f&4;a.fade=f&8;a.missed=f&16;if(f&~31)r.ok=false;
            a.onceEpoch=int64_t(r.get(8));a.lastFired=int64_t(r.get(8));a.snoozeEpoch=int64_t(r.get(8));a.armedAt=int64_t(r.get(8));a.delayMs=r.get(4);
            if(!FocusEngine::valid(a))r.ok=false;
        }
        decodePrefs(r,s.preferences);decodePrefs(r,s.session.settings);
        auto &p=s.session;p.stage=Stage(r.get(1));p.phase=Phase(r.get(1));p.round=r.get(1);p.completed=r.get(1);p.countedRound=r.get(1);
        p.id=r.get(4);p.minutes=r.get(4);p.remaining=r.get(8);p.total=r.get(8);uint8_t active=r.get(1);p.active=active!=0;
        for(auto &d:s.daily){d.day=int32_t(r.get(4));d.count=r.get(4);d.minutes=r.get(4);}
        for(auto &pending:s.pending){pending.boot=r.get(4);pending.mono=r.get(8);pending.minutes=r.get(2);if(pending.minutes>90)r.ok=false;}
        s.unassignedCount=r.get(4);s.unassignedMinutes=r.get(4);
        // v2 is the installed 0.7.0/0.7.1 journal. Preserve every old field and
        // supply conservative app-wide levels only for the newly added controls.
        if(version>=3){s.audio.alarm=r.get(1);s.audio.pomodoro=r.get(1);}
        if(!FocusEngine::valid(s.audio)||!FocusEngine::valid(s.preferences)||!FocusEngine::valid(p.settings)||uint8_t(p.stage)>uint8_t(Stage::Interrupted)||uint8_t(p.phase)>2||p.round<1||p.round>8||p.completed>8||p.countedRound>8||p.total>90ULL*60000||p.remaining>p.total||active>1||!s.nextAlarmId||!s.nextSessionId)r.ok=false;
        for(unsigned i=0;i<AlarmCount;++i)for(unsigned j=i+1;j<AlarmCount;++j)if(s.alarms[i].used&&s.alarms[j].used&&s.alarms[i].id==s.alarms[j].id)r.ok=false;
        return r.ok&&r.at==length-4;
    }
private:
    ::Preferences &store_;bool loaded_=false;int slot_=-1;uint32_t sequence_=0;size_t savedSize_=0;uint8_t saved_[Capacity]{};
    bool read(const char *key,uint8_t *data,size_t &size){size=store_.getBytesLength(key);return size>=20&&size<=Capacity&&store_.getBytes(key,data,size)==size;}
};
// Migrate only when no new-format journal exists. Corrupt records never re-enable
// defaults, and legacy scalar/blob settings remain untouched for backup purposes.
inline bool migrate(::Preferences &store,State &state){
    AlarmPersistence::Journal old(store);AlarmPersistence::State legacy;
    auto result=old.load(legacy);
    if(result==AlarmPersistence::LoadResult::Corrupt)return false;
    bool found=result!=AlarmPersistence::LoadResult::Empty;
    if(!found&&(store.isKey("alarmMin")||store.isKey("alarmOn"))){legacy.minute=store.getUShort("alarmMin",480);legacy.days=store.getUChar("alarmDays",127)&127;legacy.enabled=store.getBool("alarmOn",false)&&legacy.days;found=true;}
    if(found){auto &a=state.alarms[0];a.used=true;a.id=state.nextAlarmId++;a.minute=legacy.minute;a.days=legacy.days;a.once=false;a.enabled=legacy.enabled;a.lastFired=legacy.lastFired;a.snoozeEpoch=legacy.snoozeEpoch;if(!FocusEngine::valid(a))return false;}
    if(store.isKey("focusMin")){unsigned value=store.getUChar("focusMin",25);if(value>=5&&value<=90)state.preferences.focus=uint8_t(value);}
    state.session.settings=state.preferences;state.session.remaining=state.session.total=uint64_t(state.preferences.focus)*60000;return true;
}
} // namespace FocusStorage
