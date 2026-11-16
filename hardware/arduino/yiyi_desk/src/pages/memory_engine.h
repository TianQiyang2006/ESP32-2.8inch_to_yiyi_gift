#pragma once
#include <stdint.h>

namespace MemoryEngine {
constexpr uint8_t MaxCards = 20;
constexpr uint16_t CompareMs = 750;
enum class State : uint8_t { Ready, Playing, Comparing, Paused, LevelCleared, Won };
struct Card { uint8_t face = 0; bool matched = false; };

// Pair matching is independent of the page. A mismatch stays visible for a
// measured interval; paused time never consumes that interval or the clock.
struct Game {
    Card cards[MaxCards];
    State state = State::Ready, pausedState = State::Playing;
    uint32_t score = 0, best = 0, elapsedMs = 0, lastAt = 0, randomState = 1;
    uint16_t moves = 0, compareRemaining = 0;
    uint8_t level = 1, pairs = 0, totalPairs = 6, count = 12;
    int8_t first = -1, second = -1;
    uint32_t random() { randomState = randomState * 1664525u + 1013904223u; return randomState; }
    bool active() const { return state == State::Playing || state == State::Comparing; }
    bool visible(int i) const { return i >= 0 && i < count && (cards[i].matched || i == first || i == second); }
    int rows() const { return (count + 3) / 4; }
    int top() const { return 28 + (180 - rows() * 36) / 2; }
    int hitTest(int x, int y) const {
        const int col = (x - 23) / 70, row = (y - top()) / 36;
        if (x < 23 || y < top() || col >= 4 || row >= rows()) return -1;
        if ((x - 23) % 70 >= 64 || (y - top()) % 36 >= 30) return -1;
        const int index = row * 4 + col;
        return index < count ? index : -1;
    }
    void prepare(uint32_t seed) {
        const uint32_t saved = best; *this = Game(); best = saved;
        randomState = seed; deal(); state = State::Ready;
    }
    void start(uint32_t now, uint32_t seed) { prepare(seed); state = State::Playing; lastAt = now; }
    void restart(uint32_t now) { start(now, randomState); }
    bool nextLevel(uint32_t now) {
        if (state != State::LevelCleared || level >= 3) return false;
        ++level; deal(); state = State::Playing; lastAt = now; return true;
    }
    bool tick(uint32_t now) {
        if (!active()) return false;
        const uint32_t passed = now - lastAt;
        lastAt = now; elapsedMs += passed;
        if (state == State::Comparing && passed >= compareRemaining) {
            compareRemaining = 0; first = second = -1; state = State::Playing; return true;
        }
        if (state == State::Comparing) compareRemaining -= (uint16_t)passed;
        return passed != 0;
    }
    bool select(int index, uint32_t now) {
        tick(now);
        if (state != State::Playing || index < 0 || index >= count || cards[index].matched || index == first) return false;
        if (first < 0) { first = (int8_t)index; return true; }
        second = (int8_t)index; ++moves;
        if (cards[first].face != cards[second].face) {
            state = State::Comparing; compareRemaining = CompareMs; return true;
        }
        cards[first].matched = cards[second].matched = true;
        ++pairs; score += 100; first = second = -1;
        if (pairs == totalPairs) {
            const uint32_t penalty = (uint32_t)(moves - totalPairs) * 10;
            if (penalty < 200) score += 200 - penalty;
            state = level == 3 ? State::Won : State::LevelCleared;
        }
        if (score > best) best = score;
        return true;
    }
    bool pause(uint32_t now) {
        if (!active()) return false;
        tick(now); pausedState = state; state = State::Paused; return true;
    }
    bool resume(uint32_t now) {
        if (state != State::Paused) return false;
        state = pausedState; lastAt = now; return true;
    }
private:
    void deal() {
        totalPairs = (uint8_t)(4 + level * 2); count = totalPairs * 2;
        pairs = 0; moves = 0; first = second = -1; compareRemaining = 0;
        elapsedMs = 0; pausedState = State::Playing;
        for (int i = 0; i < MaxCards; ++i) { cards[i].face = (uint8_t)(i / 2); cards[i].matched = false; }
        for (int i = count - 1; i > 0; --i) {
            const int j = (int)((random() >> 8) % (unsigned)(i + 1));
            const Card temp = cards[i]; cards[i] = cards[j]; cards[j] = temp;
        }
    }
};
} // namespace MemoryEngine
