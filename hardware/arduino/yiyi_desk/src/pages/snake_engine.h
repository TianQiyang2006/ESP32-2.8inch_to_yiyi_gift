#pragma once
#include <stdint.h>

// Fixed-size rules shared by the ESP32 page and the native regression runner.
// Body cells are stored head first; no heap allocation or display code is used.
namespace SnakeEngine {
constexpr int Columns = 20, Rows = 13, Capacity = Columns * Rows;
enum class State { Ready, Playing, Paused, GameOver, Won };
enum class Direction { Up, Right, Down, Left };
struct Cell { int8_t x, y; };
inline bool operator==(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Cell a, Cell b) { return !(a == b); }

struct Game {
    Cell body[Capacity] = {};
    uint16_t length = 0;
    Cell food = {-1, -1};
    Direction direction = Direction::Right;
    State state = State::Ready;
    uint32_t score = 0, best = 0, lastStepAt = 0, randomState = 1;
    uint8_t speed = 1;
    Direction queued[2] = {Direction::Right, Direction::Right};
    uint8_t queuedCount = 0;

    uint32_t random() {
        // A full-period 32-bit LCG also accepts a zero seed.
        randomState = randomState * 1664525u + 1013904223u;
        return randomState;
    }
    bool occupied(Cell cell, bool includeTail = true) const {
        const uint16_t count = length - ((!includeTail && length) ? 1 : 0);
        for (uint16_t i = 0; i < count; ++i)
            if (body[i] == cell) return true;
        return false;
    }
    bool spawnFood() {
        uint16_t freeCells = 0;
        for (int y = 0; y < Rows; ++y)
            for (int x = 0; x < Columns; ++x)
                if (!occupied({(int8_t)x, (int8_t)y})) ++freeCells;
        if (!freeCells) {
            food = {-1, -1};
            state = State::Won;
            return false;
        }
        // Select a rank among free cells, then scan once. Rejection removes the
        // modulo bias and never searches for a random unoccupied coordinate.
        const uint32_t threshold = (0u - (uint32_t)freeCells) % freeCells;
        uint32_t value;
        do { value = random(); } while (value < threshold);
        uint16_t rank = (uint16_t)(value % freeCells);
        for (int y = 0; y < Rows; ++y)
            for (int x = 0; x < Columns; ++x) {
                const Cell candidate = {(int8_t)x, (int8_t)y};
                if (occupied(candidate)) continue;
                if (!rank--) { food = candidate; return true; }
            }
        return false;
    }
    void prepare(uint32_t seed) {
        length = 4;
        for (uint16_t i = 0; i < length; ++i)
            body[i] = {(int8_t)(Columns / 2 - i), (int8_t)(Rows / 2)};
        direction = Direction::Right;
        queuedCount = 0;
        score = 0;
        lastStepAt = 0;
        randomState = seed;
        if (speed > 2) speed = 1;
        state = State::Ready;
        spawnFood();
    }
    void start(uint32_t now, uint32_t seed) {
        prepare(seed);
        state = State::Playing;
        lastStepAt = now;
    }
    void restart(uint32_t now) { start(now, randomState); }
    bool turn(Direction value) {
        if (state != State::Playing || (unsigned)value > 3 || queuedCount >= 2) return false;
        const Direction previous = queuedCount ? queued[queuedCount - 1] : direction;
        if (value == previous || (((unsigned)value + 2) % 4) == (unsigned)previous) return false;
        queued[queuedCount++] = value;
        return true;
    }
    uint16_t intervalMs() const {
        static const uint16_t base[] = {240, 180, 120};
        const uint32_t initial = base[speed > 2 ? 1 : speed];
        const uint32_t reduction = score / 50 * 10;
        return (uint16_t)(reduction >= initial - 90 ? 90 : initial - reduction);
    }
    bool step() {
        if (state != State::Playing || !length) return false;
        if (queuedCount) {
            direction = queued[0];
            queued[0] = queued[1];
            --queuedCount;
        }
        Cell next = body[0];
        switch (direction) {
            case Direction::Up: --next.y; break;
            case Direction::Right: ++next.x; break;
            case Direction::Down: ++next.y; break;
            case Direction::Left: --next.x; break;
        }
        const bool growing = next == food;
        if (next.x < 0 || next.x >= Columns || next.y < 0 || next.y >= Rows || occupied(next, growing)) {
            state = State::GameOver;
            queuedCount = 0;
            return true;
        }
        if (growing && length < Capacity) ++length;
        for (uint16_t i = length - 1; i; --i) body[i] = body[i - 1];
        body[0] = next;
        if (growing) {
            score += 10;
            if (score > best) best = score;
            spawnFood();
        }
        return true;
    }
    bool tick(uint32_t now) {
        if (state != State::Playing || (uint32_t)(now - lastStepAt) < intervalMs()) return false;
        // Never replay overdue steps after a modal, network stall or redraw.
        lastStepAt = now;
        return step();
    }
    void pause() { if (state == State::Playing) state = State::Paused; }
    void resume(uint32_t now) {
        if (state != State::Paused) return;
        queuedCount = 0;
        lastStepAt = now;
        state = State::Playing;
    }
};
}  // namespace SnakeEngine
