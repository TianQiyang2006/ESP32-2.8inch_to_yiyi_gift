#pragma once
#include <stdint.h>
#include <math.h>

// Fixed-capacity rotating-pin simulation. Angles start downward and increase
// toward the right. Rendering and flight collision share the same local geometry.
namespace PinEngine {
constexpr uint8_t MaxPins = 24;
constexpr float Pi = 3.14159265358979323846f;
constexpr float CenterX = 160.0f, CenterY = 80.0f;
constexpr float HubRadius = 28.0f, PinLength = 25.0f, HeadRadius = 4.0f;
constexpr float OuterRadius = HubRadius + PinLength, StemRadius = 1.0f;
constexpr float LaunchTipY = 174.0f, ContactTipY = CenterY + HubRadius;
constexpr float MinGapDegrees = 13.0f;
constexpr uint16_t FlightMs = 240;

enum class State { Ready, Playing, Flying, Paused, LevelCleared, Won, GameOver };

inline float normalize(float angle) {
    angle = fmodf(angle, 360.0f);
    return angle < 0 ? angle + 360.0f : angle;
}
inline float gap(float a, float b) {
    const float distance = fabsf(normalize(a) - normalize(b));
    return distance > 180.0f ? 360.0f - distance : distance;
}
inline float radians(float degrees) { return degrees * Pi / 180.0f; }
inline float distanceSq(float x, float y, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay;
    const float length = dx * dx + dy * dy;
    float t = length > 0 ? ((x - ax) * dx + (y - ay) * dy) / length : 0;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    const float ex = x - (ax + t * dx), ey = y - (ay + t * dy);
    return ex * ex + ey * ey;
}

struct Game {
    float pins[MaxPins] = {};
    float rotation = 0, speed = 55, flightY = LaunchTipY;
    uint8_t level = 1, remaining = 6, attached = 3;
    uint32_t score = 0, best = 0, elapsedMs = 0, lastAt = 0, randomState = 1;
    uint16_t flightElapsedMs = 0;
    State state = State::Ready, pausedState = State::Playing;

    bool active() const { return state == State::Playing || state == State::Flying; }
    float worldAngle(int index) const { return normalize(pins[index] + rotation); }
    bool slotClear(float world = 0) const {
        for (int i = 0; i < attached; ++i)
            if (gap(worldAngle(i), world) < MinGapDegrees) return false;
        return true;
    }
    // A flying pin has a tip at flightY and its round head 25px below it.
    // Check head/head, both head/shaft pairs and the two thin shafts. Checking
    // every simulated millisecond also catches a spoke rotating into the path
    // before the incoming tip reaches the hub.
    bool flightCollision() const {
        const float tip = flightY, head = tip + PinLength;
        const float expanded = HeadRadius + StemRadius;
        for (int i = 0; i < attached; ++i) {
            const float angle = radians(worldAngle(i)), dx = sinf(angle), dy = cosf(angle);
            const float ax = CenterX + dx * HubRadius, ay = CenterY + dy * HubRadius;
            const float bx = CenterX + dx * OuterRadius, by = CenterY + dy * OuterRadius;
            const float hx = bx - CenterX, hy = by - head;
            if (hx * hx + hy * hy <= 4 * HeadRadius * HeadRadius) return true;
            if (distanceSq(bx, by, CenterX, tip, CenterX, head) <= expanded * expanded) return true;
            if (distanceSq(CenterX, head, ax, ay, bx, by) <= expanded * expanded) return true;
            // Intersection is possible only while the radial segment straddles
            // the vertical flight axis; include its 2px combined stem width.
            if (distanceSq(CenterX, tip, ax, ay, bx, by) <= 4 * StemRadius * StemRadius) return true;
            if (distanceSq(ax, ay, CenterX, tip, CenterX, head) <= 4 * StemRadius * StemRadius) return true;
        }
        return false;
    }
    void prepare(uint32_t seed) {
        randomState = seed; score = 0; level = 1; deal(); state = State::Ready;
    }
    void start(uint32_t now, uint32_t seed) { prepare(seed); state = State::Playing; lastAt = now; }
    void restart(uint32_t now) { start(now, randomState); }
    bool nextLevel(uint32_t now) {
        if (state != State::LevelCleared || level >= 3) return false;
        ++level; deal(); state = State::Playing; lastAt = now; return true;
    }
    bool shoot(uint32_t now) {
        // A queued press which began during a flight cannot launch another pin
        // merely because this clock update completes the preceding flight.
        const bool wasReady = state == State::Playing;
        tick(now);
        if (!wasReady || state != State::Playing || !remaining || attached >= MaxPins) return false;
        state = State::Flying; flightElapsedMs = 0; flightY = LaunchTipY;
        return true;
    }
    bool pause(uint32_t now) {
        tick(now);
        if (!active()) return false;
        pausedState = state; state = State::Paused; return true;
    }
    bool resume(uint32_t now) {
        if (state != State::Paused) return false;
        state = pausedState; lastAt = now; return true;
    }
    bool tick(uint32_t now) {
        if (!active()) return false;
        uint32_t delta = now - lastAt; lastAt = now;
        if (!delta) return false;
        // Keep a display/USB stall from applying seconds of unseen rotation.
        if (delta > 250) delta = 250;
        while (delta && active()) {
            if (state == State::Playing) {
                rotation = normalize(rotation + speed * ((float)delta / 1000.0f));
                elapsedMs += delta; break;
            }
            rotation = normalize(rotation + speed / 1000.0f);
            ++elapsedMs; ++flightElapsedMs; --delta;
            flightY = LaunchTipY - (LaunchTipY - ContactTipY) * ((float)flightElapsedMs / FlightMs);
            if (flightCollision()) { state = State::GameOver; break; }
            if (flightElapsedMs >= FlightMs) {
                flightY = ContactTipY;
                if (!slotClear()) { state = State::GameOver; break; }
                pins[attached++] = normalize(-rotation); --remaining; ++score;
                if (score > best) best = score;
                state = remaining ? State::Playing : level == 3 ? State::Won : State::LevelCleared;
            }
        }
        return true;
    }

private:
    void deal() {
        const uint8_t starts[] = {3, 4, 5}, shots[] = {6, 8, 10};
        const float speeds[] = {55.0f, -72.0f, 86.0f};
        attached = starts[level - 1]; remaining = shots[level - 1]; speed = speeds[level - 1];
        rotation = 0; flightY = LaunchTipY; flightElapsedMs = 0; elapsedMs = 0;
        pausedState = State::Playing;
        for (int i = 0; i < MaxPins; ++i) pins[i] = 0;
        for (int i = 0; i < attached; ++i) pins[i] = (i + 0.5f) * 360.0f / attached;
    }
};
} // namespace PinEngine
