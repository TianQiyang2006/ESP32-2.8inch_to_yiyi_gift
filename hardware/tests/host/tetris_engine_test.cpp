// Host regression: pure Tetris rules from src/pages/tetris_engine.h.
// No Arduino/LVGL sources are linked (same host bucket as nav_origin_test.cpp),
// so the deterministic schedule below is a real unit test of the game rules.
#include "src/pages/tetris_engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace TetrisEngine;

namespace {

int checks = 0;
int failures = 0;

void check(const char *what, bool condition) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

void fillRow(Game &game, int row, int first, int last, uint8_t value) {
    for (int col = first; col <= last; ++col) game.board[row][col] = value;
}

bool rowsClear(const Game &game, int first, int last) {
    for (int row = first; row <= last; ++row)
        for (int col = 0; col < Columns; ++col)
            if (game.board[row][col]) return false;
    return true;
}

// ---- piece geometry -------------------------------------------------------
void shapes() {
    for (int type = 1; type <= PieceTypes; ++type) {
        const int n = pieceBox(type);
        for (int rotation = 0; rotation < 4; ++rotation) {
            int cells = 0;
            for (int row = 0; row < n; ++row)
                for (int col = 0; col < n; ++col)
                    if (shapeCell((uint8_t)type, (uint8_t)rotation, row, col)) ++cells;
            check("every rotation keeps four cells", cells == 4);
        }
    }
    // Every piece must produce a different footprint in at least one rotation.
    check("I is horizontal at rotation 0", shapeCell(1, 0, 1, 0) && shapeCell(1, 0, 1, 3));
    check("I is vertical at rotation 1", shapeCell(1, 1, 0, 2) && shapeCell(1, 1, 3, 2));
    check("O never changes shape", shapeCell(2, 0, 0, 0) && shapeCell(2, 3, 1, 1));
    check("T points up at rotation 0", shapeCell(3, 0, 0, 1) && shapeCell(3, 0, 1, 0) && shapeCell(3, 0, 1, 2));
    check("T points right at rotation 1", shapeCell(3, 1, 0, 1) && shapeCell(3, 1, 1, 2) && shapeCell(3, 1, 2, 1));
    check("invalid piece type has no cells", !shapeCell(0, 0, 0, 0) && !shapeCell(8, 0, 0, 0));
}

// ---- randomiser -----------------------------------------------------------
void sevenBag() {
    Game game;
    game.reset(12345);
    game.start(1000, 12345);
    // The first piece was spawned, the second is queued: the next 5 draws complete
    // the first 7-bag, and each group of seven must contain every piece once.
    uint8_t first[7] = {game.piece, game.next, 0, 0, 0, 0, 0};
    for (int i = 2; i < 7; ++i) first[i] = game.drawPiece();
    bool seen[8] = {};
    for (int i = 0; i < 7; ++i) {
        check("bag piece in range", first[i] >= 1 && first[i] <= PieceTypes);
        check("bag has no duplicate", !seen[first[i]]);
        seen[first[i]] = true;
    }
    bool all = true;
    for (int type = 1; type <= PieceTypes; ++type) all = all && seen[type];
    check("first bag contains all seven types", all);
    uint8_t second[7];
    for (int i = 0; i < 7; ++i) second[i] = game.drawPiece();
    std::memset(seen, 0, sizeof(seen));
    for (int i = 0; i < 7; ++i) {
        check("second bag has no duplicate", !seen[second[i]]);
        seen[second[i]] = true;
    }
    // Determinism: same seed -> same piece stream.
    Game a, b;
    a.start(10, 777);
    b.start(10, 777);
    bool same = a.piece == b.piece && a.next == b.next;
    for (int i = 0; i < 20; ++i) same = same && a.drawPiece() == b.drawPiece();
    check("same seed yields the same stream", same);
}

// ---- gravity, drops and locks --------------------------------------------
void gravityAndDrops() {
    Game game;
    game.start(1000, 99);
    check("start is playing", game.state == State::Playing);
    check("start is empty", game.completedRows() == 0 && game.score == 0 && game.lines == 0 && game.level == 1);
    const int firstDrop = game.y;
    game.tick(1000 + intervalMs(1) - 1);
    check("gravity waits for the interval", game.y == firstDrop);
    game.tick(1000 + intervalMs(1));
    check("gravity drops one row", game.y == firstDrop + 1);

    // Soft drop: one row, one point, and the gravity timer restarts.
    const int beforeSoft = game.y;
    const uint32_t scoreBeforeSoft = game.score;
    check("soft drop moves", game.softDrop());
    check("soft drop scores one point", game.score == scoreBeforeSoft + 1);
    check("soft drop moves one row", game.y == beforeSoft + 1);

    // Hard drop: two points per row and the piece locks with a fresh piece.
    Game drop;
    drop.start(0, 4242);
    drop.piece = (uint8_t)Piece::O;
    drop.rotation = 0; drop.x = 4; drop.y = 0;
    const uint32_t scoreBeforeDrop = drop.score;
    const int distance = drop.hardDrop();
    check("hard drop returns the distance", distance > 0);
    check("hard drop scores two points per row", drop.score == scoreBeforeDrop + (uint32_t)distance * 2);
    bool placed = false;
    for (int row = 0; row < Rows; ++row)
        for (int col = 0; col < Columns; ++col)
            if (drop.board[row][col] == (uint8_t)Piece::O) placed = true;
    check("hard drop locks the piece into the board", placed);
    check("hard drop spawns the next piece", drop.piece >= 1 && drop.piece <= PieceTypes);
}

void pauseAndRestart() {
    Game game;
    game.start(500, 31);
    game.pause();
    check("pause stops play", game.state == State::Paused);
    const int row = game.y;
    game.tick(5000);
    check("paused game ignores gravity", game.y == row);
    game.resume(9000);
    check("resume returns to playing", game.state == State::Playing);
    game.tick(9000 + intervalMs(1) - 1);
    check("resume restarts the gravity timer", game.y == row);
    game.tick(9000 + intervalMs(1));
    check("resumed game drops again", game.y == row + 1);

    game.addScore(500);
    game.restart(9100);
    check("restart clears the board", game.completedRows() == 0);
    check("restart resets score and lines", game.score == 0 && game.lines == 0 && game.level == 1);
    check("restart keeps the best score", game.best >= 500);
    check("restart is playing", game.state == State::Playing);

    // Restarting from the game-over screen returns a clean run and keeps the best.
    Game over;
    over.start(0, 32);
    over.state = State::GameOver;
    over.score = 777;
    over.best = 900;
    over.restart(100);
    check("restart leaves game over", over.state == State::Playing && over.completedRows() == 0);
    check("restart clears score but keeps best after game over", over.score == 0 && over.best == 900);
}

void wallKicks() {
    // Vertical I hugging the right wall must rotate left with a kick.
    Game game;
    game.start(0, 7);
    game.piece = (uint8_t)Piece::I; game.rotation = 1; game.x = 7; game.y = 2;
    check("vertical I fits the right wall", game.fits(game.x, game.y, game.rotation));
    check("rotating by the wall succeeds", game.rotate());
    check("rotation applied", game.rotation == 2);
    check("rotation kicked one column left", game.x == 6);

    // Moving against a wall is a no-op, never an out-of-range write.
    Game left;
    left.start(0, 8);
    left.piece = (uint8_t)Piece::T; left.rotation = 0; left.x = 0; left.y = 4;
    check("left wall stops the piece", !left.moveLeft() && left.x == 0);
    check("rotating at the left wall succeeds", left.rotate());
    check("left wall rotation stays in bounds", left.x >= 0 && left.fits(left.x, left.y, left.rotation));

    // A horizontal I resting on the floor rotates upright with the two-row kick.
    Game floor;
    floor.start(0, 9);
    floor.piece = (uint8_t)Piece::I; floor.rotation = 0; floor.x = 3; floor.y = Rows - 2;
    check("horizontal I rests on the floor", floor.fits(floor.x, floor.y, floor.rotation));
    check("floor rotation succeeds", floor.rotate());
    check("floor rotation lifts the I two rows", floor.rotation == 1 && floor.y == Rows - 4);
}

// ---- line clears and scoring ---------------------------------------------
void lineClears() {
    // One line: bottom row full except the last column, vertical I drops in.
    Game one;
    one.start(0, 11);
    for (int row = Rows - 1; row >= Rows - 1; --row) fillRow(one, row, 0, Columns - 2, (uint8_t)Piece::Z);
    one.piece = (uint8_t)Piece::I; one.rotation = 1; one.x = Columns - 3; one.y = 0;
    const int distance = one.hardDrop();
    check("single line cleared", one.lines == 1 && one.completedRows() == 0);
    check("single line score", one.score == (uint32_t)distance * 2 + 100);
    // Only the three I cells above the cleared row fall down: column 9 keeps
    // three cells and row 16 is empty again.
    check("remaining cells shift down", one.board[Rows - 4][Columns - 1] == 0 &&
                                             one.board[Rows - 3][Columns - 1] != 0 &&
                                             one.board[Rows - 1][Columns - 1] != 0);

    // Four lines: rows 16..19 full except the last column.
    Game four;
    four.start(0, 12);
    for (int row = Rows - 4; row < Rows; ++row) fillRow(four, row, 0, Columns - 2, (uint8_t)Piece::L);
    four.piece = (uint8_t)Piece::I; four.rotation = 1; four.x = Columns - 3; four.y = 0;
    const int tetrisDistance = four.hardDrop();
    check("four lines cleared", four.lines == 4 && rowsClear(four, Rows - 4, Rows - 1));
    check("tetris score", four.score == (uint32_t)tetrisDistance * 2 + 800);
    check("level rises every ten lines", four.level == 1);

    // Ten lines in total: level 2 and no further rows cleared on a full board.
    Game level;
    level.start(0, 13);
    level.lines = 9;
    for (int row = Rows - 1; row >= Rows - 1; --row) fillRow(level, row, 0, Columns - 2, (uint8_t)Piece::S);
    level.piece = (uint8_t)Piece::I; level.rotation = 1; level.x = Columns - 3; level.y = 0;
    level.hardDrop();
    check("level 2 after ten lines", level.lines == 10 && level.level == 2);
    check("gravity gets faster with level", intervalMs(2) < intervalMs(1));

    // A row with a gap must never clear.
    Game gap;
    gap.start(0, 14);
    for (int row = Rows - 2; row < Rows; ++row) fillRow(gap, row, 0, Columns - 2, (uint8_t)Piece::J);
    gap.board[Rows - 2][Columns - 1] = (uint8_t)Piece::J;  // complete row 18, row 19 keeps the gap
    gap.piece = (uint8_t)Piece::O; gap.rotation = 0; gap.x = 4; gap.y = 0;
    gap.hardDrop();
    check("complete row clears while the gap row stays", gap.completedRows() == 0 && gap.lines == 1);

    // Line score uses the level of the clear: ten banked lines mean level 2 x 100.
    Game multiplier;
    multiplier.start(0, 15);
    multiplier.lines = 10; multiplier.level = 2;
    fillRow(multiplier, Rows - 1, 0, Columns - 2, (uint8_t)Piece::T);
    multiplier.piece = (uint8_t)Piece::I; multiplier.rotation = 1; multiplier.x = Columns - 3; multiplier.y = 0;
    const int multiDistance = multiplier.hardDrop();
    check("single line at level two scores 200", multiplier.score == (uint32_t)multiDistance * 2 + 200);
    check("level stays two after the eleventh line", multiplier.lines == 11 && multiplier.level == 2);
}

void gameOverAndGhost() {
    // Rows 0..1 are full except column 0, so locking elsewhere leaves them intact
    // and the next spawn collides -> game over.
    Game game;
    game.start(0, 55);
    for (int row = 0; row < 2; ++row) fillRow(game, row, 1, Columns - 1, (uint8_t)Piece::T);
    game.addScore(1200);
    game.piece = (uint8_t)Piece::O; game.rotation = 0; game.x = 0; game.y = 8;
    game.lock();
    check("game over on a blocked spawn", game.state == State::GameOver);
    check("game over keeps the best score", game.best >= 1200);
    check("game over stops movement", !game.moveLeft() && !game.rotate() && game.hardDrop() == 0);

    // Ghost row is the last row where the piece still fits.
    Game ghost;
    ghost.start(0, 56);
    ghost.piece = (uint8_t)Piece::T; ghost.rotation = 0; ghost.x = 3; ghost.y = 0;
    const int landing = ghost.ghostY();
    check("ghost lands on the floor", landing > ghost.y);
    check("ghost row itself fits", ghost.fits(ghost.x, landing, ghost.rotation));
    check("one row below the ghost does not fit", !ghost.fits(ghost.x, landing + 1, ghost.rotation));
}

void tickCatchUpIsBounded() {
    // A huge time jump must not tunnel the active piece through a stack: at most
    // four rows move per tick, then the timer re-bases.
    Game game;
    game.start(0, 61);
    game.piece = (uint8_t)Piece::O; game.rotation = 0; game.x = 4; game.y = 0;
    // Reserve row 19 for the O piece: columns 4 and 5 stay empty.
    for (int col = 0; col < Columns; ++col)
        if (col != 4 && col != 5) game.board[Rows - 1][col] = (uint8_t)Piece::J;
    game.tick(1000 * 1000);
    check("first jump is clamped to four rows", game.y == 4);
    check("the piece is still above the stack", game.state == State::Playing && game.board[Rows - 1][4] == 0);
    // Keep the fast-forward going until the piece lands: the guard may move at
    // most four rows per tick, so it must never jump past the stack row.
    bool bounded = true;
    int guard = 0;
    while (game.lines == 0 && guard++ < 60) {
        game.tick(1000000 + (uint32_t)guard * 1000);
        if (game.y > Rows - 2) bounded = false;
    }
    check("clamped drops never tunnel past the board floor", bounded);
    check("stacked drop locks and clears exactly one line", game.lines == 1 && game.state == State::Playing);
}

}  // namespace

int main() {
    shapes();
    sevenBag();
    gravityAndDrops();
    pauseAndRestart();
    wallKicks();
    lineClears();
    gameOverAndGhost();
    tickCatchUpIsBounded();
    if (failures) {
        std::printf("TETRIS ENGINE: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    std::printf("PASS: tetris engine (%d checks)\n", checks);
    return 0;
}
