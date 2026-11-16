#include "src/pages/breakout_engine.h"
#include <cstdio>
#include <cmath>
using namespace BreakoutEngine;
namespace {
int checks=0,failures=0;
void check(const char *what,bool condition){++checks;if(!condition){++failures;std::printf("FAIL %s\n",what);}}
bool near(float a,float b){return std::fabs(a-b)<.02f;}
Game fresh(){Game g;g.start(1000,20261113u);return g;}
void controls(){
    Game g;g.best=123;g.prepare(0);check("prepare preserves record",g.best==123&&g.state==State::Ready&&!g.launch(100));
    check("ready rejects drag/pause",!g.beginDrag(10,20)&&!g.pause(100)&&!g.resume(100));
    g=fresh();check("start has three lives and 24 bricks",g.state==State::Serving&&g.lives==3&&g.remaining==24);
    g.movePaddle(-1000);check("left clamp and serving ball follow",near(g.paddleX,36)&&near(g.ballX,36));
    g.movePaddle(1000);check("right clamp",near(g.paddleX,284));
    g.movePaddle(160);check("relative drag begins",g.beginDrag(150,180));g.drag(200,40);g.endDrag();
    check("drag changes only x relatively",near(g.paddleX,210)&&!g.drag(250,100));
    check("launch starts movement",g.launch(1000)&&g.state==State::Playing&&!g.launch(1000));
    const float x=g.ballX,y=g.ballY;g.tick(1005);check("launched ball moves",g.ballX!=x&&g.ballY<y);
    check("playing pause freezes phase",g.pause(1010)&&g.state==State::Paused&&g.pausedState==State::Playing);
    const auto t=g.elapsedMs;const auto pausedY=g.ballY;
    check("paused ignores clock and launch",!g.tick(90000)&&!g.launch(90000)&&g.elapsedMs==t&&g.ballY==pausedY);
    g.resume(100000);g.tick(100005);check("resume retains exact progress",g.elapsedMs==t+5);
    g=fresh();g.lastAt=0xFFFFFFF0u;g.tick(0x22u);check("unsigned millisecond wrap",g.elapsedMs==50);
    g=fresh();g.tick(100000);check("long stall has bounded catch-up",g.elapsedMs==1000);
}
void collisions(){
    Game g=fresh();g.launch(1000);g.ballX=9;g.ballY=150;g.ballVX=-190;g.ballVY=-60;g.tick(1010);
    check("left wall reflects inward",g.ballX>=8&&g.ballVX>0);
    g.ballX=311;g.ballVX=190;g.tick(1020);check("right wall reflects inward",g.ballX<=312&&g.ballVX<0);
    for(auto &b:g.bricks)b.removed=true;g.remaining=1;
    g.ballX=160;g.ballY=27;g.ballVX=30;g.ballVY=-190;g.tick(1030);check("top wall reflects downward",g.ballY>=26&&g.ballVY>0);
    g.ballX=160;g.ballY=181;g.ballVX=0;g.ballVY=200;g.tick(1050);
    check("paddle reflects up with horizontal progress",g.ballVY<0&&std::fabs(g.ballVX)>=25&&g.lives==3);
    g.ballX=4;g.ballY=210;g.ballVY=200;g.tick(1070);check("miss loses one life and serves",g.lives==2&&g.state==State::Serving);
    for(int life=2;life>0;--life){g.launch(1070);g.ballX=4;g.ballY=215;g.ballVX=0;g.ballVY=200;g.tick(1075+(2-life)*5);}
    check("three misses end run",g.state==State::GameOver&&g.lives==0&&!g.launch(9000));

    g=fresh();g.launch(1000);
    const auto b=g.bricks[18];g.ballX=b.x+b.w/2;g.ballY=b.y+b.h+BallRadius+1;g.ballVX=0;g.ballVY=-1000;
    g.tick(1005);check("fast swept hit cannot tunnel through brick",g.bricks[18].removed&&g.score==20&&g.ballVY>0&&g.remaining==23);
    g.tick(1010);check("departing brick cannot score twice",g.score==20);
    g=fresh();g.level=2;g.state=State::LevelCleared;g.level=1;g.nextLevel(1000);g.launch(1000);
    auto &armored=g.bricks[0];g.ballX=armored.x+22;g.ballY=armored.y+armored.h+5;g.ballVX=0;g.ballVY=-200;g.tick(1010);
    check("armored first hit leaves one hp",armored.hp==1&&!armored.removed&&g.score==10);
    g.tick(1020);check("armored departure is one hit",g.score==10);
}
void progression(){
    Game g=fresh();uint32_t now=1000;
    for(int level=1;level<=3;++level){
        check("level has valid fixed board",g.level==level&&g.remaining==24&&g.brickCount==24&&g.state==State::Serving);
        g.launch(now);
        for(int id=0;id<g.brickCount;++id){
            auto &b=g.bricks[id];check("brick in stage and has hp",b.x>=0&&b.x+b.w<=320&&b.y>=22&&b.y+b.h<140&&b.hp>0);
            // Isolate one real downward-facing collision at a time. Retain all
            // production score/hp/remaining and level-clear transitions.
            while(!b.removed){
                g.ballX=b.x+b.w/2;g.ballY=b.y+b.h+BallRadius+1;g.ballVX=0;g.ballVY=-200;
                now+=10;g.tick(now);
            }
        }
        check("clearing final brick transitions immediately",g.state==(level==3?State::Won:State::LevelCleared)&&!g.remaining&&g.score==g.best);
        if(level<3){check("next level preserves score and lives",g.nextLevel(now)&&g.lives==3);}
        else check("win has no next level",!g.nextLevel(now));
    }
    const auto record=g.best;g.restart(now);
    check("restart retains best and resets stages",g.best==record&&g.score==0&&g.level==1&&g.lives==3&&g.remaining==24&&g.state==State::Serving);
    g.launch(now);for(int i=0;i<250&&g.score==0;++i){now+=5;g.tick(now);}
    check("natural new serve earns points without fixtures",g.score>0);
}
void naturalThreeStageRun(){
    Game g=fresh();uint32_t now=1000;
    for(int stage=1;stage<=3;++stage){
        g.launch(now);
        for(uint32_t played=0;g.state==State::Playing&&played<300000;played+=5){
            // Legal paddle inputs only: steer the next bounce toward a visible
            // remaining column. Score, ball motion and all collisions stay in
            // the production rules rather than being fabricated by a fixture.
            const Brick *target=nullptr;
            for(const auto &brick:g.bricks)if(!brick.removed&&(!target||std::fabs(brick.x+brick.w/2-g.ballX)<std::fabs(target->x+target->w/2-g.ballX)))target=&brick;
            const float dx=target?target->x+target->w/2-g.ballX:0;
            const float dy=target?PaddleY-target->y:150;
            const float offset=clamp(dx/std::sqrt(dx*dx+dy*dy)/.82f,-.9f,.9f);
            g.movePaddle(g.ballX-offset*g.paddleWidth/2);
            now+=5;g.tick(now);
        }
        if(g.state!=(stage==3?State::Won:State::LevelCleared))std::printf("natural stage=%d state=%d remaining=%u lives=%u ball=(%.2f,%.2f) velocity=(%.2f,%.2f)\n",stage,(int)g.state,(unsigned)g.remaining,(unsigned)g.lives,g.ballX,g.ballY,g.ballVX,g.ballVY);
        check("ordinary paddle tracking can clear a whole stage",g.state==(stage==3?State::Won:State::LevelCleared));
        if(stage<3&&!g.nextLevel(now))return;
    }
    check("natural three-stage play reaches final score with lives intact",g.state==State::Won&&g.score==1680&&g.lives==3);
}
}
int main(){controls();collisions();progression();naturalThreeStageRun();std::printf("Breakout engine: %d checks, %d failures\n",checks,failures);return failures?1:0;}
