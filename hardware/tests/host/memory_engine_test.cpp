#include "src/pages/memory_engine.h"
#include <cstdio>
using namespace MemoryEngine;
namespace {
int checks=0, failures=0;
void check(const char *s,bool ok){++checks;if(!ok){++failures;std::printf("FAIL %s\n",s);}}
int partner(const Game &g,int first){for(int i=0;i<g.count;++i)if(i!=first&&g.cards[i].face==g.cards[first].face)return i;return -1;}
void basics(){
    Game g;g.best=123;g.prepare(0);
    check("ready preserves best and ignores actions",g.state==State::Ready&&g.best==123&&!g.tick(10)&&!g.select(0,10)&&!g.pause(10)&&!g.resume(10));
    g.start(100,42);check("twelve cards six pairs",g.count==12&&g.totalPairs==6&&g.pairs==0&&g.first==-1);
    check("invalid selections rejected",!g.select(-1,100)&&!g.select(12,100));
    check("first card revealed",g.select(0,100)&&g.first==0&&g.visible(0)&&g.moves==0);
    check("same card cannot pair with itself",!g.select(0,100)&&g.moves==0);
    const int mate=partner(g,0);check("match awards once",g.select(mate,101)&&g.cards[0].matched&&g.cards[mate].matched&&g.score==100&&g.moves==1&&g.pairs==1);
    check("matched cards ignore future taps",!g.select(0,102)&&!g.select(mate,102)&&g.score==100);
    int a=-1,b=-1;for(int i=0;i<g.count;++i)if(!g.cards[i].matched){if(a<0)a=i;else if(g.cards[i].face!=g.cards[a].face){b=i;break;}}
    check("mismatch shows both",g.select(a,200)&&g.select(b,210)&&g.state==State::Comparing&&g.visible(a)&&g.visible(b)&&g.moves==2);
    check("third card blocked",!g.select(mate,211)&&g.first==a&&g.second==b);
    g.tick(500);check("mismatch display interval",g.state==State::Comparing&&g.compareRemaining==460);
    check("pause retains mismatch phase",g.pause(510)&&g.pausedState==State::Comparing&&g.compareRemaining==450);
    const auto time=g.elapsedMs;g.tick(100000);check("paused clock and mismatch freeze",g.elapsedMs==time&&g.compareRemaining==450);
    check("resume reanchors",g.resume(200000)&&!g.resume(200001));
    g.tick(200449);check("last mismatch millisecond remains",g.state==State::Comparing&&g.compareRemaining==1);
    g.tick(200450);check("mismatch flips back precisely",g.state==State::Playing&&!g.visible(a)&&!g.visible(b)&&g.first==-1&&g.second==-1);
    g.start(0xFFFFFFF0u,1);g.select(0,0xFFFFFFF0u);
    for(int i=1;i<g.count;++i)if(g.cards[i].face!=g.cards[0].face){g.select(i,0xFFFFFFF0u);break;}
    g.tick(0x20u);check("clock wrap consumes 48ms",g.elapsedMs==48&&g.compareRemaining==CompareMs-48);
    g.tick(0x2deu);check("wrap mismatch completes",g.state==State::Playing);
}
void layoutsAndFullRuns(){
    for(uint32_t seed=0;seed<100;++seed){
        Game g;g.start(100,seed);uint32_t now=100;
        for(int level=1;level<=3;++level){
            unsigned counts[10]={};
            check("level count and clean selection",g.level==level&&g.count==8+4*level&&g.pairs==0&&g.moves==0&&g.first==-1);
            for(int i=0;i<g.count;++i){check("face bounds and unmatched",g.cards[i].face<g.totalPairs&&!g.cards[i].matched);++counts[g.cards[i].face];
                const int x=23+(i%4)*70+32,y=g.top()+(i/4)*36+15;
                check("card center hits correct card",g.hitTest(x,y)==i);
            }
            for(int face=0;face<g.totalPairs;++face)check("exactly two of each face",counts[face]==2);
            check("gaps and offboard reject",g.hitTest(22,g.top())==-1&&g.hitTest(87,g.top())==-1&&g.hitTest(55,g.top()+30)==-1&&g.hitTest(55,208)==-1);
            for(int i=0;i<g.count;++i)if(!g.cards[i].matched){const int mate=partner(g,i);check("natural pair selection",g.select(i,++now)&&g.select(mate,++now));}
            check("perfect round bonus and completion",g.pairs==g.totalPairs&&g.moves==g.totalPairs&&g.state==(level==3?State::Won:State::LevelCleared));
            check("completed round ignores clock and pause",!g.tick(now+10000)&&!g.select(0,now)&&!g.pause(now));
            if(level<3)check("next level resets round and keeps score",g.nextLevel(++now)&&g.elapsedMs==0);
        }
        check("all three perfect levels total 3000",g.score==3000&&g.best==3000&&!g.nextLevel(++now));
        g.restart(++now);check("restart resets round preserves record",g.level==1&&g.score==0&&g.moves==0&&g.best==3000&&g.state==State::Playing);
    }
    Game g;g.start(0,10);uint32_t now=0;
    for(int n=0;n<25;++n){g.select(0,now);for(int j=1;j<g.count;++j)if(g.cards[j].face!=g.cards[0].face){g.select(j,now);break;}now+=CompareMs;g.tick(now);}
    for(int i=0;i<g.count;++i)if(!g.cards[i].matched){g.select(i,now);g.select(partner(g,i),now);}
    check("many mistakes cannot produce negative bonus",g.score==600&&g.moves==31&&g.state==State::LevelCleared);
}
}
int main(){basics();layoutsAndFullRuns();std::printf("Memory engine: %d checks, %d failures\n",checks,failures);return failures?1:0;}
