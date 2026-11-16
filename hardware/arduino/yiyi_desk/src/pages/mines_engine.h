#pragma once
#include <stdint.h>

namespace MinesEngine {
constexpr int Cols=10, Rows=6, Cells=Cols*Rows, MineCount=12;
constexpr int CellSize=28, BoardX=20, BoardY=32;
constexpr uint32_t MaximumScore=(Cells-MineCount)*10+200;
enum class State : uint8_t { Ready, Playing, Paused, Won, GameOver };
struct Cell {
    bool mine=false, revealed=false, flagged=false;
    uint8_t adjacent=0;
};
struct Game {
    Cell cells[Cells];
    State state=State::Ready;
    uint32_t score=0,best=0,elapsedMs=0,lastAt=0,randomState=1;
    uint8_t remaining=Cells-MineCount,flags=0;
    bool generated=false,flagMode=false;
    int8_t exploded=-1;

    uint32_t random(){randomState=randomState*1664525u+1013904223u;return randomState;}
    void prepare(uint32_t seed){
        const uint32_t record=best<=MaximumScore?best:0;
        *this=Game();best=record;randomState=seed?seed:1;
    }
    void start(uint32_t now,uint32_t seed){prepare(seed);lastAt=now;state=State::Playing;}
    void restart(uint32_t now){start(now,randomState^0x9E3779B9u);}
    bool tick(uint32_t now){
        if(state!=State::Playing)return false;
        const uint32_t delta=now-lastAt;lastAt=now;
        if(!generated||!delta)return false;
        // Saturating elapsed time remains meaningful over millis rollover and
        // cannot wrap a long session back to a short displayed duration.
        elapsedMs=UINT32_MAX-elapsedMs<delta?UINT32_MAX:elapsedMs+delta;
        return true;
    }
    bool pause(uint32_t now){
        if(state!=State::Playing)return false;
        tick(now);state=State::Paused;return true;
    }
    bool resume(uint32_t now){if(state!=State::Paused)return false;lastAt=now;state=State::Playing;return true;}
    bool toggleMode(){if(state!=State::Playing)return false;flagMode=!flagMode;return true;}
    int hitTest(int x,int y)const{
        x-=BoardX;y-=BoardY;
        if(x<0||y<0||x>=Cols*CellSize||y>=Rows*CellSize)return -1;
        return y/CellSize*Cols+x/CellSize;
    }
    bool toggleFlag(int id,uint32_t now){
        if(state!=State::Playing||id<0||id>=Cells||cells[id].revealed)return false;
        tick(now);
        if(cells[id].flagged){cells[id].flagged=false;--flags;}
        else {if(flags>=MineCount)return false;cells[id].flagged=true;++flags;}
        return true;
    }
    bool reveal(int id,uint32_t now){
        if(state!=State::Playing||id<0||id>=Cells||cells[id].flagged||cells[id].revealed)return false;
        tick(now);
        if(!generated){generate(id);lastAt=now;}
        if(cells[id].mine){cells[id].revealed=true;exploded=(int8_t)id;state=State::GameOver;return true;}
        uint8_t queue[Cells];unsigned head=0,tail=0;
        cells[id].revealed=true;queue[tail++]=(uint8_t)id;
        while(head<tail){
            const int current=queue[head++];--remaining;score+=10;
            if(cells[current].adjacent)continue;
            const int cx=current%Cols,cy=current/Cols;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                const int x=cx+dx,y=cy+dy;
                if(x<0||x>=Cols||y<0||y>=Rows)continue;
                Cell &cell=cells[y*Cols+x];
                if(cell.mine||cell.revealed||cell.flagged)continue;
                cell.revealed=true;queue[tail++]=(uint8_t)(y*Cols+x);
            }
        }
        if(!remaining){
            state=State::Won;score+=200;flags=MineCount;
            for(auto &cell:cells)if(cell.mine)cell.flagged=true;
        }
        if(score>best)best=score;
        return true;
    }
private:
    void generate(int safe){
        uint8_t candidates[Cells];unsigned count=0;
        const int sx=safe%Cols,sy=safe/Cols;
        for(int id=0;id<Cells;++id){
            const int dx=id%Cols-sx,dy=id/Cols-sy;
            if(dx>=-1&&dx<=1&&dy>=-1&&dy<=1)continue;
            candidates[count++]=(uint8_t)id;
        }
        for(int i=0;i<MineCount;++i){
            const unsigned chosen=i+(random()>>8)%(count-i);
            const uint8_t swap=candidates[i];candidates[i]=candidates[chosen];candidates[chosen]=swap;
            cells[candidates[i]].mine=true;
        }
        for(int id=0;id<Cells;++id){
            const int cx=id%Cols,cy=id/Cols;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                const int x=cx+dx,y=cy+dy;
                if((dx||dy)&&x>=0&&x<Cols&&y>=0&&y<Rows&&cells[y*Cols+x].mine)++cells[id].adjacent;
            }
        }
        generated=true;
    }
};
} // namespace MinesEngine
