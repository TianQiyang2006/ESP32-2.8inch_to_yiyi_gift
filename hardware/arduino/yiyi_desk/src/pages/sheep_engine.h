#pragma once
#include <stdint.h>

// Fixed-size layered triple-match rules. Coordinates are local to the playfield;
// graphics, input routing and persistent storage remain the page's responsibility.
namespace SheepEngine {
constexpr int TileSize = 32, MaxTiles = 54, TrayCapacity = 7, TypeCount = 9;
enum class State { Ready, Playing, Paused, LevelCleared, Won, GameOver };
struct Tile {
    int16_t x, y;
    uint8_t layer, type;
    bool removed;
};

struct Game {
    Tile tiles[MaxTiles] = {};
    uint8_t tileCount = 0, tray[TrayCapacity] = {}, trayCount = 0, level = 1, remaining = 0;
    // For an initial deal, proof lists a complete exposed-tile winning order.
    // It is deliberately invalidated after shuffle: shuffle promises a new
    // permutation, not a certified solution for an arbitrary partial tray.
    uint8_t proof[MaxTiles] = {};
    uint32_t score = 0, best = 0, randomState = 1;
    State state = State::Ready;
    bool undoAvailable = false, shuffleAvailable = true, proofValid = true;

    uint32_t random() {
        randomState = randomState * 1664525u + 1013904223u;
        return randomState;
    }
    uint32_t bounded(uint32_t count) {
        const uint32_t threshold = (0u - count) % count;
        uint32_t value;
        do { value = random(); } while (value < threshold);
        return value % count;
    }
    static bool overlap(const Tile &a, const Tile &b) {
        return a.x < b.x + TileSize && a.x + TileSize > b.x &&
               a.y < b.y + TileSize && a.y + TileSize > b.y;
    }
    bool exposed(int id) const {
        if (id < 0 || id >= tileCount || tiles[id].removed) return false;
        for (int i = 0; i < tileCount; ++i)
            if (!tiles[i].removed && tiles[i].layer > tiles[id].layer && overlap(tiles[id], tiles[i])) return false;
        return true;
    }
    bool canSelect(int id) const { return state == State::Playing && exposed(id); }
    int hitTest(int x, int y) const {
        int found = -1;
        for (int i = 0; i < tileCount; ++i) {
            const Tile &tile = tiles[i];
            if (tile.removed || x < tile.x || x >= tile.x + TileSize || y < tile.y || y >= tile.y + TileSize) continue;
            if (found < 0 || tile.layer >= tiles[found].layer) found = i;
        }
        return found;
    }
    void prepare(uint32_t seed) {
        randomState = seed; score = 0; level = 1;
        deal(); state = State::Ready;
    }
    void start(uint32_t seed) { prepare(seed); state = State::Playing; }
    void restart() { start(randomState); }
    bool nextLevel() {
        if (state != State::LevelCleared || level != 1) return false;
        level = 2; deal(); state = State::Playing;
        return true;
    }
    bool pause() {
        if (state != State::Playing) return false;
        state = State::Paused; return true;
    }
    bool resume() {
        if (state != State::Paused) return false;
        state = State::Playing; return true;
    }
    bool canUndo() const {
        return undoAvailable && undoUnused &&
               (state == State::Playing || state == State::GameOver || state == State::LevelCleared || state == State::Won);
    }
    bool select(int id) {
        if (!canSelect(id) || trayCount >= TrayCapacity) return false;
        if (undoUnused) {
            previous.id = (uint8_t)id; previous.trayCount = trayCount;
            for (int i = 0; i < TrayCapacity; ++i) previous.tray[i] = tray[i];
            previous.score = score; previous.remaining = remaining; previous.state = state;
            undoAvailable = true;
        }
        const uint8_t type = tiles[id].type;
        int insertion = trayCount;
        for (int i = 0; i < trayCount; ++i) if (tray[i] == type) insertion = i + 1;
        for (int i = trayCount; i > insertion; --i) tray[i] = tray[i - 1];
        tray[insertion] = type; ++trayCount;
        tiles[id].removed = true; --remaining;
        // Grouping makes any triple contiguous. Resolve it before deciding that
        // the seventh slot is a loss, so the seventh tile can rescue the tray.
        for (int i = 0; i + 2 < trayCount; ++i) {
            if (tray[i] != tray[i + 1] || tray[i] != tray[i + 2]) continue;
            for (int j = i; j + 3 < trayCount; ++j) tray[j] = tray[j + 3];
            trayCount -= 3;
            for (int j = trayCount; j < TrayCapacity; ++j) tray[j] = 0;
            score += 10; if (score > best) best = score;
            break;
        }
        if (trayCount >= TrayCapacity) state = State::GameOver;
        else if (!remaining) state = trayCount ? State::GameOver : level == 1 ? State::LevelCleared : State::Won;
        return true;
    }
    bool undo() {
        if (!canUndo()) return false;
        tiles[previous.id].removed = false;
        trayCount = previous.trayCount;
        for (int i = 0; i < TrayCapacity; ++i) tray[i] = previous.tray[i];
        score = previous.score; remaining = previous.remaining; state = previous.state;
        // A record describes the highest score ever reached, including the run
        // before this voluntary undo, and is never reduced by restoring a turn.
        undoUnused = false; undoAvailable = false;
        return true;
    }
    bool shuffle() {
        if (state != State::Playing || !shuffleAvailable || remaining < 2) return false;
        uint8_t ids[MaxTiles], count = 0;
        for (int i = 0; i < tileCount; ++i) if (!tiles[i].removed) ids[count++] = (uint8_t)i;
        for (int i = count - 1; i > 0; --i) {
            const int j = (int)bounded((uint32_t)i + 1);
            const uint8_t saved = tiles[ids[i]].type;
            tiles[ids[i]].type = tiles[ids[j]].type; tiles[ids[j]].type = saved;
        }
        shuffleAvailable = false; undoAvailable = false; proofValid = false;
        return true;
    }

private:
    struct Snapshot {
        uint8_t id = 0, tray[TrayCapacity] = {}, trayCount = 0, remaining = 0;
        uint32_t score = 0;
        State state = State::Playing;
    } previous;
    bool undoUnused = true;

