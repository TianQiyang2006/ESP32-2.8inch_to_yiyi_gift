#pragma once
#include <stdint.h>

// Fixed pools and a 10ms simulation keep collision and firing independent of
// display cadence. All positions are local to the 320x208 playfield.
namespace PlaneEngine {
constexpr int Width = 320, Height = 208;
constexpr int MaxShots = 20, MaxHostile = 32, MaxEnemies = 8;
constexpr int MinX = 12, MaxX = 308, MinY = 30, MaxY = 194;
constexpr uint16_t StepMs = 10, FireIntervalMs = 180, InvincibleMs = 1200;
enum class State : uint8_t { Ready, Playing, Paused, Won, GameOver };

struct Bullet {
    float x = 0, y = 0, vx = 0, vy = 0;
    bool active = false;
};
struct Enemy {
    float x = 0, y = 0, vx = 0, vy = 0;
    uint16_t fireMs = 0;
    uint8_t hp = 0, kind = 0; // 0 fighter, 1 armoured fighter, 2 boss
    bool active = false;
};

inline float clamp(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}
// Inclusive segment/box collision, also used in relative coordinates for two
// moving bodies. A fast bullet cannot jump past a thin collision rectangle.
inline bool swept(float x0, float y0, float x1, float y1,
                  float left, float top, float right, float bottom) {
    float enter = 0, leave = 1;
    const float from[2] = {x0, y0}, delta[2] = {x1-x0, y1-y0};
    const float lo[2] = {left, top}, hi[2] = {right, bottom};
    for (int axis = 0; axis < 2; ++axis) {
        if (delta[axis] > -0.00001f && delta[axis] < 0.00001f) {
            if (from[axis] < lo[axis] || from[axis] > hi[axis]) return false;
        } else {
            float a = (lo[axis]-from[axis])/delta[axis], b = (hi[axis]-from[axis])/delta[axis];
            if (a > b) { const float swap = a; a = b; b = swap; }
            if (a > enter) enter = a;
            if (b < leave) leave = b;
            if (enter > leave) return false;
        }
    }
    return true;
}
inline float halfWidth(const Enemy &enemy) { return enemy.kind == 2 ? 29.f : enemy.kind == 1 ? 14.f : 11.f; }
inline float halfHeight(const Enemy &enemy) { return enemy.kind == 2 ? 16.f : enemy.kind == 1 ? 11.f : 9.f; }

struct Game {
    Bullet shots[MaxShots], hostile[MaxHostile];
    Enemy enemies[MaxEnemies];
    State state = State::Ready;
    float shipX = 160, shipY = 180;
    uint32_t score = 0, best = 0, elapsedMs = 0, lastAt = 0, randomState = 1;
    uint16_t kills = 0, invulnerableMs = 0, stepRemainder = 0, shotMs = FireIntervalMs-StepMs, spawnMs = 0;
    uint8_t lives = 3, wave = 1, spawned = 0;
    bool dragging = false;
    float dragOriginX = 0, dragOriginY = 0, shipOriginX = 0, shipOriginY = 0;

