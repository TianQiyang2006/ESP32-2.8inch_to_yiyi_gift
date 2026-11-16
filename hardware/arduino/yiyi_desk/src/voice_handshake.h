#pragma once
#include <stdint.h>

namespace VoiceHandshake {
enum class Phase { Idle, Transport, Hello, Ready };
// DNS/TCP/TLS/HTTP upgrade have their own budget. The server-hello clock starts
// only AFTER the connected event and a successful client-hello write.
struct Clock {
    Phase phase=Phase::Idle;
    uint32_t since=0;
    void reset(){phase=Phase::Idle;since=0;}
    void connecting(uint32_t now){phase=Phase::Transport;since=now;}
    void helloSent(uint32_t now){phase=Phase::Hello;since=now;}
    void ready(){phase=Phase::Ready;}
    bool expired(uint32_t now) const {
        return (phase==Phase::Transport && uint32_t(now-since)>=40000) ||
               // The official service can complete TLS/session negotiation
               // after the socket is connected; allow slow Wi-Fi/Internet
               // links enough time before triggering the automatic retry.
               (phase==Phase::Hello && uint32_t(now-since)>=25000);
    }
};
}
