#include "src/pages/mines_engine.h"
#include <cstdio>

using namespace MinesEngine;
namespace {
int checks=0,failures=0;
void check(const char *name,bool value){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
Game fresh(uint32_t now=1000,uint32_t seed=20261113u){Game game;game.start(now,seed);return game;}
unsigned mineCount(const Game &game){unsigned n=0;for(const auto &c:game.cells)n+=c.mine;return n;}
unsigned revealed(const Game &game){unsigned n=0;for(const auto &c:game.cells)n+=c.revealed&&!c.mine;return n;}
int hiddenSafe(const Game &game){for(int id=0;id<Cells;++id)if(!game.cells[id].mine&&!game.cells[id].revealed)return id;return -1;}
int hiddenMine(const Game &game){for(int id=0;id<Cells;++id)if(game.cells[id].mine)return id;return -1;}
void lifecycle(){
    Game game;game.best=123;game.prepare(0);
    check("ready preserves legal best and seeds zero safely",game.best==123&&game.randomState!=0&&game.state==State::Ready&&!game.generated&&game.remaining==48);
    check("ready refuses inputs and time",!game.reveal(20,1000)&&!game.toggleFlag(20,1000)&&!game.toggleMode()&&!game.pause(1000)&&!game.resume(1000)&&!game.tick(1000));
    game.best=MaximumScore+1;game.prepare(1);check("obsolete impossible best is discarded",game.best==0);
    game=fresh();check("start is ungenerated and has no mines",game.state==State::Playing&&!game.generated&&mineCount(game)==0&&game.score==0&&game.flags==0&&game.elapsedMs==0);
    check("pre opening clock does not run",!game.tick(5000)&&game.elapsedMs==0);
    check("invalid cells refuse all action",!game.reveal(-1,5001)&&!game.reveal(Cells,5001)&&!game.toggleFlag(-1,5001)&&!game.toggleFlag(Cells,5001));
    check("mode toggles without generating board",game.toggleMode()&&game.flagMode&&!game.generated&&game.toggleMode()&&!game.flagMode);
    game.reveal(25,6000);game.tick(6300);const auto best=game.best;game.restart(7000);
    check("restart resets board flags and timer preserving record",game.state==State::Playing&&!game.generated&&game.score==0&&game.best==best&&game.remaining==48&&game.flags==0&&game.elapsedMs==0&&!game.flagMode);
}
void generation(){
    bool varied=false;
    for(unsigned seed=0;seed<64;++seed)for(int first=0;first<Cells;++first){
        Game game=fresh(0,seed);check("every first point reveals successfully",game.reveal(first,100));
        check("generated board has exactly twelve mines",game.generated&&mineCount(game)==MineCount);
        check("opening cell is zero and never loses",!game.cells[first].mine&&game.cells[first].adjacent==0&&game.cells[first].revealed&&game.state!=State::GameOver);
        bool safe=true,adjacentCorrect=true;
        for(int id=0;id<Cells;++id){
            const int dx=id%Cols-first%Cols,dy=id/Cols-first/Cols;
            if(dx>=-1&&dx<=1&&dy>=-1&&dy<=1)safe=safe&&!game.cells[id].mine;
            unsigned nearby=0;
            for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx){const int x=id%Cols+xx,y=id/Cols+yy;if((xx||yy)&&x>=0&&x<Cols&&y>=0&&y<Rows)nearby+=game.cells[y*Cols+x].mine;}
            adjacentCorrect=adjacentCorrect&&nearby==game.cells[id].adjacent;
        }
        check("all first neighbours are safe",safe);check("every adjacent count independently correct",adjacentCorrect);
        check("flood accounts each safe cell once",game.remaining+revealed(game)==48&&game.score==revealed(game)*10+(game.state==State::Won?200:0));
        if(seed==1&&first==25){Game other=fresh(0,2);other.reveal(25,100);for(int id=0;id<Cells;++id)varied=varied||other.cells[id].mine!=game.cells[id].mine;}
    }
    check("seeded boards vary instead of one fixed mine layout",varied);
    Game a=fresh(0,123),b=fresh(0,123);a.reveal(25,100);b.reveal(25,100);bool same=true;
    for(int id=0;id<Cells;++id)same=same&&a.cells[id].mine==b.cells[id].mine&&a.cells[id].adjacent==b.cells[id].adjacent;
    check("same seed and opening reproduce the same board",same);
}
void flagsAndFlood(){
    Game game=fresh();check("flag before opening is allowed",game.toggleFlag(25,1100)&&game.flags==1&&game.cells[25].flagged&&!game.generated);
    check("flag protects cell from reveal without generating",!game.reveal(25,1200)&&!game.generated&&game.remaining==48&&game.elapsedMs==0);
    check("second flag action removes flag",game.toggleFlag(25,1300)&&game.flags==0&&!game.cells[25].flagged);
    for(int i=0;i<MineCount;++i)check("each available flag can be placed",game.toggleFlag(i,1400));
    check("flag count cannot exceed actual mine budget",!game.toggleFlag(12,1500)&&game.flags==MineCount&&!game.cells[12].flagged);
    check("full flag budget still permits removal",game.toggleFlag(0,1600)&&game.flags==11&&game.toggleFlag(12,1700)&&game.flags==12);
    game=fresh();game.toggleFlag(24,1100);game.reveal(25,1200);
    check("flood never opens flagged safe neighbour",game.cells[24].flagged&&!game.cells[24].revealed&&!game.cells[24].mine&&game.remaining>0);
    const auto before=game.score;game.toggleFlag(24,1300);game.reveal(24,1400);
    check("removing safe flag allows normal reveal and points",game.cells[24].revealed&&game.score>=before+10);
    const auto score=game.score;check("revealed cell cannot be flagged or scored twice",!game.toggleFlag(25,1500)&&!game.reveal(25,1500)&&game.score==score);

    // Build an independent breadth-first oracle from the generated immutable
    // mine map, then compare every revealed cell from production flood.
    for(int first=0;first<Cells;++first){
        Game board=fresh(0,(uint32_t)(321+first));board.reveal(first,100);
        bool expected[Cells]={};uint8_t queue[Cells];unsigned head=0,tail=0;expected[first]=true;queue[tail++]=(uint8_t)first;
        while(head<tail){const int id=queue[head++];if(board.cells[id].adjacent)continue;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int x=id%Cols+dx,y=id/Cols+dy;if(x<0||x>=Cols||y<0||y>=Rows)continue;const int other=y*Cols+x;if(!expected[other]&&!board.cells[other].mine){expected[other]=true;queue[tail++]=(uint8_t)other;}}}
        bool matches=true;for(int id=0;id<Cells;++id)matches=matches&&board.cells[id].revealed==expected[id];
        check("flood matches independent reachable-zero oracle",matches);
    }
}
void endStates(){
    Game game=fresh();game.reveal(25,1000);const auto score=game.score,record=game.best;
    const int bomb=hiddenMine(game);check("mine click ends actual generated run",game.reveal(bomb,1500)&&game.state==State::GameOver&&game.exploded==bomb&&game.cells[bomb].revealed);
    check("mine click does not award points",game.score==score&&game.best==record&&game.elapsedMs==500);
    const auto elapsed=game.elapsedMs;check("loss freezes all gameplay inputs",!game.tick(5000)&&!game.reveal(hiddenSafe(game),5000)&&!game.toggleFlag(0,5000)&&!game.toggleMode()&&!game.pause(5000)&&game.elapsedMs==elapsed);
    game.restart(6000);game.reveal(25,6100);const int flagged=hiddenSafe(game);game.toggleFlag(flagged,6200);
    for(int id=0;id<Cells;++id)if(!game.cells[id].mine&&id!=flagged)game.reveal(id,6300);
    check("one flagged safe cell blocks victory",game.state==State::Playing&&game.remaining==1&&game.score==470);
    game.toggleFlag(flagged,6400);check("last safe reveal wins",game.reveal(flagged,6500)&&game.state==State::Won&&game.remaining==0);
    check("victory has exact bounded reward and automatic mine flags",game.score==MaximumScore&&game.best==MaximumScore&&game.flags==MineCount&&game.exploded==-1);
    bool flaggedMines=true;for(const auto &cell:game.cells)flaggedMines=flaggedMines&&(!cell.mine||cell.flagged);
    check("all and only remaining mines are marked on win",flaggedMines&&revealed(game)==48);
    check("victory cannot advance timer or gameplay",!game.tick(9000)&&!game.reveal(bomb,9000)&&!game.toggleFlag(bomb,9000)&&!game.pause(9000));
    game.restart(10000);check("win restarts preserving the legal high score",game.best==MaximumScore&&game.score==0&&game.state==State::Playing&&!game.generated);
}
void timeAndBounds(){
    Game game=fresh(100);game.reveal(25,1000);check("first reveal starts timer at zero",game.elapsedMs==0&&game.lastAt==1000);
    game.tick(1123);check("active timer uses elapsed milliseconds",game.elapsedMs==123);
    check("pause accounts final active milliseconds",game.pause(1150)&&game.state==State::Paused&&game.elapsedMs==150);
    check("paused refuses cell/mode actions",!game.reveal(0,9000)&&!game.toggleFlag(0,9000)&&!game.toggleMode()&&!game.tick(9000)&&game.elapsedMs==150);
    check("paused does not pause twice",!game.pause(9500));game.resume(10000);game.tick(10001);
    check("resume ignores paused wall time",game.elapsedMs==151&&game.state==State::Playing&&!game.resume(10002));
    game=fresh(0xFFFFFFF0u);game.reveal(25,0xFFFFFFF8u);game.tick(0x18u);
    check("active clock crosses unsigned millis rollover",game.elapsedMs==32);
    game.pause(0x20u);game.resume(0xFFFFFFF8u);game.tick(2);
    check("resume and its anchor may cross rollover",game.elapsedMs==50);
    game.elapsedMs=UINT32_MAX-5;game.tick(12);check("very long elapsed time saturates without wrap",game.elapsedMs==UINT32_MAX);
    Game board;
    check("board exact first and last pixels resolve",board.hitTest(BoardX,BoardY)==0&&board.hitTest(BoardX+279,BoardY+167)==59);
    check("board grid boundary selects correct cell",board.hitTest(BoardX+28,BoardY)==1&&board.hitTest(BoardX,BoardY+28)==10);
    check("all margins are rejected",board.hitTest(BoardX-1,BoardY)==-1&&board.hitTest(BoardX,BoardY-1)==-1&&board.hitTest(BoardX+280,BoardY)==-1&&board.hitTest(BoardX,BoardY+168)==-1);
}
}
int main(){lifecycle();generation();flagsAndFlood();endStates();timeAndBounds();std::printf("Mines engine: %d checks, %d failures\n",checks,failures);return failures?1:0;}
