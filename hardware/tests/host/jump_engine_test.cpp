// Regression of the production press/hold jump rules, without Arduino or LVGL.
#include "src/pages/jump_engine.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace JumpEngine;
namespace {
int checks = 0, failures = 0;
void check(const char *what, bool condition) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", what); }
}
bool near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
Game fresh(uint32_t now = 1000) { Game game; game.start(now, 20261112u); return game; }
uint16_t holdForDistance(unsigned distance) {
    return (uint16_t)((distance * ChargeLimitMs + MaximumDistance - 1) / MaximumDistance);
}
uint32_t launch(Game &game, uint32_t pressAt, unsigned distance) {
    check("playing accepts charge", game.press(pressAt));
    const uint32_t releasedAt = pressAt + holdForDistance(distance);
    check("charging accepts release", game.release(releasedAt));
    check("release uses requested distance", game.jumpDistance == distance);
    return releasedAt;
}
void initialAndCharge() {
    Game game;
    game.best = 123; game.prepare(0);
    check("prepare preserves highest score", game.state == State::Ready && game.score == 0 && game.best == 123);
    check("ready refuses gameplay inputs", !game.press(100) && !game.release(200) && !game.cancelCharge() && !game.pause(300));
    check("ready does not run clock", !game.tick(9000));
    game = fresh();
    check("start begins on platform", game.state == State::Playing && game.score == 0 && game.currentRadius == 22);
    check("playing does not release or cancel", !game.release(1000) && !game.cancelCharge());
    check("press begins charging", game.press(1100) && game.state == State::Charging && game.chargeStartedAt == 1100);
    check("duplicate press cannot reset charge", !game.press(1200) && game.chargeStartedAt == 1100);
    check("charge is measured without a tick", game.chargeElapsed(1600) == 500 && near(game.chargeProgress(1600), .5f));
    check("tick publishes charge", game.tick(1600) && game.chargeMs == 500);
    check("same charge timestamp does not redraw", !game.tick(1600));
    check("long holds clamp", game.chargeElapsed(9000) == 1000 && near(game.chargeProgress(9000), 1));
    check("charge can be canceled without launching", game.cancelCharge() && game.state == State::Playing && game.chargeMs == 0 && game.jumpDistance == 0);
    check("release after lost press cannot launch", !game.release(9200));
    game.press(10000); game.release(20000);
    check("release clamps maximum travel", game.state == State::Jumping && game.chargeMs == 1000 && game.jumpDistance == 210);
    check("flying ignores pointer input", !game.press(20001) && !game.release(20002) && !game.cancelCharge());
    game = fresh(); game.press(1100); game.release(1100);
    check("short tap can safely return", game.jumpDistance == 0);
    check("tap returns to current platform", game.tick(1520) && game.state == State::Settling && game.landedCurrent && !game.landedTarget && game.score == 0 && game.jumps == 0);
}
void timingAndArc() {
    Game game = fresh();
    const uint32_t releasedAt = launch(game, 2000, 100);
    check("arc begins at zero", near(game.flightProgress(releasedAt), 0) && near(game.height(releasedAt), 0));
    check("arc reaches midpoint and peak", near(game.flightProgress(releasedAt + 210), .5f) && near(game.height(releasedAt + 210), 42));
    check("arc is symmetric", near(game.height(releasedAt + 105), game.height(releasedAt + 315)));
    check("flight lasts full duration", !game.tick(releasedAt + 419) && game.state == State::Jumping);
    check("arc ends at zero and clamps", near(game.flightProgress(releasedAt + 9000), 1) && near(game.height(releasedAt + 9000), 0));
    game.targetDistance = 100; game.targetRadius = 20;
    check("flight resolves at boundary", game.tick(releasedAt + 420) && game.state == State::Settling && game.phaseAt == releasedAt + 420);
    check("settle lasts full duration", !game.tick(releasedAt + 639) && game.state == State::Settling);
    check("settle completes at boundary", game.tick(releasedAt + 640) && game.state == State::Playing);
    game = fresh(); game.targetDistance = 100;
    const uint32_t secondRelease = launch(game, 2000, 100);
    check("late tick retains one observable transition", game.tick(secondRelease + 5000) && game.state == State::Settling && game.phaseAt == secondRelease + 420);
    check("second late tick completes settle", game.tick(secondRelease + 5000) && game.state == State::Playing && game.phaseAt == secondRelease + 640);
    check("late tick never makes another jump", !game.tick(secondRelease + 5000));
}
void contactsAndScoring() {
    const int offsets[] = {-21, -20, -6, -5, 0, 5, 6, 20, 21};
    for (int offset : offsets) {
        Game game = fresh(); game.targetDistance = 110; game.targetRadius = 20;
        const uint32_t at = launch(game, 2000, 110 + offset);
        game.tick(at + 420);
        const bool hit = offset >= -20 && offset <= 20;
        const bool center = offset >= -5 && offset <= 5;
        check("platform boundary contact is inclusive", (game.state == State::Settling) == hit);
        check("contact sets landing type", game.landedTarget == hit && !game.landedCurrent);
        check("center awards exactly two, normal one, miss zero", game.score == (center ? 2u : hit ? 1u : 0u) && game.lastGain == game.score);
        check("combo and successful jump count", game.combo == (center ? 1u : 0u) && game.jumps == (hit ? 1u : 0u));
        check("new record tracks score", game.best == game.score);
        if (!hit) {
            check("miss displays falling before end", !game.tick(at + 719) && game.state == State::Falling);
            check("fall ends after 300ms", game.tick(at + 720) && game.state == State::GameOver);
            check("game over rejects all gameplay", !game.tick(at + 9000) && !game.press(at + 9001) && !game.pause(at + 9002));
        }
    }
    for (unsigned distance : {21u, 22u, 23u, 50u, 210u}) {
        Game game = fresh(); game.targetDistance = 110; game.targetRadius = 20;
        const uint16_t oldTarget = game.targetDistance, oldRadius = game.targetRadius;
        const int8_t oldDirection = game.direction;
        const uint32_t at = launch(game, 3000, distance);
        game.tick(at + 420);
        check("current platform edge is inclusive", game.landedCurrent == (distance <= 22));
        if (game.landedCurrent) {
            game.tick(at + 640);
            check("harmless tap keeps same target", game.state == State::Playing && game.targetDistance == oldTarget && game.targetRadius == oldRadius && game.direction == oldDirection && game.score == 0);
        } else check("gap and overshoot fail", game.state == State::Falling);
    }
    Game game = fresh(); uint32_t now = 5000;
    for (unsigned run = 1; run <= 4; ++run) {
        game.targetDistance = 100;
        const uint16_t oldRadius = game.targetRadius;
        const int8_t oldDirection = game.direction;
        const uint32_t at = launch(game, now, 100);
        game.tick(at + 420);
        const int8_t planned = game.nextDirection;
        check("perfect combo counts consecutive centers", game.combo == run && game.score == run * 2 && game.jumps == run);
        check("camera retains old direction during settle", game.direction == oldDirection && (planned == -1 || planned == 1));
        game.tick(at + 640);
        check("settle moves to planned next platform", game.direction == planned && game.currentRadius == oldRadius);
        now = at + 1000;
    }
    game.targetDistance = 100;
    uint32_t at = launch(game, now, 108); game.tick(at + 420);
    check("off-center success resets combo", game.combo == 0 && game.score == 9 && game.lastGain == 1 && !game.perfect);
    game.tick(at + 640);
    const uint32_t record = game.best;
    game.restart(at + 900);
    check("restart resets run but keeps record", game.state == State::Playing && game.score == 0 && game.combo == 0 && game.jumps == 0 && game.best == record);
}
void pauseAndRollover() {
    Game game = fresh(); game.press(2000); game.tick(2400);
    check("pause charging cancels held jump", game.pause(2500) && game.state == State::Paused && game.pausedState == State::Playing && game.chargeMs == 0);
    check("paused does not accept release or clock", !game.release(9000) && !game.tick(10000));
    check("resume requires fresh press", game.resume(20000) && game.state == State::Playing && !game.release(20001));
    check("resume only applies to paused state", !game.resume(21000));
    game.targetDistance = 100;
    uint32_t at = launch(game, 22000, 100);
    const float beforeHeight = game.height(at + 140);
    check("pause freezes flight phase", game.pause(at + 140) && game.elapsed(at + 9000) == 140 && near(game.height(at + 9000), beforeHeight));
    check("paused flight does not advance", !game.tick(at + 10000) && game.state == State::Paused);
    const uint32_t resumedAt = at + 20000;
    game.resume(resumedAt);
    check("resume preserves remaining flight", !game.tick(resumedAt + 279) && game.state == State::Jumping);
    check("resumed flight lands after remaining time", game.tick(resumedAt + 280) && game.state == State::Settling);
    game.pause(resumedAt + 330);
    check("settle pause saves elapsed", game.elapsed(resumedAt + 9000) == 50 && game.pausedState == State::Settling);
    game.resume(resumedAt + 10000);
    check("settle pause preserves remaining duration", !game.tick(resumedAt + 10169) && game.tick(resumedAt + 10170) && game.state == State::Playing);
    at = launch(game, resumedAt + 11000, 210); game.tick(at + 420);
    game.pause(at + 520); game.resume(at + 20000);
    check("fall pause preserves remaining duration", !game.tick(at + 20199) && game.tick(at + 20200) && game.state == State::GameOver);
    game = fresh(0xFFFFFF00u); game.press(0xFFFFFF80u);
    check("charge elapsed crosses unsigned rollover", game.chargeElapsed(0x74u) == 244);
    game.release(0x180u);
    check("release computes rollover hold", game.chargeMs == 512 && game.jumpDistance == 107);
    game.targetDistance = 107;
    check("flight duration after rollover", !game.tick(0x323u) && game.tick(0x324u) && game.state == State::Settling);
    game = fresh(); at = launch(game, 3000, 100); game.targetDistance = 100;
    game.pause(at + 100); game.resume(10);
    check("resume anchor may wrap safely", game.elapsed(10) == 100 && !game.tick(329) && game.tick(330));
}
void targets() {
    Game a = fresh(), b = fresh(); bool sawLeft = false, sawRight = false;
    for (unsigned i = 0; i < 200; ++i) {
        check("same seed produces same target stream", a.targetDistance == b.targetDistance && a.targetRadius == b.targetRadius && a.direction == b.direction);
        check("generated platform range is playable", a.targetDistance >= 85 && a.targetDistance <= 145 && a.targetRadius >= 18 && a.targetRadius <= 27 && (a.direction == -1 || a.direction == 1));
        sawLeft = sawLeft || a.direction == -1; sawRight = sawRight || a.direction == 1;
        const uint32_t pressedAt = i * 2000u + 1000;
        for (Game *game : {&a, &b}) {
            game->press(pressedAt); game->release(pressedAt + holdForDistance(game->targetDistance));
            const uint32_t releasedAt = game->phaseAt;
            game->tick(releasedAt + 420); game->tick(releasedAt + 640);
        }
    }
    check("target stream uses both diagonal directions", sawLeft && sawRight);
    check("long session records every successful target", a.jumps == 200 && a.score == 400 && a.best == 400 && a.combo == 200);
}
}
int main() {
    initialAndCharge(); timingAndArc(); contactsAndScoring(); pauseAndRollover(); targets();
    std::printf("Jump engine: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
