#pragma once
#include <stdint.h>

// Distance along the line joining the current and target platforms. The LVGL
// page projects that line into its isometric scene; these rules own all timing,
// charge, contact, scores and random targets without display or heap state.
namespace JumpEngine {
constexpr uint16_t ChargeLimitMs = 1000, FlightDurationMs = 420;
constexpr uint16_t SettleDurationMs = 220, FallDurationMs = 300;
constexpr uint16_t MaximumDistance = 210, PerfectRadius = 5;
enum class State { Ready, Playing, Charging, Jumping, Settling, Falling, Paused, GameOver };

struct Game {
    State state = State::Ready, pausedState = State::Playing;
    uint32_t score = 0, best = 0, jumps = 0, combo = 0, lastGain = 0;
    uint32_t phaseAt = 0, chargeStartedAt = 0, randomState = 1, pausedElapsed = 0;
    uint16_t chargeMs = 0, flightMs = FlightDurationMs;
    uint16_t jumpDistance = 0, targetDistance = 100, currentRadius = 22, targetRadius = 22;
    int8_t direction = 1, nextDirection = 1;
    bool landedTarget = false, landedCurrent = false, perfect = false;

    uint32_t random() {
        randomState = randomState * 1664525u + 1013904223u;
        return randomState;
    }
    int8_t chooseDirection() { return (random() >> 31) ? 1 : -1; }
    void generateTarget() {
        targetDistance = (uint16_t)(85 + (random() >> 8) % 61);
        targetRadius = (uint16_t)(18 + (random() >> 8) % 10);
    }
    void prepare(uint32_t seed) {
        state = State::Ready; pausedState = State::Playing;
        score = jumps = combo = lastGain = 0;
        phaseAt = chargeStartedAt = pausedElapsed = 0;
        chargeMs = jumpDistance = 0; flightMs = FlightDurationMs;
        currentRadius = 22; randomState = seed;
        landedTarget = landedCurrent = perfect = false;
        generateTarget();
        direction = nextDirection = chooseDirection();
    }
    void start(uint32_t now, uint32_t seed) {
        prepare(seed); state = State::Playing; phaseAt = now;
    }
    void restart(uint32_t now) { start(now, randomState); }
    uint32_t elapsed(uint32_t now) const {
        return state == State::Paused ? pausedElapsed : (uint32_t)(now - phaseAt);
    }
    uint16_t chargeElapsed(uint32_t now) const {
        if (state != State::Charging) return chargeMs;
        const uint32_t held = (uint32_t)(now - chargeStartedAt);
        return (uint16_t)(held >= ChargeLimitMs ? ChargeLimitMs : held);
    }
    float chargeProgress(uint32_t now) const { return (float)chargeElapsed(now) / ChargeLimitMs; }
    float flightProgress(uint32_t now) const {
        const State active = state == State::Paused ? pausedState : state;
        if (active == State::Settling || active == State::Falling || active == State::GameOver) return 1.0f;
        if (active != State::Jumping) return 0.0f;
        const uint32_t passed = elapsed(now);
        return passed >= flightMs ? 1.0f : (float)passed / flightMs;
    }
    float height(uint32_t now) const {
        const State active = state == State::Paused ? pausedState : state;
        if (active != State::Jumping) return 0.0f;
        const float t = flightProgress(now);
        return 168.0f * t * (1.0f - t);
    }
    bool press(uint32_t now) {
        if (state != State::Playing) return false;
        state = State::Charging; phaseAt = chargeStartedAt = now;
        chargeMs = 0; jumpDistance = 0;
        landedTarget = landedCurrent = perfect = false; lastGain = 0;
        return true;
    }
    bool release(uint32_t now) {
        if (state != State::Charging) return false;
        chargeMs = chargeElapsed(now);
        jumpDistance = (uint16_t)((uint32_t)chargeMs * MaximumDistance / ChargeLimitMs);
        state = State::Jumping; phaseAt = now;
        return true;
    }
    bool cancelCharge() {
        if (state != State::Charging) return false;
        state = State::Playing; chargeMs = jumpDistance = 0;
        return true;
    }
    bool tick(uint32_t now) {
        if (state == State::Charging) {
            const uint16_t held = chargeElapsed(now);
            if (held == chargeMs) return false;
            chargeMs = held;
            return true;
        }
        if (state == State::Jumping && elapsed(now) >= flightMs) {
            const int difference = (int)jumpDistance - targetDistance;
            const unsigned distanceFromTarget = difference < 0 ? (unsigned)-difference : (unsigned)difference;
            phaseAt += flightMs;
            landedTarget = distanceFromTarget <= targetRadius;
            landedCurrent = !landedTarget && jumpDistance <= currentRadius;
            if (landedTarget) {
                perfect = distanceFromTarget <= PerfectRadius;
                lastGain = perfect ? 2 : 1;
                combo = perfect ? combo + 1 : 0;
                score += lastGain; if (score > best) best = score;
                ++jumps;
                nextDirection = chooseDirection();
                state = State::Settling;
            } else if (landedCurrent) {
                combo = lastGain = 0; perfect = false;
                state = State::Settling;
            } else {
                combo = lastGain = 0; perfect = false;
                state = State::Falling;
            }
            return true;
        }
        if (state == State::Settling && elapsed(now) >= SettleDurationMs) {
            phaseAt += SettleDurationMs;
            if (landedTarget) {
                currentRadius = targetRadius;
                direction = nextDirection;
                generateTarget();
            }
            state = State::Playing;
            return true;
        }
        if (state == State::Falling && elapsed(now) >= FallDurationMs) {
            phaseAt += FallDurationMs; state = State::GameOver;
            return true;
        }
        return false;
    }
    bool pause(uint32_t now) {
        if (state == State::Ready || state == State::Paused || state == State::GameOver) return false;
        if (state == State::Charging) {
            cancelCharge(); pausedState = State::Playing; pausedElapsed = 0;
        } else {
            pausedState = state; pausedElapsed = elapsed(now);
        }
        state = State::Paused;
        return true;
    }
    bool resume(uint32_t now) {
        if (state != State::Paused) return false;
        state = pausedState; phaseAt = now - pausedElapsed;
        return true;
    }
};
}  // namespace JumpEngine
