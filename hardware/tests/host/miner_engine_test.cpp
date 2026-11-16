#include "src/pages/miner_engine.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace MinerEngine;
namespace {
int checks = 0, failures = 0;
void check(const char *what, bool condition) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", what); }
}
bool near(float a, float b) { return std::fabs(a-b) < 0.01f; }
Game fresh(uint32_t now = 1000, uint32_t seed = 20261113u) { Game g; g.start(now,seed); return g; }
void fixedRay(Game &g, uint32_t now, float angle = 0) {
    g.state=State::Playing;g.lastTickAt=now;g.angle=angle;g.ropeLength=RestLength;g.updateHook();
    check("cast starts from playing",g.cast(now));
}
void readinessAndClock() {
    Game g;g.best=500;g.prepare(0);
    check("prepare keeps record and ready",g.state==State::Ready&&g.best==500&&g.score==0&&g.level==1&&g.secondsLeft()==60);
    check("ready does not advance or cast",!g.tick(8000)&&!g.cast(8000)&&!g.pause(8000)&&!g.resume(8000)&&!g.nextLevel(8000));
    g=fresh();
    check("new stage has ten remaining ores",g.oreCount==MaxOres&&g.remaining==MaxOres&&g.target==300);
    check("same clock has no movement",!g.tick(1000));
    g.tick(1001);check("seconds round up",g.secondsLeft()==60);
    g.tick(2000);check("clock decreases at full second",g.secondsLeft()==59);
    check("pause playing succeeds",g.pause(2100)&&g.state==State::Paused);
    const auto elapsed=g.elapsedMs;const auto angle=g.angle;
    check("paused blocks clock and actions",!g.tick(90000)&&!g.cast(90000)&&!g.pause(90000)&&g.elapsedMs==elapsed&&near(g.angle,angle));
    check("resume resets elapsed anchor",g.resume(100000)&&g.tick(100100)&&g.elapsedMs==elapsed+100);
    check("resume only works when paused",!g.resume(100200));
    g=fresh();g.tick(61000);
    check("time expires at sixty seconds",g.state==State::GameOver&&g.secondsLeft()==0&&g.score==0);
    check("timeout ignores more gameplay",!g.tick(90000)&&!g.cast(90000)&&!g.nextLevel(90000));
    g=fresh(0xFFFFFF00u);g.tick(0x100u);
    check("clock wraps without losing time",g.elapsedMs==512&&g.secondsLeft()==60);
    g.pause(0x200u);g.resume(0xFFFFFFF0u);g.tick(0x54u);
    check("resume clock also wraps safely",g.elapsedMs==868);
}
void castsAndTunneling() {
    Game g=fresh();fixedRay(g,1000);
    check("duplicate cast is refused",!g.cast(1000));
    g.tick(1200);
    check("extending rope cannot award points",g.state==State::Casting&&g.score==0&&g.caught==-1&&g.hookY>48);
    g.tick(1500);
    check("center diamond is caught by swept collision",g.state==State::Reeling&&g.caught==9&&g.score==0&&!g.ores[9].removed);
    const float heldLength=g.ropeLength;const auto elapsed=g.elapsedMs;
    check("pause freezes reel",g.pause(1500)&&g.state==State::Paused&&g.pausedState==State::Reeling);
    g.tick(99000);check("paused reel stays at same coordinates",near(g.ropeLength,heldLength)&&g.elapsedMs==elapsed);
    g.resume(100000);g.tick(100050);
    check("reel continues after pause",g.state==State::Reeling&&g.ropeLength<heldLength&&g.score==0);
    g.tick(101000);
    check("score is credited only after full reel",g.state==State::Playing&&g.score==250&&g.best==250&&g.remaining==9&&g.ores[9].removed&&g.caught==-1&&near(g.ropeLength,RestLength));

    g=fresh();fixedRay(g,1000);g.tick(4000);
    check("one delayed tick cannot tunnel through ore",g.score==250&&g.ores[9].removed&&g.remaining==9);
    g=fresh();g.oreCount=g.remaining=2;
    g.ores[0]={160,90,5,Kind::Gold,100,2,false};g.ores[1]={160,150,14,Kind::Diamond,250,1,false};
    fixedRay(g,1000);g.tick(2500);
    check("nearest circle wins swept contact",g.ores[0].removed&&!g.ores[1].removed&&g.score==100);
    g=fresh();g.oreCount=g.remaining=1;
    g.ores[0]={171,100,8,Kind::Gold,100,2,false};
    fixedRay(g,1000);g.tick(1700);
    check("hook radius includes tangent contact",g.caught==0||g.ores[0].removed);
    g=fresh();g.oreCount=g.remaining=1;
    g.ores[0]={172,100,8,Kind::Gold,100,2,false};
    fixedRay(g,1000);g.tick(2200);
    check("ray outside expanded radius misses",g.caught==-1&&g.score==0);
    g.tick(5000);
    check("empty reel returns without score",g.state==State::Playing&&g.score==0&&g.remaining==1);
    g=fresh();g.oreCount=g.remaining=1;g.ores[0]={20,100,8,Kind::Gold,100,2,true};
    fixedRay(g,1000,1.1f);g.tick(5000);
    check("hook respects side wall then returns",g.state==State::Playing&&g.hookX>=6&&g.hookX<=314&&g.score==0);
}
void weightAndDeadline() {
    Game small=fresh(),heavy=fresh();
    for(Game *g : {&small,&heavy}) {
        g->oreCount=g->remaining=1;g->ores[0]={160,130,10,Kind::Gold,100,1,false};
        g->state=State::Reeling;g->caught=0;g->ropeLength=100;g->updateHook();
    }
    heavy.ores[0].weight=4;
    small.tick(1200);heavy.tick(1200);
    check("heavy ore retracts more slowly",heavy.ropeLength>small.ropeLength&&near(small.reelSpeed(),185)&&near(heavy.reelSpeed(),46.25f));
    Game g=fresh();g.state=State::Reeling;g.caught=9;g.ropeLength=100;g.updateHook();g.elapsedMs=59990;
    g.tick(1100);
    check("deadline truncates travel and does not grant pending ore",g.state==State::GameOver&&g.secondsLeft()==0&&g.score==0&&!g.ores[9].removed);
    g=fresh();g.state=State::Reeling;g.caught=9;g.ropeLength=RestLength+1;g.updateHook();g.elapsedMs=59900;g.score=100;
    g.tick(1200);
    check("goal retrieved before deadline clears immediately",g.state==State::LevelCleared&&g.score==350&&g.best==350);
}
void layoutsAndThreeLevels() {
    bool varied=false;
    for(uint32_t seed=0;seed<100;++seed) {
        Game g=fresh(1000,seed);
        for(int stage=1;stage<=3;++stage) {
            unsigned total=0;
            check("stage goal follows cumulative targets",g.target==(stage==1?300:stage==2?750:1250)&&g.level==stage&&g.secondsLeft()==60);
            for(int i=0;i<g.oreCount;++i) {
                const auto &o=g.ores[i];total+=o.value;
                check("ore fits visible mine and has valid weight",o.x-o.radius>=6&&o.x+o.radius<=314&&o.y-o.radius>=64&&o.y+o.radius<=198&&o.weight>0&&!o.removed);
            }
            check("every layout contains enough reachable value",total>=g.target-g.score&&near(g.ores[9].x,160)&&near(g.ores[9].y,110));
            if(seed&&g.ores[0].x!=62)varied=true;
            // Play two natural vertical casts: the near diamond is collected
            // first and the far one is exposed next. No score/state fixtures.
            const uint32_t t=1000+stage*10000;
            fixedRay(g,t);g.tick(t+3000);
            if(g.state==State::Playing){fixedRay(g,t+4000);g.tick(t+8000);}
            check("natural ore catches clear each target",g.state==(stage==3?State::Won:State::LevelCleared)&&g.score>=g.target);
            if(stage<3)check("next level keeps earned score and resets timer",g.nextLevel(t+9000)&&g.state==State::Playing&&!g.nextLevel(t+9000));
            else check("third clear is final win",!g.nextLevel(t+9000));
        }
        const auto best=g.best;g.restart(100000);
        check("restart resets run and preserves record",g.state==State::Playing&&g.level==1&&g.score==0&&g.best==best&&g.remaining==10);
    }
    check("seeds vary ore placement",varied);
}
}
int main() {
    readinessAndClock();castsAndTunneling();weightAndDeadline();layoutsAndThreeLevels();
    std::printf("Miner engine: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
