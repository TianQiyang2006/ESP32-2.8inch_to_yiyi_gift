#pragma once
#include <stdint.h>
#include <math.h>

namespace BreakoutEngine {
constexpr int Width=320,Height=208,MaxBricks=24;
constexpr float BallRadius=4.0f,PaddleY=187.0f,PaddleHeight=7.0f;
constexpr uint16_t StepMs=5;
enum class State {Ready,Serving,Playing,Paused,LevelCleared,Won,GameOver};
struct Brick {float x,y,w,h;uint8_t hp,type;bool removed;};
inline float clamp(float value,float low,float high){return value<low?low:value>high?high:value;}
struct Game {
    Brick bricks[MaxBricks]={};
    uint8_t brickCount=0,level=1,lives=3;
    uint16_t remaining=0,stepRemainder=0;
    uint32_t score=0,best=0,lastAt=0,elapsedMs=0,randomState=1;
    float ballX=160,ballY=PaddleY-BallRadius-1,ballVX=65,ballVY=-160,paddleX=160,paddleWidth=64;
    bool dragging=false;
    float dragOriginX=0,paddleOriginX=160;
    State state=State::Ready,pausedState=State::Serving;

    bool active()const{return state==State::Serving||state==State::Playing;}
    void prepare(uint32_t seed){
        const auto record=best;*this=Game();best=record;randomState=seed;deal();state=State::Ready;
    }
    void start(uint32_t now,uint32_t seed){prepare(seed);state=State::Serving;lastAt=now;}
    void restart(uint32_t now){start(now,randomState^0x9E3779B9u);}
    bool nextLevel(uint32_t now){
        if(state!=State::LevelCleared||level>=3)return false;
        ++level;deal();state=State::Serving;lastAt=now;return true;
    }
    bool launch(uint32_t now){
        tick(now);if(state!=State::Serving)return false;
        ballX=paddleX;ballY=PaddleY-BallRadius-1;ballVX=65.0f+level*5;ballVY=-155.0f-level*5;
        state=State::Playing;return true;
    }
    bool movePaddle(float x){
        if(!active())return false;
        const float next=clamp(x,paddleWidth/2+4,Width-paddleWidth/2-4);
        const bool changed=next!=paddleX;paddleX=next;
        if(state==State::Serving)ballX=paddleX;
        return changed;
    }
    bool beginDrag(int x,int){
        if(!active()||dragging)return false;
        dragging=true;dragOriginX=(float)x;paddleOriginX=paddleX;return true;
    }
    bool drag(int x,int){return dragging&&movePaddle(paddleOriginX+x-dragOriginX);}
    bool endDrag(){const bool was=dragging;dragging=false;return was;}
    bool pause(uint32_t now){
        tick(now);if(!active())return false;
        pausedState=state;state=State::Paused;endDrag();return true;
    }
    bool resume(uint32_t now){if(state!=State::Paused)return false;state=pausedState;lastAt=now;endDrag();return true;}
    bool tick(uint32_t now){
        if(!active())return false;
        uint32_t delta=now-lastAt;lastAt=now;
        // Bound unseen catch-up while preserving normal 5ms physics and wrap.
        if(delta>1000)delta=1000;
        delta+=stepRemainder;stepRemainder=(uint16_t)(delta%StepMs);
        bool changed=false;
        while(delta>=StepMs&&active()){
            elapsedMs+=StepMs;if(state==State::Playing)step();
            delta-=StepMs;changed=true;
        }
        return changed;
    }

private:
    void deal(){
        brickCount=remaining=MaxBricks;paddleWidth=64;paddleX=160;stepRemainder=0;dragging=false;
        ballX=paddleX;ballY=PaddleY-BallRadius-1;ballVX=65;ballVY=-160;
        for(int row=0;row<4;++row)for(int col=0;col<6;++col){
            const int id=row*6+col;
            bricks[id]={(float)(18+col*48),(float)(33+row*18),44,13,
                (uint8_t)(level==1?1:level==2?(row<2?2:1):((row+col)%2?1:2)),(uint8_t)row,false};
        }
    }
    // Point against an expanded brick, returning earliest contact and its normal.
    static bool sweep(float x,float y,float dx,float dy,float left,float top,float right,float bottom,float &time,int &nx,int &ny){
        float enter=-100000,leave=100000;int ex=0,ey=0;
        const float p[2]={x,y},d[2]={dx,dy},low[2]={left,top},high[2]={right,bottom};
        for(int axis=0;axis<2;++axis){
            if(fabsf(d[axis])<0.00001f){if(p[axis]<low[axis]||p[axis]>high[axis])return false;continue;}
            float a=(low[axis]-p[axis])/d[axis],b=(high[axis]-p[axis])/d[axis];int normal=-1;
            if(a>b){const float swap=a;a=b;b=swap;normal=1;}
            if(a>enter){enter=a;ex=axis==0?normal:0;ey=axis==1?normal:0;}
            if(b<leave)leave=b;if(enter>leave)return false;
        }
        // A negative entry means already departing a surface just hit. Reject it
        // rather than counting the same brick repeatedly while leaving its box.
        if(leave<0||enter<-0.0001f||enter>1)return false;
        time=enter<0?0:enter;nx=ex;ny=ey;return true;
    }
    void loseBall(){
        if(lives)--lives;endDrag();stepRemainder=0;
        if(!lives){state=State::GameOver;return;}
        state=State::Serving;ballX=paddleX;ballY=PaddleY-BallRadius-1;
    }
    void step(){
        float seconds=.005f;
        for(int contacts=0;contacts<6&&seconds>0&&state==State::Playing;++contacts){
            const float dx=ballVX*seconds,dy=ballVY*seconds;
            float first=1.00001f;int nx=0,ny=0,brick=-1;bool paddle=false;
            const auto wall=[&](float t,int x,int y){if(t>=0&&t<=1&&t<first){first=t;nx=x;ny=y;brick=-1;paddle=false;}};
            if(dx<0)wall((4+BallRadius-ballX)/dx,1,0);
            if(dx>0)wall((316-BallRadius-ballX)/dx,-1,0);
            if(dy<0)wall((22+BallRadius-ballY)/dy,0,1);
            if(dy>0){
                const float t=(PaddleY-BallRadius-ballY)/dy;
                const float x=ballX+dx*t;
                if(t>=0&&t<=1&&x>=paddleX-paddleWidth/2-BallRadius&&x<=paddleX+paddleWidth/2+BallRadius&&t<first){
                    first=t;nx=0;ny=-1;brick=-1;paddle=true;
                }
            }
            for(int i=0;i<brickCount;++i){
                const auto &b=bricks[i];if(b.removed)continue;
                float t;int x,y;
                if(sweep(ballX,ballY,dx,dy,b.x-BallRadius,b.y-BallRadius,b.x+b.w+BallRadius,b.y+b.h+BallRadius,t,x,y)&&t<first){
                    first=t;nx=x;ny=y;brick=i;paddle=false;
                }
            }
            if(first>1){ballX+=dx;ballY+=dy;seconds=0;}
            else{
                ballX+=dx*first;ballY+=dy*first;seconds*=1-first;
                if(paddle){
                    const float offset=clamp((ballX-paddleX)/(paddleWidth/2),-1,1);
                    const float speed=170+level*12;
                    const float incomingVX=ballVX;
                    ballVX=offset*speed*.82f;
                    // A central bounce still travels sideways enough to avoid
                    // repeating one vertical lane for the rest of the stage.
                    if(fabsf(ballVX)<25)ballVX=fabsf(offset)<.01f?(incomingVX<0?-25:25):(ballVX<0?-25:25);
                    ballVY=-sqrtf(speed*speed-ballVX*ballVX);
                }else{if(nx)ballVX=-ballVX;if(ny)ballVY=-ballVY;}
                ballX+=nx*.02f;ballY+=ny*.02f;
                if(brick>=0){
                    auto &b=bricks[brick];score+=10;
                    if(--b.hp==0){b.removed=true;--remaining;score+=10;}
                    if(score>best)best=score;
                    if(!remaining){state=level==3?State::Won:State::LevelCleared;endDrag();return;}
                }
            }
        }
        if(ballY-BallRadius>Height)loseBall();
    }
};
} // namespace BreakoutEngine
