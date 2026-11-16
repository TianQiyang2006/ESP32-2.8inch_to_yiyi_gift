#pragma once
#include <stdint.h>

namespace VoiceSession {
// Worker-owned session lifecycle. Transport retries leave active intact.
struct Session {
    static constexpr uint32_t IdleMs=60000, ResponseMs=60000, RearmMs=220;
    bool active=false, sleeping=false, rearming=false;
    uint32_t listeningAt=0, waitingAt=0, rearmAt=0;
    void start(){active=true;sleeping=false;rearming=false;}
    void end(bool sleep=false){active=false;sleeping=sleep;rearming=false;}
    void listen(uint32_t now){listeningAt=now;rearming=false;}
    void waiting(uint32_t now){waitingAt=now;}
    bool idle(uint32_t now,bool speech,uint32_t lastSpeechAt) const {
        return active&&uint32_t(now-(speech?lastSpeechAt:listeningAt))>=IdleMs;
    }
    bool responseExpired(uint32_t now) const {return uint32_t(now-waitingAt)>=ResponseMs;}
    void playbackDone(uint32_t now){rearmAt=now+RearmMs;rearming=true;}
    bool resume(uint32_t now) const {return active&&rearming&&int32_t(now-rearmAt)>=0;}
};
}
