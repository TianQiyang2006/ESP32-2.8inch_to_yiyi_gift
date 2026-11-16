#pragma once
#include <Preferences.h>
#include "timer_engine.h"
#include "focus_storage.h"

// Independent namespace and versioned, CRC-protected double buffer. Existing
// alarm/Pomodoro records are neither rewritten nor migrated by this journal.
namespace TimerStorage {
class Journal {
public:
    static constexpr size_t Size = 16 + 6 + TimerEngine::PresetCount * 29 + 4;
    explicit Journal(::Preferences &store) : store_(store) {}
    bool load(TimerEngine::Settings &settings) {
        uint8_t bytes[Size];
        bool found = false, exists = false;
        for (int i = 0; i < 2; ++i) {
            const char *key = i ? "timer1" : "timer0";
            exists |= store_.isKey(key);
            TimerEngine::Settings candidate;
            uint32_t seq;
            if (store_.getBytesLength(key) != Size ||
                store_.getBytes(key, bytes, Size) != Size || !decode(bytes, candidate, seq)) continue;
            if (!found || int32_t(seq - sequence_) > 0) {
                found = true; slot_ = i; sequence_ = seq; settings = candidate;
            }
        }
        return found || !exists;
    }
    bool save(const TimerEngine::Settings &s) {
        if (!TimerEngine::valid(s)) return false;
        uint8_t data[Size]{}, check[Size]{};
        FocusStorage::Writer w(data);
        w.put(0x31524D54, 4); w.put(1, 4); w.put(sequence_ + 1, 4); w.put(Size, 4);
        w.put(s.seconds, 4); w.put(s.tone, 1); w.put(s.volume, 1);
        for (auto &p : s.presets) {
            w.put(p.seconds, 4);
            for (auto c : p.name) w.put(uint8_t(c), 1);
        }
        w.put(FocusStorage::crc(data, Size - 4), 4);
        const char *key = slot_ == 0 ? "timer1" : "timer0";
        if (store_.putBytes(key, data, Size) != Size ||
            store_.getBytes(key, check, Size) != Size || memcmp(data, check, Size)) return false;
        TimerEngine::Settings verified; uint32_t seq;
        if (!decode(check, verified, seq)) return false;
        slot_ = slot_ == 0 ? 1 : 0; sequence_ = seq;
        return true;
    }
private:
    ::Preferences &store_;
    int slot_ = -1;
    uint32_t sequence_ = 0;
    static bool decode(const uint8_t *data, TimerEngine::Settings &s, uint32_t &seq) {
        FocusStorage::Reader r(data, Size);
        if (r.get(4) != 0x31524D54 || r.get(4) != 1) return false;
        seq = uint32_t(r.get(4));
        if (r.get(4) != Size) return false;
        s.seconds = r.get(4); s.tone = r.get(1); s.volume = r.get(1);
        for (auto &p : s.presets) {
            p.seconds = r.get(4);
            for (auto &c : p.name) c = char(r.get(1));
        }
        return r.get(4) == FocusStorage::crc(data, Size - 4) && r.ok && TimerEngine::valid(s);
    }
};
} // namespace TimerStorage
