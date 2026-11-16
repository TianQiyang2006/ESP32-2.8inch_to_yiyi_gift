#pragma once
// Pure Tetris rules for the hand-written game page (src/pages/tetris_page.cpp).
//
// This header deliberately has no Arduino/LVGL dependency: the host regression
// (hardware/tests/host/tetris_engine_test.cpp) pins the rules, and the page only
// renders the state it exposes. Fields are public on purpose, exactly like
// src/pages/focus_engine.h: the UI reads `board`/`piece`/`next` directly, and the
// offscreen screenshot runner sets a deterministic scenario through them.
//
// Board convention: board[row][col], row 0 is the top row, col 0 the left column.
// A cell holds 0 when empty or the piece type (1..7) that was locked there.
#include <stdint.h>
#include <string.h>

namespace TetrisEngine {

constexpr int Columns = 10;
// 16 visible rows: the 320x240 landscape screen cannot show 20 rows and keep the
// blocks big; a shorter field is the trade-off for a readable, chunky board.
constexpr int Rows = 16;

enum class State : uint8_t { Ready, Playing, Paused, GameOver };

// Piece types, also used as board cell values and palette indices.
enum class Piece : uint8_t { None = 0, I = 1, O = 2, T = 3, S = 4, Z = 5, J = 6, L = 7 };
constexpr int PieceTypes = 7;

inline int pieceBox(uint8_t type) { return type == (uint8_t)Piece::I ? 4 : type == (uint8_t)Piece::O ? 2 : 3; }

// Base orientation, row bit (box-1-col) set means the cell is filled.
inline uint8_t baseRow(uint8_t type, int row) {
    switch (type) {
        case 1: return row == 1 ? 0xF : 0x0;                       // I: horizontal bar
        case 2: return row < 2 ? 0x3 : 0x0;                        // O
        case 3: { static const uint8_t r[3] = {0x2, 0x7, 0x0}; return r[row]; }  // T
        case 4: { static const uint8_t r[3] = {0x3, 0x6, 0x0}; return r[row]; }  // S
        case 5: { static const uint8_t r[3] = {0x6, 0x3, 0x0}; return r[row]; }  // Z
        case 6: { static const uint8_t r[3] = {0x4, 0x7, 0x0}; return r[row]; }  // J
        case 7: { static const uint8_t r[3] = {0x1, 0x7, 0x0}; return r[row]; }  // L
    }
    return 0;
}

inline bool baseCell(uint8_t type, int row, int col) {
    const int n = pieceBox(type);
    if (row < 0 || row >= n || col < 0 || col >= n) return false;
    return (baseRow(type, row) & (1u << (n - 1 - col))) != 0;
}

// Clockwise rotation inside the piece's own box: new[row][col] = old[n-1-col][row].
inline bool shapeCell(uint8_t type, uint8_t rotation, int row, int col) {
    if (type < 1 || type > PieceTypes || rotation > 3) return false;
    const int n = pieceBox(type);
    for (int turn = 0; turn < rotation; ++turn) {
        const int nextRow = n - 1 - col, nextCol = row;
        row = nextRow;
        col = nextCol;
    }
    return baseCell(type, row, col);
}

inline int spawnX(uint8_t type) {
    return type == (uint8_t)Piece::O ? 4 : 3;
}

// Gravity interval per level (1..15). Fast enough to stay playable, slow enough
// that the first levels remain comfortable on the touch pad.
inline uint32_t intervalMs(uint8_t level) {
    static const uint16_t table[15] = {800, 720, 630, 550, 470, 380, 300, 220,
                                       130, 100, 80, 70, 60, 50, 40};
    return table[level < 1 ? 0 : level > 15 ? 14 : level - 1];
}

inline uint32_t nextRandom(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

struct Game {
    State state = State::Ready;
    uint8_t board[Rows][Columns]{};   // locked cells, 0 or a Piece value
    uint8_t piece = 0, next = 0;      // active and upcoming piece types
    uint8_t rotation = 0;
    int8_t x = 3, y = 0;
    uint32_t score = 0, best = 0;
    uint16_t lines = 0;
    uint8_t level = 1;
    uint32_t nextDropMs = 0;
    // Internal: deterministic 7-bag randomiser and the clock used by gravity.
    uint32_t rng = 0x1D2C3B4Au;
    uint8_t bag[PieceTypes]{}; uint8_t bagPos = PieceTypes;
    uint32_t nowMs = 0;

    void reset(uint32_t seed) {
        for (int r = 0; r < Rows; ++r)
            for (int c = 0; c < Columns; ++c) board[r][c] = 0;
        score = 0; lines = 0; level = 1;
        piece = next = 0; rotation = 0; x = 3; y = 0;
        rng = seed ? seed : 0x1D2C3B4Au;
        bagPos = PieceTypes;
        state = State::Ready;
        nextDropMs = 0;
    }

    uint8_t drawPiece() {
        if (bagPos >= PieceTypes) {
            for (int i = 0; i < PieceTypes; ++i) bag[i] = (uint8_t)(i + 1);
            for (int i = PieceTypes - 1; i > 0; --i) {
                const uint32_t pick = nextRandom(rng) % (uint32_t)(i + 1);
                const uint8_t swap = bag[i]; bag[i] = bag[pick]; bag[pick] = swap;
            }
            bagPos = 0;
        }
        return bag[bagPos++];
    }

    bool fits(int testX, int testY, uint8_t testRotation) const {
        const int n = pieceBox(piece);
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c) {
                if (!shapeCell(piece, testRotation, r, c)) continue;
                const int bx = testX + c, by = testY + r;
                if (bx < 0 || bx >= Columns || by >= Rows) return false;
                if (by >= 0 && board[by][bx] != 0) return false;
            }
        return true;
    }

    void spawn() {
        piece = next;
        next = drawPiece();
        rotation = 0;
        x = (int8_t)spawnX(piece);
        y = 0;
        if (!fits(x, y, rotation)) {
            state = State::GameOver;
            if (score > best) best = score;
        }
    }

    void start(uint32_t now, uint32_t seed) {
        reset(seed);
        state = State::Playing;
        nowMs = now;
        next = drawPiece();
        spawn();
        nextDropMs = now + intervalMs(level);
    }

    void restart(uint32_t now) { start(now, rng ? rng : 1u); }

    void pause() { if (state == State::Playing) state = State::Paused; }
    void resume(uint32_t now) {
        if (state != State::Paused) return;
        state = State::Playing;
        nowMs = now;
        nextDropMs = now + intervalMs(level);
    }

    void addScore(uint32_t value) {
        score += value;
        if (score > best) best = score;
    }

    // Locks the active piece, clears complete rows, spawns the next piece.
    void lock() {
        const int n = pieceBox(piece);
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                if (shapeCell(piece, rotation, r, c)) {
                    const int by = y + r, bx = x + c;
                    if (by >= 0 && by < Rows && bx >= 0 && bx < Columns) board[by][bx] = piece;
                }
        int full = 0;
        for (int row = Rows - 1; row >= 0; --row) {
            bool complete = true;
            for (int col = 0; col < Columns; ++col) if (!board[row][col]) { complete = false; break; }
            if (!complete) continue;
            ++full;
            for (int move = row; move > 0; --move) memcpy(board[move], board[move - 1], Columns);
            memset(board[0], 0, Columns);
            ++row;  // re-check the row that just moved down
        }
        if (full) {
            static const uint16_t points[5] = {0, 100, 300, 500, 800};
            addScore((uint32_t)points[full > 4 ? 4 : full] * level);
            lines = (uint16_t)(lines + full);
            level = (uint8_t)(1 + lines / 10);
            if (level > 15) level = 15;
        }
        spawn();
        nextDropMs = nowMs + intervalMs(level);
    }

    bool moveLeft() {
        if (state != State::Playing || !fits(x - 1, y, rotation)) return false;
        --x; return true;
    }
    bool moveRight() {
        if (state != State::Playing || !fits(x + 1, y, rotation)) return false;
        ++x; return true;
    }
    // Tries rotation with a small wall/floor kick set; true when the piece turned.
    // The two-row vertical kicks let a horizontal I piece rotate while resting on
    // the floor (the I is four rows tall when vertical and needs two rows of room).
    bool rotate() {
        if (state != State::Playing) return false;
        static const int8_t kicks[11][2] = {{0, 0}, {-1, 0}, {1, 0}, {-2, 0}, {2, 0},
                                            {0, -1}, {-1, -1}, {1, -1},
                                            {0, -2}, {-1, -2}, {1, -2}};
        const uint8_t turned = (uint8_t)((rotation + 1) & 3);
        for (int i = 0; i < 11; ++i) {
            const int testX = x + kicks[i][0], testY = y + kicks[i][1];
            if (testY < 0) continue;
            if (!fits(testX, testY, turned)) continue;
            rotation = turned; x = (int8_t)testX; y = (int8_t)testY;
            return true;
        }
        return false;
    }

    bool softDrop() {
        if (state != State::Playing) return false;
        if (fits(x, y + 1, rotation)) {
            ++y; addScore(1); nextDropMs = nowMs + intervalMs(level); return true;
        }
        lock();
        return true;
    }

    int hardDrop() {
        if (state != State::Playing) return 0;
        int distance = 0;
        while (fits(x, y + 1, rotation)) { ++y; ++distance; }
        if (distance) addScore((uint32_t)distance * 2);
        lock();
        return distance;
    }

    // Gravity. Returns true when the visible state changed.
    bool tick(uint32_t now) {
        nowMs = now;
        if (state != State::Playing) return false;
        if (nextDropMs == 0) { nextDropMs = now + intervalMs(level); return false; }
        bool changed = false;
        int guard = 0;
        while ((int32_t)(now - nextDropMs) >= 0 && guard++ < 4) {
            if (fits(x, y + 1, rotation)) {
                ++y; changed = true;
                nextDropMs += intervalMs(level);
            } else {
                lock(); changed = true;
                break;
            }
        }
        if (guard >= 4) nextDropMs = now + intervalMs(level);
        return changed;
    }

    // Landing row of the active piece, for the ghost outline.
    int ghostY() const {
        if (state != State::Playing) return y;
        int row = y;
        while (true) {
            bool free = true;
            const int n = pieceBox(piece);
            for (int r = 0; r < n && free; ++r)
                for (int c = 0; c < n; ++c) {
                    if (!shapeCell(piece, rotation, r, c)) continue;
                    const int bx = x + c, by = row + 1 + r;
                    if (bx < 0 || bx >= Columns || by >= Rows || board[by][bx]) { free = false; break; }
                }
            if (!free) return row;
            ++row;
        }
    }

    // Number of complete rows (used by the host test to assert line clears).
    int completedRows() const {
        int count = 0;
        for (int row = 0; row < Rows; ++row) {
            bool complete = true;
            for (int col = 0; col < Columns; ++col) if (!board[row][col]) { complete = false; break; }
            if (complete) ++count;
        }
        return count;
    }
};

}  // namespace TetrisEngine
