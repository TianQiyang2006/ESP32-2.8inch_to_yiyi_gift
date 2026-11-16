#pragma once
#include "alarm_persistence.h"

namespace AlarmRecovery {
constexpr int64_t CatchUpSeconds = 60;
inline int weekday(int64_t beijingMidnight) {
    int64_t days = (beijingMidnight + 8 * 3600) / 86400;
    return (int)((days + 4) % 7 + 7) % 7;
}
inline int64_t midnight(int64_t epoch) {
    auto date = DeskLogic::beijingDate(epoch);
    return DeskLogic::beijingEpoch(date.year, date.month, date.day);
}
inline int64_t due(const AlarmPersistence::State &state, int64_t now) {
    if (!state.enabled || !state.days || !DeskLogic::timeValid(now)) return 0;
    int64_t target = midnight(now) + (int64_t)state.minute * 60;
    if (target > now) target -= 86400;
    if (now - target > CatchUpSeconds || target <= state.lastFired) return 0;
    if (!(state.days & (1u << weekday(target - (int64_t)state.minute * 60)))) return 0;
    return target;
}
inline int64_t next(const AlarmPersistence::State &state, int64_t now) {
    if (!state.enabled || !state.days || !DeskLogic::timeValid(now)) return 0;
    int64_t day = midnight(now);
    for (int offset = 0; offset < 8; ++offset) {
        int64_t date = day + offset * 86400;
        int64_t target = date + (int64_t)state.minute * 60;
        if (target >= now && target > state.lastFired && (state.days & (1u << weekday(date)))) return target;
    }
    return 0;
}
} // namespace AlarmRecovery