    void addGrid(int columns, int rows, int x, int y, uint8_t layer) {
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < columns; ++col)
                tiles[tileCount++] = {(int16_t)(x + col * 36), (int16_t)(y + row * 36), layer, 0, false};
    }
    void deal() {
        tileCount = trayCount = 0;
        for (int i = 0; i < MaxTiles; ++i) { tiles[i] = {0, 0, 0, 0, true}; proof[i] = 0xFF; }
        for (int i = 0; i < TrayCapacity; ++i) tray[i] = 0;
        if (level == 1) {
            addGrid(4, 3, 90, 16, 0);
            addGrid(3, 2, 108, 34, 1);
        } else {
            addGrid(7, 3, 36, 4, 0);
            addGrid(6, 3, 54, 16, 1);
            addGrid(5, 3, 72, 28, 2);
        }
        remaining = tileCount;
        undoUnused = shuffleAvailable = proofValid = true; undoAvailable = false;
        // Higher-layer overlap forms an acyclic dependency graph. Randomly
        // remove an exposed tile at every step to obtain a certified legal order.
        for (int step = 0; step < tileCount; ++step) {
            uint8_t choices[MaxTiles], count = 0;
            for (int id = 0; id < tileCount; ++id) if (exposed(id)) choices[count++] = (uint8_t)id;
            const uint8_t chosen = choices[bounded(count)];
            proof[step] = chosen; tiles[chosen].removed = true;
        }
        for (int id = 0; id < tileCount; ++id) tiles[id].removed = false;
        // Each nine-pick block is a permutation of three triples. Such a block
        // cannot leave seven unmatched tiles: among seven picks one of its three
        // types has appeared three times and is removed before the loss check.
        for (int block = 0; block < tileCount; block += 9) {
            uint8_t types[TypeCount], picks[9];
            for (int i = 0; i < TypeCount; ++i) types[i] = (uint8_t)i;
            for (int i = TypeCount - 1; i > 0; --i) {
                const int j = (int)bounded((uint32_t)i + 1);
                const uint8_t saved = types[i]; types[i] = types[j]; types[j] = saved;
            }
            for (int i = 0; i < 9; ++i) picks[i] = types[i / 3];
            for (int i = 8; i > 0; --i) {
                const int j = (int)bounded((uint32_t)i + 1);
                const uint8_t saved = picks[i]; picks[i] = picks[j]; picks[j] = saved;
            }
            for (int i = 0; i < 9; ++i) tiles[proof[block + i]].type = picks[i];
        }
    }
};
}  // namespace SheepEngine
