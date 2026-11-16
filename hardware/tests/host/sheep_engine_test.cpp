// Regression of the production layered triple-match rules, without Arduino/LVGL.
#include "src/pages/sheep_engine.h"
#include <cstdio>
#include <cstring>

using namespace SheepEngine;
namespace {
int checks = 0, failures = 0;
void check(const char *what, bool condition) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", what); }
}
Game fresh(uint32_t seed = 20261113u) { Game game; game.start(seed); return game; }
Game flat(unsigned count) {
    Game game = fresh();
    game.tileCount = game.remaining = (uint8_t)count;
    for (unsigned i = 0; i < count; ++i) game.tiles[i] = {(int16_t)(i * 36), 20, 0, (uint8_t)(i % TypeCount), false};
    return game;
}
bool noTriples(const Game &game) {
    unsigned counts[TypeCount] = {};
    for (int i = 0; i < game.trayCount; ++i) if (++counts[game.tray[i]] >= 3) return false;
    return true;
}
void readinessAndBoundaries() {
    Game game;
    game.best = 170; game.prepare(0);
    check("prepare stays ready and keeps best", game.state == State::Ready && game.best == 170 && game.score == 0 && game.level == 1);
    check("no undo exists before first selection", !game.undoAvailable && !game.canUndo() && !game.undo());
    check("ready rejects selection, pause, resume, shuffle and next level", !game.select(game.proof[0]) && !game.pause() && !game.resume() && !game.shuffle() && !game.nextLevel());
    game = fresh();
    check("start opens introductory deal", game.state == State::Playing && game.tileCount == 18 && game.remaining == 18 && !game.trayCount);
    check("invalid IDs are rejected", !game.select(-1) && !game.select(game.tileCount) && !game.canSelect(200));
    const int id = game.proof[0];
    check("paused rejects selection and tools", game.pause() && game.state == State::Paused && !game.select(id) && !game.shuffle() && !game.undo() && !game.pause());
    check("resume re-enables same board", game.resume() && !game.resume() && game.canSelect(id));
    check("valid tile can only be selected once", game.select(id) && !game.select(id) && game.tiles[id].removed && game.remaining == 17);
    check("selection creates usable undo", game.undoAvailable && game.canUndo());

    game = flat(2); game.tiles[0] = {20, 30, 0, 1, false}; game.tiles[1] = {52, 30, 1, 2, false};
    check("touching horizontal tile edges do not cover", game.canSelect(0) && game.canSelect(1));
    game.tiles[1].x = 51;
    check("one-pixel higher overlap blocks", !game.canSelect(0) && game.canSelect(1));
    check("visible lower corner remains blocked", game.hitTest(20, 30) == 0 && !game.canSelect(game.hitTest(20, 30)));
    check("topmost rectangle wins overlap touch", game.hitTest(51, 30) == 1);
    check("hit includes top-left", game.hitTest(51, 30) == 1);
    check("hit excludes right edge", game.hitTest(83, 30) == -1);
    check("hit excludes lower edge", game.hitTest(51, 62) == -1);
    check("off-board touch misses", game.hitTest(-1, -1) == -1);
    game.tiles[1].x = 20; game.tiles[1].y = 62;
    check("touching vertical tile edges do not cover", game.canSelect(0));
    game.tiles[1].y = 61;
    check("one-pixel vertical overlap blocks", !game.canSelect(0));
    game.tiles[1].y = 30; game.tiles[1].layer = 0;
    check("equal layer overlap does not block", game.canSelect(0) && game.canSelect(1));
    check("equal layer drawing order resolves touch", game.hitTest(20, 30) == 1);
    game.tiles[1].removed = true;
    check("removed top tile no longer intercepts", game.hitTest(20, 30) == 0 && game.canSelect(0));
}
void trayAndUndo() {
    Game game = flat(8);
    const uint8_t order[] = {0, 1, 0, 2, 1, 0, 4, 5};
    for (int i = 0; i < 8; ++i) game.tiles[i].type = order[i];
    for (int i = 0; i < 5; ++i) game.select(i);
    const uint8_t expected[] = {0, 0, 1, 1, 2};
    check("tray groups equal types after each pick", game.trayCount == 5 && !std::memcmp(game.tray, expected, 5));
    const uint8_t oldRemaining = game.remaining;
    check("third equal type removes immediately", game.select(5) && game.trayCount == 3 && game.score == 10 && game.best == 10);
    check("triple preserves other group order", game.tray[0] == 1 && game.tray[1] == 1 && game.tray[2] == 2);
    check("undo restores removed triple and selected board tile", game.undo() && game.trayCount == 5 && !std::memcmp(game.tray, expected, 5) && !game.tiles[5].removed && game.remaining == oldRemaining && game.score == 0);
    check("undo never lowers record and is once per level", game.best == 10 && !game.undoAvailable && !game.undo());
    game.select(5);
    check("new selections cannot refill consumed undo", !game.undoAvailable && !game.undo());

    game = flat(9);
    const uint8_t seven[] = {0, 1, 2, 3, 4, 0, 0};
    for (int i = 0; i < 7; ++i) game.tiles[i].type = seven[i];
    for (int i = 0; i < 6; ++i) game.select(i);
    check("six unmatched tiles are still playing", game.trayCount == 6 && game.state == State::Playing);
    check("seventh slot triple resolves before loss", game.select(6) && game.state == State::Playing && game.trayCount == 4 && game.score == 10);

    game = flat(9);
    for (int i = 0; i < 7; ++i) game.select(i);
    check("seventh unmatched tile loses", game.state == State::GameOver && game.trayCount == 7 && game.remaining == 2);
    check("game over rejects normal selection and shuffle", !game.select(7) && !game.shuffle() && !game.pause());
    check("undo can rescue game over", game.undo() && game.state == State::Playing && game.trayCount == 6 && !game.tiles[6].removed && game.remaining == 3);

    game = flat(3);
    for (int i = 0; i < 3; ++i) { game.tiles[i].type = 5; game.select(i); }
    check("empty intro board opens level-clear", game.state == State::LevelCleared && game.remaining == 0 && game.trayCount == 0 && game.score == 10);
    check("undo may restore final cleared triple", game.undo() && game.state == State::Playing && game.remaining == 1 && game.trayCount == 2 && game.score == 0 && game.best == 10);
    game.select(2); game.nextLevel();
    check("next level resets per-level tools", game.level == 2 && game.state == State::Playing && game.tileCount == 54 && !game.undoAvailable && game.shuffleAvailable);
    check("new level grants undo again", game.select(game.proof[0]) && game.undoAvailable && game.undo());
}
void shuffleAndDeterminism() {
    Game a = fresh(99), b = fresh(99);
    check("same seed yields same intro deal", !std::memcmp(a.tiles, b.tiles, sizeof(a.tiles)) && !std::memcmp(a.proof, b.proof, sizeof(a.proof)));
    for (int i = 0; i < 4; ++i) { a.select(a.proof[i]); b.select(b.proof[i]); }
    const Game before = a;
    unsigned typesBefore[TypeCount] = {}, typesAfter[TypeCount] = {};
    for (int i = 0; i < a.tileCount; ++i) if (!a.tiles[i].removed) ++typesBefore[a.tiles[i].type];
    check("first shuffle is available", a.shuffle() && b.shuffle());
    check("shuffle is deterministic for same state", !std::memcmp(a.tiles, b.tiles, sizeof(a.tiles)));
    check("shuffle preserves tray, score and remaining", a.trayCount == before.trayCount && !std::memcmp(a.tray, before.tray, sizeof(a.tray)) && a.score == before.score && a.remaining == before.remaining);
    for (int i = 0; i < a.tileCount; ++i) {
        check("shuffle never moves or resurrects tiles", a.tiles[i].x == before.tiles[i].x && a.tiles[i].y == before.tiles[i].y && a.tiles[i].layer == before.tiles[i].layer && a.tiles[i].removed == before.tiles[i].removed);
        if (!a.tiles[i].removed) ++typesAfter[a.tiles[i].type];
        else check("shuffle leaves removed tile types untouched", a.tiles[i].type == before.tiles[i].type);
    }
    check("shuffle preserves remaining type multiset", !std::memcmp(typesBefore, typesAfter, sizeof(typesBefore)));
    check("shuffle is once per level and invalidates solution claim", !a.shuffleAvailable && !a.shuffle() && !a.proofValid);
    check("shuffle discards incompatible undo snapshot", !a.undoAvailable && !a.undo());
    int id = -1;
    for (int i = 0; i < a.tileCount; ++i) if (a.canSelect(i)) { id = i; break; }
    check("future selection can still use unused undo", id >= 0 && a.select(id) && a.undoAvailable && a.undo());
    a.restart();
    check("restart resets intro, tray and tools", a.level == 1 && a.state == State::Playing && a.tileCount == 18 && a.trayCount == 0 && a.score == 0 && a.shuffleAvailable && !a.undoAvailable && a.proofValid);
}
void certifiedDeals() {
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        Game game = fresh(seed);
        for (int stage = 1; stage <= 2; ++stage) {
            const unsigned expected = stage == 1 ? 18 : 54;
            check("deal has expected stage tile count", game.tileCount == expected && game.remaining == expected && game.level == stage && game.proofValid);
            bool seen[MaxTiles] = {};
            unsigned counts[TypeCount] = {};
            for (int id = 0; id < game.tileCount; ++id) {
                const Tile &tile = game.tiles[id];
                check("deal rectangles fit board and use valid types", tile.x >= 12 && tile.x + TileSize <= 304 && tile.y >= 4 && tile.y + TileSize <= 138 && tile.type < TypeCount);
                ++counts[tile.type];
            }
            bool divisible = true;
            for (int type = 0; type < TypeCount; ++type) divisible = divisible && counts[type] % 3 == 0;
            check("deal types consist of complete triples", divisible);
            for (int step = 0; step < game.tileCount; ++step) {
                const int id = game.proof[step];
                check("certificate is a permutation of tile IDs", id < game.tileCount && !seen[id]);
                if (id >= game.tileCount) break;
                seen[id] = true;
                check("certificate always selects an exposed tile", game.canSelect(id));
                check("certificate selection succeeds", game.select(id));
                check("certificate remains under loss capacity", game.trayCount < TrayCapacity && noTriples(game));
                check("each certificate pick removes one tile", game.remaining == game.tileCount - step - 1);
            }
            check("certificate clears tray and every tile", !game.remaining && !game.trayCount);
            check("stage scores count each triple", game.score == (stage == 1 ? 60u : 240u) && game.best == game.score);
            if (stage == 1) check("intro certificate advances into challenge", game.state == State::LevelCleared && game.nextLevel() && !game.nextLevel());
            else check("challenge certificate wins both stages", game.state == State::Won && !game.nextLevel());
        }
        const uint32_t best = game.best;
        check("winning restart preserves highest score", (game.restart(), game.state == State::Playing && game.score == 0 && game.best == best && game.level == 1));
    }
}
}
int main() {
    readinessAndBoundaries(); trayAndUndo(); shuffleAndDeterminism(); certifiedDeals();
    std::printf("Sheep engine: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