    uint32_t random() { randomState = randomState*1664525u+1013904223u; return randomState; }
    unsigned aliveEnemies() const { unsigned n = 0; for (const auto &v : enemies) n += v.active; return n; }
    unsigned shotCount() const { unsigned n = 0; for (const auto &v : shots) n += v.active; return n; }
    unsigned hostileCount() const { unsigned n = 0; for (const auto &v : hostile) n += v.active; return n; }
    unsigned quota() const { return wave <= 3 ? 6u+2u*wave : 1u; }
    uint16_t spawnInterval() const { return (uint16_t)(1250-125*wave); }
    void prepare(uint32_t seed) {
        const uint32_t record = best;
        *this = Game(); best = record; randomState = seed ? seed : 1;
    }
    void start(uint32_t now, uint32_t seed) { prepare(seed); state = State::Playing; lastAt = now; }
    void restart(uint32_t now) { start(now, randomState^0x9E3779B9u); }
    bool beginDrag(int x, int y) {
        if (state != State::Playing || dragging) return false;
        dragging = true; dragOriginX = (float)x; dragOriginY = (float)y;
        shipOriginX = shipX; shipOriginY = shipY;
        return true;
    }
    bool endDrag() { const bool was = dragging; dragging = false; return was; }
    bool drag(int x, int y) {
        if (state != State::Playing || !dragging) return false;
        const float nx = clamp(shipOriginX+x-dragOriginX, MinX, MaxX);
        const float ny = clamp(shipOriginY+y-dragOriginY, MinY, MaxY);
        const float ox = shipX, oy = shipY;
        shipX = nx; shipY = ny;
        // Touch samples can span many pixels. Moving through a threat counts
        // as contact, just like a moving bullet crossing the stationary ship.
        for (auto &bullet : hostile) if (bullet.active && swept(ox, oy, nx, ny,
                bullet.x-8, bullet.y-10, bullet.x+8, bullet.y+10)) {
            bullet.active = false; damage();
        }
        for (auto &enemy : enemies) if (enemy.active && swept(ox, oy, nx, ny,
                enemy.x-halfWidth(enemy)-5, enemy.y-halfHeight(enemy)-7,
                enemy.x+halfWidth(enemy)+5, enemy.y+halfHeight(enemy)+7)) {
            if (enemy.kind != 2) enemy.active = false;
            damage();
        }
        return nx != ox || ny != oy;
    }
    bool pause(uint32_t now) {
        if (state != State::Playing) return false;
        tick(now);
        if (state != State::Playing) return false;
        state = State::Paused; endDrag(); return true;
    }
    bool resume(uint32_t now) {
        if (state != State::Paused) return false;
        lastAt = now; state = State::Playing; endDrag(); return true;
    }
    bool tick(uint32_t now) {
        if (state != State::Playing) return false;
        uint32_t delta = now-lastAt; lastAt = now;
        // A stalled display/USB transfer should not force seconds of unseen
        // damage or monopolise the ESP loop. Normal timer cadence is 20ms.
        if (delta > 1000) delta = 1000;
        delta += stepRemainder;
        stepRemainder = (uint16_t)(delta%StepMs);
        bool changed = false;
        while (delta >= StepMs && state == State::Playing) {
            step(); delta -= StepMs; changed = true;
        }
        return changed;
    }

private:
    void damage() {
        if (state != State::Playing || invulnerableMs || !lives) return;
        --lives; invulnerableMs = InvincibleMs;
        if (!lives) { state = State::GameOver; endDrag(); }
    }
    void fire() {
        for (auto &bullet : shots) if (!bullet.active) {
            bullet.x = shipX; bullet.y = shipY-14; bullet.vx = 0; bullet.vy = -240; bullet.active = true; return;
        }
    }
    void enemyBullet(float x, float y, float vx, float vy) {
        for (auto &bullet : hostile) if (!bullet.active) {
            bullet.x = x; bullet.y = y; bullet.vx = vx; bullet.vy = vy; bullet.active = true; return;
        }
    }
    bool spawn() {
        for (auto &enemy : enemies) if (!enemy.active) {
            enemy = Enemy(); enemy.active = true;
            enemy.kind = wave == 4 ? 2 : wave == 3 && spawned%3 == 2 ? 1 : 0;
            enemy.hp = enemy.kind == 2 ? 48 : enemy.kind == 1 ? 2 : 1;
            enemy.x = spawned == 0 ? 160.f : (float)(28+(random()>>8)%265);
            enemy.y = enemy.kind == 2 ? -20.f : -12.f;
            enemy.vy = enemy.kind == 2 ? 25.f : (float)(25+wave*5);
            enemy.vx = enemy.kind == 2 ? 58.f : spawned == 0 ? 0.f : (float)((int)((random()>>8)%3)-1)*12;
            ++spawned; return true;
        }
        return false;
    }
    void nextWave() {
        ++wave; spawned = 0; spawnMs = 0;
        for (auto &bullet : hostile) bullet.active = false;
        if (wave == 4) spawn();
    }
    void step() {
        elapsedMs += StepMs;
        invulnerableMs = invulnerableMs > StepMs ? (uint16_t)(invulnerableMs-StepMs) : 0;
        shotMs += StepMs;
        if (shotMs >= FireIntervalMs) { shotMs -= FireIntervalMs; fire(); }
        if (wave < 4 && spawned < quota()) {
            spawnMs += StepMs;
            const uint16_t due = spawned ? spawnInterval() : 400;
            if (spawnMs >= due && spawn()) spawnMs -= due;
        }
        float oldX[MaxEnemies], oldY[MaxEnemies];
        for (int i = 0; i < MaxEnemies; ++i) {
            auto &enemy = enemies[i]; oldX[i] = enemy.x; oldY[i] = enemy.y;
            if (!enemy.active) continue;
            enemy.x += enemy.vx*.01f; enemy.y += enemy.vy*.01f;
            const float edge = halfWidth(enemy)+3;
            if (enemy.x < edge) { enemy.x = edge; if (enemy.vx < 0) enemy.vx = -enemy.vx; }
            if (enemy.x > Width-edge) { enemy.x = Width-edge; if (enemy.vx > 0) enemy.vx = -enemy.vx; }
            if (enemy.kind == 2 && enemy.y >= 44) { enemy.y = 44; enemy.vy = 0; }
            enemy.fireMs += StepMs;
            const uint16_t interval = enemy.kind == 2 ? 720 : (uint16_t)(1550-wave*140);
            if (enemy.fireMs >= interval && enemy.y >= 20 && enemy.y <= Height) {
                enemy.fireMs -= interval;
                if (enemy.kind == 2) {
                    enemyBullet(enemy.x-16, enemy.y+15, -28, 96);
                    enemyBullet(enemy.x, enemy.y+17, 0, 108);
                    enemyBullet(enemy.x+16, enemy.y+15, 28, 96);
                } else enemyBullet(enemy.x, enemy.y+12, 0, (float)(75+12*wave));
            }
        }
        for (auto &bullet : shots) {
            if (!bullet.active) continue;
            const float ox = bullet.x, oy = bullet.y;
            bullet.x += bullet.vx*.01f; bullet.y += bullet.vy*.01f;
            for (int i = 0; i < MaxEnemies; ++i) {
                auto &enemy = enemies[i];
                if (!enemy.active || !swept(ox-oldX[i], oy-oldY[i], bullet.x-enemy.x, bullet.y-enemy.y,
                        -halfWidth(enemy)-2, -halfHeight(enemy)-4, halfWidth(enemy)+2, halfHeight(enemy)+4)) continue;
                bullet.active = false;
                if (--enemy.hp == 0) {
                    enemy.active = false; ++kills;
                    score += enemy.kind == 2 ? 200u : enemy.kind == 1 ? 20u : 10u;
                    if (score > best) best = score;
                    if (enemy.kind == 2) {
                        state = State::Won; endDrag();
                        for (auto &other : hostile) other.active = false;
                    }
                }
                break;
            }
            if (bullet.y < -10 || bullet.x < -10 || bullet.x > Width+10) bullet.active = false;
        }
        if (state != State::Playing) return;
        for (auto &bullet : hostile) {
            if (!bullet.active) continue;
            const float ox = bullet.x, oy = bullet.y;
            bullet.x += bullet.vx*.01f; bullet.y += bullet.vy*.01f;
            if (swept(ox, oy, bullet.x, bullet.y, shipX-8, shipY-10, shipX+8, shipY+10)) {
                bullet.active = false; damage();
            }
            if (bullet.y > Height+8 || bullet.x < -8 || bullet.x > Width+8) bullet.active = false;
        }
        for (int i = 0; i < MaxEnemies; ++i) {
            auto &enemy = enemies[i];
            if (!enemy.active) continue;
            if (swept(oldX[i], oldY[i], enemy.x, enemy.y,
                    shipX-halfWidth(enemy)-5, shipY-halfHeight(enemy)-7,
                    shipX+halfWidth(enemy)+5, shipY+halfHeight(enemy)+7)) {
                if (enemy.kind != 2) enemy.active = false;
                damage();
            }
            if (enemy.y > Height+halfHeight(enemy)) enemy.active = false;
        }
        if (state == State::Playing && wave < 4 && spawned == quota() && !aliveEnemies()) nextWave();
    }
};
} // namespace PlaneEngine
