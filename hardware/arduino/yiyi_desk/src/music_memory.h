#pragma once
#include <Preferences.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace MusicMemory {
struct State {
    char track[128]{}, playlist[244]{};
    uint32_t position=0, duration=0, mode=0, volume=40;
};
struct Record { uint32_t magic=0x4d555349,version=1,sequence=0; State state; uint32_t crc=0; };
static_assert(sizeof(Record)==404,"Persistent music record layout changed");
inline uint32_t checksum(const Record &record){
    uint32_t crc=0xffffffff;
    const auto *bytes=reinterpret_cast<const uint8_t*>(&record);
    for(size_t i=0;i<offsetof(Record,crc);++i){crc^=bytes[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320U&uint32_t(-int32_t(crc&1)));}
    return ~crc;
}
inline bool valid(const State &s){
    return memchr(s.track,0,sizeof(s.track))&&memchr(s.playlist,0,sizeof(s.playlist))&&
        s.volume<=100&&s.mode<3&&s.position<=86400000&&s.duration<=86400000;
}
class Journal {
    int slot_=-1; uint32_t sequence_=0; State saved_; bool loaded_=false;
    bool read(Preferences &p,const char *key,Record &r){
        return p.getBytesLength(key)==sizeof(r)&&p.getBytes(key,&r,sizeof(r))==sizeof(r)&&
            r.magic==0x4d555349&&r.version==1&&valid(r.state)&&r.crc==checksum(r);
    }
public:
    bool load(Preferences &p,State &state){
        Record a,b;bool va=read(p,"play0",a),vb=read(p,"play1",b);
        if(!va&&!vb){slot_=-1;sequence_=0;loaded_=false;return false;}
        bool useB=vb&&(!va||int32_t(b.sequence-a.sequence)>0);
        const Record &r=useB?b:a;slot_=useB?1:0;sequence_=r.sequence;
        state=saved_=r.state;loaded_=true;return true;
    }
    bool save(Preferences &p,const State &state){
        if(!valid(state))return false;
        if(loaded_&&!memcmp(&state,&saved_,sizeof(state)))return true;
        Record r;r.sequence=sequence_+1;r.state=state;r.crc=checksum(r);
        int next=slot_==0?1:0;const char *key=next?"play1":"play0";
        if(p.putBytes(key,&r,sizeof(r))!=sizeof(r))return false;
        Record check;if(!read(p,key,check)||memcmp(&r,&check,sizeof(r)))return false;
        slot_=next;sequence_=r.sequence;saved_=state;loaded_=true;return true;
    }
};
}
