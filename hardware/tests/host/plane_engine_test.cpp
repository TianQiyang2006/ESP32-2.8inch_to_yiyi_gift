// Production flight rules without Arduino/LVGL or a connected device.
#include "src/pages/plane_engine.h"
#include <cmath>
#include <cstdio>

using namespace PlaneEngine;
namespace {
int checks=0,failures=0;
void check(const char *name,bool okay){++checks;if(!okay){++failures;std::printf("FAIL %s\n",name);}}
bool near(float a,float b){return std::fabs(a-b)<.001f;}
Game fresh(uint32_t now=1000){Game game;game.start(now,20261113u);return game;}
void advance(Game &game,uint32_t &now,unsigned ms){for(unsigned done=0;done<ms;done+=10){now+=10;game.tick(now);}}
void bullet(Bullet &b,float x,float y,float vx,float vy){b.x=x;b.y=y;b.vx=vx;b.vy=vy;b.active=true;}
void enemy(Enemy &e,float x,float y,uint8_t kind=0,uint8_t hp=1,float vx=0,float vy=0){e=Enemy();e.x=x;e.y=y;e.kind=kind;e.hp=hp;e.vx=vx;e.vy=vy;e.active=true;}

void lifecycleAndInput(){
    Game game;game.best=123;game.prepare(0);
    check("prepare keeps record and starts ready",game.state==State::Ready&&game.best==123&&game.lives==3&&game.wave==1&&game.randomState!=0);
    check("ready has no moving objects",!game.shotCount()&&!game.hostileCount()&&!game.aliveEnemies());
    check("ready refuses simulation and input",!game.tick(9000)&&!game.beginDrag(100,100)&&!game.drag(200,100)&&!game.pause(9000)&&!game.resume(9000));
    game.start(1000,42);
    check("new run resets counters but preserves record",game.state==State::Playing&&game.score==0&&game.best==123&&game.kills==0&&game.lives==3&&game.wave==1);
    check("touch can start away from the aircraft",game.beginDrag(50,60));
    check("touch down does not teleport the ship",near(game.shipX,160)&&near(game.shipY,180));
    check("second touch cannot replace drag origin",!game.beginDrag(200,200));
    check("movement follows relative finger delta",game.drag(70,55)&&near(game.shipX,180)&&near(game.shipY,175));
    game.drag(-500,-500);
    check("upper left motion clamps at flight boundaries",near(game.shipX,MinX)&&near(game.shipY,MinY));
    game.drag(900,900);
    check("lower right motion clamps at flight boundaries",near(game.shipX,MaxX)&&near(game.shipY,MaxY));
    check("clamped motion produces no false update",!game.drag(901,901));
    check("release ends exactly one drag",game.endDrag()&&!game.endDrag()&&!game.drag(0,0));
    check("next drag establishes a fresh relative origin",game.beginDrag(0,0)&&game.drag(-10,-20)&&near(game.shipX,298)&&near(game.shipY,174));
    game.pause(1000);
    check("pause consumes held contact",game.state==State::Paused&&!game.dragging&&!game.drag(0,0)&&!game.beginDrag(0,0));
    game.resume(2000);
    check("resume needs fresh contact",!game.dragging&&!game.drag(10,10)&&!game.resume(2000));
    game.score=20;game.best=123;game.lives=1;game.restart(3000);
    check("restart resets flight and tools, keeps record",game.score==0&&game.best==123&&game.kills==0&&game.lives==3&&game.wave==1&&!game.dragging&&!game.shotCount());
}
void geometry(){
    check("fast vertical segment hits thin box",swept(5,-100,5,100,0,0,10,1));
    check("fast reverse segment hits thin box",swept(5,100,5,-100,0,0,10,1));
    check("inclusive grazing edge hits",swept(10,-10,10,10,0,0,10,5));
    check("parallel outside segment misses",!swept(10.01f,-10,10.01f,10,0,0,10,5));
    check("stationary interior point hits",swept(2,2,2,2,0,0,5,5));
    check("stationary exterior point misses",!swept(6,2,6,2,0,0,5,5));
    check("diagonal bounding overlap alone does not hit",!swept(0,0,10,10,0,8,2,10));
    check("segment beyond endpoint misses",!swept(0,0,1,1,2,2,3,3));
}
void shotsAndScoring(){
    Game game=fresh();game.shotMs=0;
    enemy(game.enemies[0],160,80);bullet(game.shots[0],160,130,0,-10000);
    check("swept player shot damages crossing enemy",game.tick(1010)&&!game.enemies[0].active&&!game.shots[0].active&&game.score==10&&game.best==10&&game.kills==1);
    check("destroyed enemy does not score repeatedly",game.tick(1020)&&game.score==10&&game.kills==1);
    game=fresh();game.shotMs=0;enemy(game.enemies[0],160,80);bullet(game.shots[0],173,80,0,0);game.tick(1010);
    check("shot at collision edge counts",game.score==10);
    game=fresh();game.shotMs=0;enemy(game.enemies[0],160,80);bullet(game.shots[0],173.01f,80,0,0);game.tick(1010);
    check("shot immediately outside collision edge misses",game.score==0&&game.enemies[0].active);
    game=fresh();game.shotMs=0;enemy(game.enemies[0],80,80,0,1,20000);bullet(game.shots[0],160,80,0,0);game.tick(1010);
    check("relative sweep detects enemy moving across shot",game.score==10&&game.kills==1);
    game=fresh();game.shotMs=0;enemy(game.enemies[0],160,80,1,2);bullet(game.shots[0],160,80,0,0);game.tick(1010);
    check("armour absorbs first hit without points",game.enemies[0].active&&game.enemies[0].hp==1&&game.score==0&&!game.shots[0].active);
    bullet(game.shots[0],160,80,0,0);game.tick(1020);
    check("armour second hit awards one kill and twenty",!game.enemies[0].active&&game.score==20&&game.kills==1);
    game=fresh();game.wave=4;game.spawned=1;game.shotMs=0;game.beginDrag(0,0);
    enemy(game.enemies[0],160,44,2,2);bullet(game.shots[0],160,44,0,0);game.tick(1010);
    check("boss remains active until last hp",game.enemies[0].hp==1&&game.state==State::Playing&&game.score==0);
    bullet(game.shots[0],160,44,0,0);bullet(game.hostile[0],160,180,0,0);game.tick(1020);
    check("boss defeat awards two hundred and ends in victory",game.state==State::Won&&game.score==200&&game.best==200&&game.kills==1&&!game.dragging&&!game.hostileCount());
    check("victory rejects further clock and movement",!game.tick(9000)&&!game.beginDrag(100,100)&&!game.pause(9000));
    game.restart(10000);
    check("victory can restart with its record",game.state==State::Playing&&game.best==200&&game.score==0&&game.lives==3);
}
void damageAndInvulnerability(){
    Game game=fresh();uint32_t now=1000;game.shotMs=0;
    bullet(game.hostile[0],160,130,0,10000);advance(game,now,10);
    check("fast hostile shot cannot tunnel through player",game.lives==2&&!game.hostile[0].active&&game.invulnerableMs==InvincibleMs);
    for(int i=0;i<6;++i)bullet(game.hostile[i],160,180,0,0);
    advance(game,now,10);
    check("simultaneous hits cost only one life",game.lives==2&&!game.hostileCount()&&game.invulnerableMs==1190);
    advance(game,now,1180);
    check("invulnerability lasts until exact boundary",game.invulnerableMs==10&&game.lives==2);
    bullet(game.hostile[0],160,180,0,0);advance(game,now,10);
    check("hit at protection expiry costs a life",game.lives==1&&game.invulnerableMs==InvincibleMs);
    // Isolate the third independent contact from the intentionally generated
    // battlefield. It still resolves through the production simulation.
    for(auto &e:game.enemies)e.active=false;for(auto &b:game.hostile)b.active=false;
    game.invulnerableMs=0;game.beginDrag(0,0);bullet(game.hostile[0],160,180,0,0);advance(game,now,10);
    check("third independent hit ends the game",game.lives==0&&game.state==State::GameOver&&!game.dragging);
    check("game over freezes the world",!game.tick(now+10000)&&!game.beginDrag(0,0)&&!game.pause(now+10000));
    game=fresh();game.shotMs=0;bullet(game.hostile[0],100,180,0,0);game.beginDrag(0,0);game.drag(-120,0);
    check("large drag sweeps through hostile bullet",game.lives==2&&!game.hostile[0].active&&near(game.shipX,40));
    game=fresh();game.shotMs=0;enemy(game.enemies[0],160,130,0,1,0,10000);game.tick(1010);
    check("enemy crossing player causes one collision without kill score",game.lives==2&&!game.enemies[0].active&&game.score==0&&game.kills==0);
    game=fresh();game.shotMs=0;enemy(game.enemies[0],100,180);game.beginDrag(0,0);game.drag(-120,0);
    check("ship drag cannot tunnel through enemy plane",game.lives==2&&!game.enemies[0].active);
}
void fixedPools(){
    Game game=fresh();
    for(int i=0;i<MaxShots;++i)bullet(game.shots[i],20+i*2,100,0,0);
    game.tick(1010);bool preserved=true;
    for(int i=0;i<MaxShots;++i)preserved=preserved&&near(game.shots[i].x,20+i*2)&&near(game.shots[i].y,100);
    check("full player shot pool never overwrites a live shot",game.shotCount()==MaxShots&&preserved);
    game.shots[4].active=false;game.shotMs=170;game.tick(1020);
    check("released player shot slot is reused",game.shotCount()==MaxShots&&near(game.shots[4].x,160)&&game.shots[4].vy==-240);
    game=fresh();game.wave=4;game.spawned=1;game.shotMs=0;
    enemy(game.enemies[0],160,44,2,48);game.enemies[0].fireMs=710;
    for(int i=0;i<MaxHostile;++i)bullet(game.hostile[i],5+i,90,0,0);
    game.tick(1010);preserved=true;
    for(int i=0;i<MaxHostile;++i)preserved=preserved&&near(game.hostile[i].x,5+i)&&near(game.hostile[i].y,90);
    check("full hostile pool refuses spread without overwriting",game.hostileCount()==MaxHostile&&preserved);
    game.hostile[0].active=false;game.hostile[3].active=false;game.enemies[0].fireMs=710;game.tick(1020);
    check("boss spread safely fills only free hostile slots",game.hostileCount()==MaxHostile&&game.hostile[0].vy==96&&game.hostile[3].vy==108);
    game=fresh();game.shotMs=0;bullet(game.shots[0],20,-9,0,-240);bullet(game.hostile[0],5,215,0,200);game.tick(1010);
    check("offscreen shots are recycled",!game.shots[0].active&&!game.hostile[0].active);
    game=fresh();game.shotMs=0;enemy(game.enemies[0],290,217,0,1,0,200);game.tick(1010);
    check("escaped enemies are recycled without fabricated points",!game.enemies[0].active&&game.score==0&&game.kills==0&&game.lives==3);
}
bool sameWorld(const Game &a,const Game &b){
    if(a.state!=b.state||a.score!=b.score||a.best!=b.best||a.kills!=b.kills||a.lives!=b.lives||a.wave!=b.wave||a.spawned!=b.spawned||
       a.elapsedMs!=b.elapsedMs||a.stepRemainder!=b.stepRemainder||a.randomState!=b.randomState||a.invulnerableMs!=b.invulnerableMs||a.shotMs!=b.shotMs||a.spawnMs!=b.spawnMs)return false;
    for(int i=0;i<MaxEnemies;++i){const auto &x=a.enemies[i],&y=b.enemies[i];if(x.active!=y.active||x.hp!=y.hp||x.kind!=y.kind||!near(x.x,y.x)||!near(x.y,y.y)||!near(x.vx,y.vx)||!near(x.vy,y.vy)||x.fireMs!=y.fireMs)return false;}
    for(int which=0;which<2;++which){const Bullet *x=which?a.hostile:a.shots,*y=which?b.hostile:b.shots;const int n=which?MaxHostile:MaxShots;
        for(int i=0;i<n;++i)if(x[i].active!=y[i].active||!near(x[i].x,y[i].x)||!near(x[i].y,y[i].y)||!near(x[i].vx,y[i].vx)||!near(x[i].vy,y[i].vy))return false;}
    return true;
}
void timing(){
    Game game=fresh(0);game.tick(95);
    check("substeps preserve remaining milliseconds",game.elapsedMs==90&&game.stepRemainder==5);
    check("pause advances only the unprocessed active time",game.pause(98)&&game.state==State::Paused&&game.elapsedMs==90&&game.stepRemainder==8);
    const Game paused=game;
    check("paused ticks do not update any world field",!game.tick(9000)&&sameWorld(game,paused));
    game.resume(10000);game.tick(10001);
    check("resume keeps substep remainder without paused elapsed",game.elapsedMs==90&&game.stepRemainder==9);
    game.tick(10002);
    check("resume advances at exact remaining substep boundary",game.elapsedMs==100&&game.stepRemainder==0&&near(paused.shots[0].y-game.shots[0].y,2.4f));
    game=fresh(0xFFFFFFF0u);game.tick(0xEu);
    check("simulation delta crosses millis rollover",game.elapsedMs==30&&game.stepRemainder==0);
    game.pause(0x14u);game.resume(0xFFFFFFF8u);game.tick(2);
    check("resume anchor can itself cross rollover",game.elapsedMs==40&&game.stepRemainder==6);
    game=fresh();game.tick(101000);
    check("long display stalls have bounded unseen simulation",game.elapsedMs==1000&&game.stepRemainder==0);
    Game a=fresh(0),b=fresh(0);uint32_t now=0;
    advance(a,now,10000);now=0;const unsigned cadence[]={7,31,4,58,13,17,21};unsigned at=0;
    while(now<10000){unsigned step=cadence[at++%7];if(now+step>10000)step=10000-now;now+=step;b.tick(now);}
    check("fixed simulation gives identical world under irregular render cadence",sameWorld(a,b));
}
void naturalBattleAndCampaign(){
    Game game=fresh(0);uint32_t now=0;
    advance(game,now,390);check("first target waits until the launch grace expires",game.aliveEnemies()==0);
    advance(game,now,10);check("first target is a centered fighter",game.enemies[0].active&&near(game.enemies[0].x,160)&&game.enemies[0].kind==0&&near(game.enemies[0].vx,0));
    while(game.score==0&&now<2500)advance(game,now,10);
    check("new game earns its first real kill by automatic shots",game.score==10&&game.kills==1&&game.best==10&&game.state==State::Playing&&game.lives==3);
    game=fresh();game.spawned=(uint8_t)game.quota();enemy(game.enemies[0],20,70);bullet(game.hostile[0],270,90,0,0);game.tick(1010);
    check("wave never advances while its last enemy is alive",game.wave==1);
    game.enemies[0].active=false;game.tick(1020);
    check("cleared wave advances and clears old hostile shots",game.wave==2&&game.spawned==0&&!game.hostileCount()&&game.spawnMs==0);
    check("later waves add planes and shorten spawn spacing",game.quota()==10&&game.spawnInterval()==1000);

    // A protected test pilot allows the entire generated campaign to run in
    // one deterministic trace. Enemy HP, shooting, movement, waves, collisions
    // and scoring remain the production rules; protection isolates progression.
    game=fresh(0);now=0;game.shipX=12;game.invulnerableMs=65535;
    unsigned maxSpawn[4]={0,0,0,0};bool inBounds=true;
    while(game.wave<4&&now<90000&&game.state==State::Playing){
        advance(game,now,10);if(game.wave<=3&&game.spawned>maxSpawn[game.wave-1])maxSpawn[game.wave-1]=game.spawned;
        for(const auto &e:game.enemies)if(e.active)inBounds=inBounds&&e.x>=halfWidth(e)+3&&e.x<=Width-halfWidth(e)-3;
    }
    check("generated waves spawn exactly eight ten twelve enemies",maxSpawn[0]==8&&maxSpawn[1]==10&&maxSpawn[2]==12);
    check("generated enemies stay within horizontal bounds",inBounds);
    check("third wave leads to one full health boss",game.wave==4&&game.aliveEnemies()==1&&game.enemies[0].kind==2&&game.enemies[0].hp==48);
    game.invulnerableMs=65535;game.beginDrag(0,0);const int originX=(int)game.shipX,originY=(int)game.shipY;
    unsigned maxSpread=0;bool bossBounded=true;
    while(game.state==State::Playing&&now<120000){
        for(const auto &e:game.enemies)if(e.active&&e.kind==2){
            bossBounded=bossBounded&&e.x>=32&&e.x<=288&&e.y<=44;
            game.drag((int)e.x-originX,105-originY);
        }
        advance(game,now,10);if(game.hostileCount()>maxSpread)maxSpread=game.hostileCount();
    }
    check("boss patrol stays inside its playfield",bossBounded);
    check("boss actually fires a multi shot pattern",maxSpread>=3);
    check("automatic fire can finish generated boss campaign",game.state==State::Won&&game.lives==3&&game.kills>=1&&game.score>=200&&game.best==game.score&&!game.dragging);
    check("finite game score cannot exceed complete enemy reward",game.score<=540&&game.kills<=31);
}
}
int main(){
    lifecycleAndInput();geometry();shotsAndScoring();damageAndInvulnerability();fixedPools();timing();naturalBattleAndCampaign();
    std::printf("Plane engine: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
