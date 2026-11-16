#include "src/pages/pin_engine.h"
#include <cstdio>
#include <cmath>

using namespace PinEngine;
namespace {
int checks=0,failures=0;
void check(const char *name,bool result) {
    ++checks;if(!result){++failures;std::printf("FAIL %s\n",name);}
}
bool near(float a,float b){return std::fabs(a-b)<0.005f;}
Game fresh(uint32_t now=1000){Game g;g.start(now,20261113u);return g;}
void advance(Game &g,uint32_t &now,uint32_t duration) {
    while(duration){const uint32_t step=duration>20?20:duration;now+=step;g.tick(now);duration-=step;}
}
void anglesAndGeometry() {
    check("negative angle normalizes",near(normalize(-1),359));
    check("multiple revolutions normalize",near(normalize(1081),1));
    check("gap crosses zero",near(gap(359,1),2)&&near(gap(-1,361),2));
    check("gap is symmetric and short",near(gap(10,350),20)&&near(gap(350,10),20));
    Game g=fresh();g.attached=1;g.pins[0]=359;g.rotation=0;
    check("near zero is blocked",!g.slotClear(1));
    g.pins[0]=MinGapDegrees;
    check("minimum angle boundary is allowed",g.slotClear());
    g.pins[0]=MinGapDegrees-0.01f;
    check("inside minimum angle is blocked",!g.slotClear());
    g=fresh();g.attached=1;g.pins[0]=0;g.speed=0;g.flightY=138;
    check("flying tip contacts old pin head before reaching hub",g.flightCollision());
    g.flightY=155;
    check("separate flight pin has no premature collision",!g.flightCollision());
    g.pins[0]=90;g.flightY=ContactTipY;
    check("sideways pin does not block vertical ray",!g.flightCollision());
}
void stateAndFlight() {
    Game g;g.best=9;g.prepare(7);
    check("prepare is ready and preserves record",g.state==State::Ready&&g.best==9&&g.score==0&&g.remaining==6&&g.attached==3);
    check("ready blocks simulation and shooting",!g.tick(4000)&&!g.shoot(4000)&&!g.pause(4000)&&!g.resume(4000)&&!g.nextLevel(4000));
    g=fresh();check("first shot launches",g.shoot(1000)&&g.state==State::Flying);
    check("shot has no immediate score",g.score==0&&g.remaining==6&&near(g.flightY,LaunchTipY));
    check("same-time shot refused",!g.shoot(1000));
    g.tick(1100);
    check("visible flight advances without early attachment",g.state==State::Flying&&g.flightElapsedMs==100&&g.flightY<LaunchTipY&&g.flightY>ContactTipY&&g.score==0);
    check("flight accepts no queued second shot",!g.shoot(1239)&&g.state==State::Flying&&g.score==0);
    check("clock-completed shot still cannot chain from flight press",!g.shoot(1240)&&g.state==State::Playing&&g.score==1&&g.remaining==5&&g.attached==4);
    check("successful pin is attached at bottom contact",gap(g.worldAngle(3),0)<0.01f&&near(g.flightY,ContactTipY)&&g.best==1);
    g=fresh();g.attached=1;g.pins[0]=0;g.speed=0;g.shoot(1000);g.tick(1240);
    check("head collision ends flight before hub and awards nothing",g.state==State::GameOver&&g.flightElapsedMs<FlightMs&&g.flightY>ContactTipY&&g.score==0&&g.remaining==6);
    check("loss ignores extra input",!g.shoot(2000)&&!g.tick(2000)&&!g.nextLevel(2000));
    // This pin begins outside the ray and swings through it during the flight.
    g=fresh();g.attached=1;g.pins[0]=340;g.speed=160;g.shoot(1000);g.tick(1240);
    check("rotating spoke can hit the incoming flight",g.state==State::GameOver&&g.score==0);
    g=fresh();g.attached=1;g.pins[0]=180;g.speed=0;g.shoot(1000);g.tick(1240);
    check("clear opposite pin permits contact",g.state==State::Playing&&g.score==1);
}
void pauseAndWrap() {
    Game g=fresh();g.shoot(1000);g.tick(1100);g.pause(1110);
    const auto progress=g.flightElapsedMs;const auto elapsed=g.elapsedMs;const auto angle=g.rotation,y=g.flightY;
    check("pause remembers flying phase",g.state==State::Paused&&g.pausedState==State::Flying);
    check("paused time and inputs do nothing",!g.tick(80000)&&!g.shoot(80000)&&!g.pause(80000)&&g.elapsedMs==elapsed&&near(g.rotation,angle)&&near(g.flightY,y));
    check("resume restores phase",g.resume(90000)&&g.state==State::Flying&&!g.resume(90000));
    g.tick(90030);check("resume consumes only new elapsed time",g.flightElapsedMs==progress+30);
    g=fresh(0xFFFFFFF0u);g.tick(0x54u);
    check("millisecond wrap keeps correct rotation",g.elapsedMs==100&&near(g.rotation,5.5f));
    g.pause(0x64u);const auto before=g.elapsedMs;g.resume(0xFFFFFFF0u);g.tick(0x18u);
    check("pause/resume can cross wrap",g.elapsedMs==before+40);
    g=fresh();g.tick(11000);
    check("stalled frame is bounded",g.elapsedMs==250&&near(g.rotation,13.75f));
}
bool safeShot(Game &g,uint32_t &now) {
    // Search future rotation in copies, then execute the same timed shot on the
    // untouched game. No score, attached pin, or stage state is manufactured.
    for(unsigned wait=0;wait<9000;wait+=10) {
        Game forecast=g;
        if(forecast.shoot(now)) {
            forecast.tick(now+FlightMs);
            if(forecast.state!=State::GameOver) {
                const auto before=g.score;
                if(!g.shoot(now))return false;
                advance(g,now,FlightMs);
                check("solver shot grants exactly one point",g.score==before+1);
                return g.state!=State::GameOver;
            }
        }
        advance(g,now,10);
    }
    return false;
}
void completeThreeLevels() {
    for(unsigned run=0;run<8;++run) {
        uint32_t now=1000+run*100000;Game g=fresh(now);
        for(int stage=1;stage<=3;++stage) {
            check("stage layout and direction are distinct",g.level==stage&&g.attached==stage+2&&g.remaining==(stage==1?6:stage==2?8:10)&&
                  (stage==2?g.speed<0:g.speed>0));
            const unsigned required=g.remaining;
            for(unsigned shot=0;shot<required;++shot)check("every stage has a playable next shot",safeShot(g,now));
            check("stage completes after final successful pin",g.remaining==0&&g.state==(stage==3?State::Won:State::LevelCleared));
            if(stage<3)check("next stage preserves earned points",g.nextLevel(now)&&g.state==State::Playing&&g.score==(stage==1?6:14)&&!g.nextLevel(now));
            else check("third stage wins at total 24 points",g.score==24&&g.best==24&&!g.nextLevel(now));
        }
        g.restart(now);check("restart keeps record",g.state==State::Playing&&g.score==0&&g.best==24&&g.level==1);
    }
}
}
int main(){anglesAndGeometry();stateAndFlight();pauseAndWrap();completeThreeLevels();
    std::printf("Pin engine: %d checks, %d failures\n",checks,failures);return failures?1:0;}
