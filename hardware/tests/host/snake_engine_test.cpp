// Host regression of the production Snake rules; no Arduino/LVGL is linked.
#include "src/pages/snake_engine.h"
#include <cstdio>
#include <cstring>

using namespace SnakeEngine;
namespace {
int checks = 0, failures = 0;
constexpr int CenterX = Columns / 2, CenterY = Rows / 2;
constexpr uint32_t MaximumScore = (Capacity - 4) * 10;
void check(const char *what, bool condition) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", what); }
}
Game fresh() { Game game; game.start(1000, 12345); return game; }
void initialAndTiming() {
    Game game;
    game.best = 150; game.speed = 2;
    game.prepare(0);
    check("prepare is ready", game.state == State::Ready);
    check("prepare preserves best and speed", game.best == 150 && game.speed == 2);
    check("initial body is centered and contiguous", game.length == 4 && game.body[0] == Cell{CenterX, CenterY} && game.body[3] == Cell{CenterX - 3, CenterY});
    check("ready game does not move", !game.tick(9000) && !game.step() && !game.turn(Direction::Up));
    game = fresh();
    const Cell head = game.body[0];
    check("tick waits for full interval", !game.tick(1179) && game.body[0] == head);
    check("tick takes one cell", game.tick(1180) && game.body[0] == Cell{CenterX + 1, CenterY});
    check("same timestamp cannot move again", !game.tick(1180));
    game = fresh();
    check("long stall only takes one step", game.tick(1000000) && game.body[0] == Cell{CenterX + 1, CenterY} && game.state == State::Playing);
    check("long stall resets schedule", !game.tick(1000001));
    game = fresh(); game.lastStepAt = 0xFFFFFFA0u;
    check("unsigned millisecond rollover waits", !game.tick(0x50u));
    check("unsigned millisecond rollover moves", game.tick(0x54u));
    game = fresh(); game.turn(Direction::Up); game.pause();
    check("paused does not move", !game.tick(90000) && !game.step() && game.state == State::Paused);
    check("paused rejects turns", !game.turn(Direction::Down));
    game.resume(90000);
    check("resume clears pending input and clock", !game.tick(90001) && game.queuedCount == 0);
    game.tick(90180);
    check("resume continues original direction", game.body[0] == Cell{CenterX + 1, CenterY});
}
void turns() {
    Game game = fresh();
    check("immediate reverse rejected", !game.turn(Direction::Left));
    check("same direction rejected", !game.turn(Direction::Right));
    check("first buffered corner accepted", game.turn(Direction::Up));
    check("reverse of buffered direction rejected", !game.turn(Direction::Down));
    check("second buffered corner accepted", game.turn(Direction::Left));
    check("third buffered corner rejected", !game.turn(Direction::Down));
    game.step();
    check("first tick consumes first corner only", game.direction == Direction::Up && game.body[0] == Cell{CenterX, CenterY - 1});
    game.step();
    check("second tick consumes second corner", game.direction == Direction::Left && game.body[0] == Cell{CenterX - 1, CenterY - 1});
    check("queued turns cannot bypass reverse prevention", !game.turn(Direction::Right));
    check("invalid direction rejected", !game.turn((Direction)4));
}
void foodAndGrowth() {
    Game game = fresh();
    const Cell tail = game.body[3];
    game.food = {CenterX + 1, CenterY};
    check("eating step executes", game.step());
    check("food grows body and awards points", game.length == 5 && game.score == 10 && game.best == 10);
    check("growth retains old tail", game.body[4] == tail);
    check("new food stays off snake", !game.occupied(game.food));
    game.step();
    check("empty cell does not grow", game.length == 5 && game.score == 10);
    game.restart(6000);
    check("restart resets run and preserves best", game.state == State::Playing && game.score == 0 && game.best == 10 && game.length == 4);
    Game a = fresh(), b = fresh();
    bool seenRows[Rows] = {};
    for (int i = 0; i < 500; ++i) {
        a.spawnFood(); b.spawnFood();
        check("food is deterministic in free cells", a.food == b.food && a.food.x >= 0 && a.food.x < Columns && a.food.y >= 0 && a.food.y < Rows && !a.occupied(a.food));
        if (a.food.y >= 0 && a.food.y < Rows) seenRows[a.food.y] = true;
    }
    for (int row = 0; row < Rows; ++row) check("food generator reaches every board row", seenRows[row]);
    game = fresh(); game.length = Capacity - 1;
    for (int i = 0; i < Capacity - 1; ++i) game.body[i] = {(int8_t)(i % Columns), (int8_t)(i / Columns)};
    check("one free cell is always selected", game.spawnFood() && game.food == Cell{Columns - 1, Rows - 1});
    game.body[Capacity - 1] = {Columns - 1, Rows - 1}; game.length = Capacity;
    check("full board ends without placement loop", !game.spawnFood() && game.state == State::Won && game.food == Cell{-1, -1});
    check("won game stays stopped", !game.tick(9000) && !game.turn(Direction::Up));
}
void collisionsAndWin() {
    for (int d = 0; d < 4; ++d) {
        Game game = fresh(); game.direction = (Direction)d;
        game.body[0] = d == 0 ? Cell{5, 0} : d == 1 ? Cell{Columns - 1, 4} : d == 2 ? Cell{5, Rows - 1} : Cell{0, 4};
        const Cell head = game.body[0];
        check("all walls stop the game", game.step() && game.state == State::GameOver && game.body[0] == head);
        check("game over cannot keep moving", !game.step());
    }
    Game game = fresh();
    // Fold around a 2x2 square: the next left cell is the departing tail.
    game.length = 4; game.direction = Direction::Left;
    game.body[0] = {5, 5}; game.body[1] = {5, 4}; game.body[2] = {4, 4}; game.body[3] = {4, 5}; game.food = {0, 0};
    check("moving into departing tail is legal", game.step() && game.state == State::Playing && game.body[0] == Cell{4, 5});
    game = fresh(); game.length = 5; game.direction = Direction::Left;
    game.body[0] = {5, 5}; game.body[1] = {5, 4}; game.body[2] = {4, 4}; game.body[3] = {4, 5}; game.body[4] = {3, 5}; game.food = {0, 0};
    check("moving into body is fatal", game.step() && game.state == State::GameOver);
    game = fresh(); game.length = 4; game.direction = Direction::Left;
    game.body[0] = {5, 5}; game.body[1] = {5, 4}; game.body[2] = {4, 4}; game.body[3] = {4, 5}; game.food = {4, 5};
    check("tail remains occupied when growing", game.step() && game.state == State::GameOver);
    game = fresh(); game.length = Capacity - 1;
    // A serpentine path fills every cell except (0,0); head at (1,0)
    // moves left to consume the final food. This is a valid continuous body.
    int count = 0;
    for (int y = 0; y < Rows; ++y)
        for (int i = 0; i < Columns; ++i) {
            const int x = y % 2 ? Columns - 1 - i : i;
            if (x || y) game.body[count++] = {(int8_t)x, (int8_t)y};
        }
    game.direction = Direction::Left; game.food = {0, 0}; game.score = MaximumScore - 10;
    check("last food produces a complete-board win", game.step() && game.state == State::Won && game.length == Capacity && game.score == MaximumScore && game.best == MaximumScore && game.food == Cell{-1, -1});
}
void expandedBoardRows() {
    Game game = fresh();
    game.direction = Direction::Down; game.food = {0, 0};
    for (int i = 0; i < 4; ++i) game.body[i] = {(int8_t)(5 - i), 9};
    for (int row = 10; row < Rows; ++row)
        check("new lower rows accept normal movement", game.step() && game.state == State::Playing && game.body[0] == Cell{5, (int8_t)row});
    check("row below expanded board is fatal", game.step() && game.state == State::GameOver && game.body[0].y == Rows - 1);
    for (int row = 10; row < Rows; ++row) {
        game = fresh();
        for (int i = 0; i < 4; ++i) game.body[i] = {(int8_t)(5 - i), (int8_t)row};
        game.food = {6, (int8_t)row};
        check("new lower rows support eating and growth", game.step() && game.state == State::Playing && game.score == 10 && game.length == 5 && game.body[0] == Cell{6, (int8_t)row});
    }
}
void speeds() {
    Game game = fresh();
    for (uint8_t speed = 0; speed < 3; ++speed) {
        game.speed = speed; game.score = 0;
        check("selected speed has expected initial interval", game.intervalMs() == 240 - speed * 60);
        game.score = 40;
        check("speed increases every fifth food", game.intervalMs() == 240 - speed * 60);
        game.score = 50;
        check("fifth food reduces interval", game.intervalMs() == 230 - speed * 60);
        game.score = MaximumScore;
        check("acceleration respects 90ms floor", game.intervalMs() == 90);
    }
}
}
int main() {
    initialAndTiming(); turns(); foodAndGrowth(); collisionsAndWin(); expandedBoardRows(); speeds();
    std::printf("Snake engine: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
