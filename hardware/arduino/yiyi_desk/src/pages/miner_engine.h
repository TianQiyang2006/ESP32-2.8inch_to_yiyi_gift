#pragma once
#include <stdint.h>
#include <math.h>

// The hook travels in stage-local pixels. Fixed ore storage and swept ray/circle
// contact keep gameplay independent of LVGL and prevent misses at a low frame rate.
namespace MinerEngine {
constexpr int MaxOres = 10;
constexpr uint32_t LevelDurationMs = 60000;
constexpr float OriginX = 160.0f, OriginY = 30.0f, RestLength = 18.0f;
constexpr float CastSpeed = 155.0f, EmptyReelSpeed = 185.0f, HookRadius = 3.0f;
enum class State { Ready, Playing, Casting, Reeling, Paused, LevelCleared, Won, GameOver };
enum class Kind { Gold, Diamond, Rock };
struct Ore {
    float x, y, radius;
    Kind kind;
    uint16_t value;
    float weight;
    bool removed;
};

struct Game {
    Ore ores[MaxOres] = {};
    uint8_t oreCount = 0, level = 1, remaining = 0;
    uint32_t score = 0, best = 0, elapsedMs = 0, lastTickAt = 0, randomState = 1, swingMs = 0;
    uint16_t target = 300, lastGain = 0;
    float hookX = OriginX, hookY = OriginY + RestLength, angle = 0, ropeLength = RestLength;
    int caught = -1;
    State state = State::Ready, pausedState = State::Playing;

