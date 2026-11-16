#pragma once
#include <Preferences.h>
#include <stdint.h>
#include <string.h>
#include "../../desk_logic.h"

namespace AlarmPersistence {
struct State {
    uint16_t minute = 8 * 60;
    uint8_t days = 0x7f;
    bool enabled = false;
    int64_t lastFired = 0;
    int64_t snoozeEpoch = 0;
};
enum class LoadResult { Empty, Ready, Recovered, Corrupt };

// Alternate whole records; a failed replacement leaves the previous slot intact.
class Journal {
public:
    static constexpr size_t RecordSize = 40;
    explicit Journal(Preferences &preferences) : store_(preferences) {}
    LoadResult load(State &state) {
        uint8_t a[RecordSize]{}, b[RecordSize]{};
        State sa, sb; uint32_t qa = 0, qb = 0;
        bool existsA = store_.isKey("alarm0"), existsB = store_.isKey("alarm1");
        bool validA = read("alarm0", a) && decode(a, sa, qa);
        bool validB = read("alarm1", b) && decode(b, sb, qb);
        if (!validA && !validB) {
            loaded_ = false; slot_ = -1; sequence_ = 0;
            return existsA || existsB ? LoadResult::Corrupt : LoadResult::Empty;
        }
        bool useB = validB && (!validA || (int32_t)(qb - qa) > 0);
        slot_ = useB ? 1 : 0; sequence_ = useB ? qb : qa;
        state = useB ? sb : sa; saved_ = state; loaded_ = true;
        return (existsA && !validA) || (existsB && !validB) ? LoadResult::Recovered : LoadResult::Ready;
    }
    bool save(const State &state) {
        if (!valid(state)) return false;
        if (loaded_ && same(state, saved_)) return true;
        uint8_t bytes[RecordSize]{}, check[RecordSize]{};
        uint32_t next = sequence_ + 1;
        encode(state, next, bytes);
        int target = slot_ == 0 ? 1 : 0;
        const char *key = target == 0 ? "alarm0" : "alarm1";
        if (store_.putBytes(key, bytes, sizeof(bytes)) != sizeof(bytes)) return false;
        if (!read(key, check) || memcmp(bytes, check, sizeof(bytes)) != 0) return false;
        State verified; uint32_t sequence = 0;
        if (!decode(check, verified, sequence) || sequence != next) return false;
        slot_ = target; sequence_ = next; saved_ = verified; loaded_ = true;
        return true;
    }
private:
    Preferences &store_;
    State saved_;
    int slot_ = -1;
    uint32_t sequence_ = 0;
    bool loaded_ = false;
    static bool same(const State &a, const State &b) {
        return a.minute == b.minute && a.days == b.days && a.enabled == b.enabled &&
               a.lastFired == b.lastFired && a.snoozeEpoch == b.snoozeEpoch;
    }
    static bool valid(const State &state) {
        return state.minute < 1440 && state.days <= 0x7f && (!state.enabled || state.days != 0) &&
               (state.lastFired == 0 || DeskLogic::timeValid(state.lastFired)) &&
               (state.snoozeEpoch == 0 || DeskLogic::timeValid(state.snoozeEpoch));
    }
    static void put(uint8_t *bytes, unsigned offset, uint64_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i) bytes[offset + i] = (uint8_t)(value >> (8 * i));
    }
    static uint64_t get(const uint8_t *bytes, unsigned offset, unsigned count) {
        uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i) value |= (uint64_t)bytes[offset + i] << (8 * i);
        return value;
    }
    static uint32_t crc(const uint8_t *bytes, size_t size) {
        uint32_t value = 0xffffffffu;
        for (size_t i = 0; i < size; ++i) {
            value ^= bytes[i];
            for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ ((0u - (value & 1u)) & 0xedb88320u);
        }
        return value ^ 0xffffffffu;
    }
    static void encode(const State &state, uint32_t sequence, uint8_t *bytes) {
        memset(bytes, 0, RecordSize);
        put(bytes, 0, 0x314d4c41u, 4); // ALM1
        bytes[4] = 1; bytes[5] = state.enabled ? 1 : 0; bytes[6] = state.days;
        put(bytes, 8, state.minute, 2); put(bytes, 12, sequence, 4);
        put(bytes, 16, (uint64_t)state.lastFired, 8); put(bytes, 24, (uint64_t)state.snoozeEpoch, 8);
        put(bytes, 36, crc(bytes, 36), 4);
    }
    static bool decode(const uint8_t *bytes, State &state, uint32_t &sequence) {
        if (get(bytes, 0, 4) != 0x314d4c41u || bytes[4] != 1 || bytes[5] > 1 ||
            get(bytes, 36, 4) != crc(bytes, 36)) return false;
        if (bytes[7] || bytes[10] || bytes[11] || get(bytes, 32, 4)) return false;
        state.minute = (uint16_t)get(bytes, 8, 2); state.days = bytes[6]; state.enabled = bytes[5] != 0;
        state.lastFired = (int64_t)get(bytes, 16, 8); state.snoozeEpoch = (int64_t)get(bytes, 24, 8);
        sequence = (uint32_t)get(bytes, 12, 4);
        return valid(state);
    }
    bool read(const char *key, uint8_t *bytes) {
        return store_.getBytesLength(key) == RecordSize && store_.getBytes(key, bytes, RecordSize) == RecordSize;
    }
};
} // namespace AlarmPersistence