    uint32_t random() { randomState = randomState * 1664525u + 1013904223u; return randomState; }
    bool active() const { return state == State::Playing || state == State::Casting || state == State::Reeling; }
    void updateHook() {
        hookX = OriginX + sinf(angle) * ropeLength;
        hookY = OriginY + cosf(angle) * ropeLength;
    }
    uint16_t secondsLeft() const {
        return elapsedMs >= LevelDurationMs ? 0 : (uint16_t)((LevelDurationMs - elapsedMs + 999) / 1000);
    }
    float reelSpeed() const {
        return caught >= 0 && caught < oreCount ? EmptyReelSpeed / ores[caught].weight : EmptyReelSpeed;
    }
    float maximumLength() const {
        const float dx = sinf(angle), dy = cosf(angle);
        float limit = dy > 0.0001f ? (198.0f - OriginY) / dy : 1000.0f;
        if (dx > 0.0001f) { const float side = (314.0f - OriginX) / dx; if (side < limit) limit = side; }
        else if (dx < -0.0001f) { const float side = (6.0f - OriginX) / dx; if (side < limit) limit = side; }
        return limit;
    }
    void prepare(uint32_t seed) {
        randomState = seed; score = 0; level = 1;
        deal(); state = State::Ready;
    }
    void start(uint32_t now, uint32_t seed) { prepare(seed); state = State::Playing; lastTickAt = now; }
    void restart(uint32_t now) { start(now, randomState); }
    bool nextLevel(uint32_t now) {
        if (state != State::LevelCleared || level >= 3) return false;
        ++level; deal(); state = State::Playing; lastTickAt = now; return true;
    }
    bool cast(uint32_t now) {
        tick(now);
        if (state != State::Playing) return false;
        state = State::Casting; lastGain = 0; caught = -1;
        ropeLength = RestLength; updateHook();
        return true;
    }
    bool pause(uint32_t now) {
        tick(now);
        if (!active()) return false;
        pausedState = state; state = State::Paused; return true;
    }
    bool resume(uint32_t now) {
        if (state != State::Paused) return false;
        state = pausedState; lastTickAt = now; return true;
    }
    bool tick(uint32_t now) {
        if (!active()) return false;
        const uint32_t passed = (uint32_t)(now - lastTickAt);
        if (!passed) return false;
        lastTickAt = now;
        const uint32_t available = elapsedMs >= LevelDurationMs ? 0 : LevelDurationMs - elapsedMs;
        const uint32_t stepMs = passed > available ? available : passed;
        elapsedMs += stepMs;
        advance((float)stepMs / 1000.0f);
        if (active() && elapsedMs >= LevelDurationMs) state = State::GameOver;
        return true;
    }

private:
    void deal() {
        static const uint16_t goals[] = {300, 750, 1250};
        static const Ore layout[MaxOres] = {
            {62,112,15,Kind::Gold,200,3.5f,false}, {112,86,10,Kind::Gold,100,1.7f,false},
            {215,118,16,Kind::Gold,200,3.5f,false}, {265,88,9,Kind::Gold,100,1.6f,false},
            {160,170,8,Kind::Diamond,250,0.9f,false}, {65,177,17,Kind::Gold,200,4.0f,false},
            {245,177,11,Kind::Gold,100,1.8f,false}, {31,147,12,Kind::Rock,20,4.6f,false},
            {289,147,12,Kind::Rock,20,4.6f,false}, {160,110,8,Kind::Diamond,250,0.9f,false}
        };
        oreCount = remaining = MaxOres; target = goals[level - 1];
        elapsedMs = swingMs = 0; lastGain = 0; angle = 0; caught = -1; ropeLength = RestLength;
        pausedState = State::Playing;
        for (int i = 0; i < oreCount; ++i) {
            ores[i] = layout[i];
            if (i != 4 && i != 9) {
                ores[i].x += (int)((random() >> 8) % 9) - 4;
                ores[i].y += (int)((random() >> 8) % 7) - 3;
                if (level == 2) ores[i].x = 320 - ores[i].x;
            }
        }
        updateHook();
    }
    int firstContact(float from, float to, float &contact) const {
        const float dx = sinf(angle), dy = cosf(angle);
        int found = -1; contact = to + 1;
        for (int i = 0; i < oreCount; ++i) {
            const Ore &ore = ores[i]; if (ore.removed) continue;
            const float ox = ore.x - OriginX, oy = ore.y - OriginY;
            const float projection = ox * dx + oy * dy;
            const float radius = ore.radius + HookRadius;
            float perpendicularSq = ox * ox + oy * oy - projection * projection;
            if (perpendicularSq > radius * radius) continue;
            if (perpendicularSq < 0) perpendicularSq = 0;
            const float halfChord = sqrtf(radius * radius - perpendicularSq);
            const float enter = projection - halfChord, leave = projection + halfChord;
            if (leave < from || enter > to) continue;
            const float hit = enter < from ? from : enter;
            if (hit < contact) { contact = hit; found = i; }
        }
        return found;
    }
    void retrieved() {
        if (caught >= 0 && caught < oreCount && !ores[caught].removed) {
            ores[caught].removed = true; lastGain = ores[caught].value;
            score += lastGain; if (score > best) best = score;
            --remaining;
        }
        caught = -1; ropeLength = RestLength; updateHook();
        if (score >= target) state = level == 3 ? State::Won : State::LevelCleared;
        else if (!remaining) state = State::GameOver;
        else state = State::Playing;
    }
    void advance(float seconds) {
        // A cast can become a reel and finish during one delayed tick. Consume
        // the exact travel time at each transition instead of jumping through
        // ore or awarding it before the hook has returned to the winch.
        for (int transitions = 0; seconds > 0 && active() && transitions < 4; ++transitions) {
            if (state == State::Playing) {
                swingMs += (uint32_t)(seconds * 1000.0f + 0.5f);
                angle = sinf((float)(swingMs % 4000u) * 0.001570796327f) * 1.10f;
                updateHook(); return;
            }
            if (state == State::Casting) {
                const float boundary = maximumLength();
                float end = ropeLength + CastSpeed * seconds;
                if (end > boundary) end = boundary;
                float contact;
                const int hit = firstContact(ropeLength, end, contact);
                const float destination = hit >= 0 ? contact : end;
                const float used = (destination - ropeLength) / CastSpeed;
                ropeLength = destination; updateHook();
                if (hit >= 0 || ropeLength >= boundary - 0.001f) {
                    caught = hit; state = State::Reeling;
                    seconds -= used; if (seconds < 0) seconds = 0;
                } else return;
            } else {
                const float speed = reelSpeed();
                const float used = (ropeLength - RestLength) / speed;
                if (used <= seconds) {
                    seconds -= used; retrieved();
                } else {
                    ropeLength -= speed * seconds; updateHook(); return;
                }
            }
        }
    }
};
}  // namespace MinerEngine
