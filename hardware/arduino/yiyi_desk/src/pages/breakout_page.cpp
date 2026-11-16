#include "mini_apps.h"
#include "breakout_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

namespace BreakoutPage {
namespace {
using namespace BreakoutEngine;
constexpr uint32_t Bg=0x142D3C,Header=0x1D3C4A,Ink=0xFFF4D8,Muted=0xA1BFC1;
constexpr uint32_t Blue=0x3B7E8A,Light=0xDCE8DF,White=0xFFFEF4;
const lv_font_t *F14=&fd_sans_bold_14_ui,*F16=&fd_sans_bold_16_ui;
enum Command{Back=1,Pause,Resume,Restart,Start,NextLevel};
Game game;
lv_obj_t *pageRoot=nullptr,*stage=nullptr,*overlay=nullptr,*scoreLabel=nullptr,*bestLabel=nullptr;
lv_timer_t *timer=nullptr;
bool prepared=false,loaded=false,blockUntilRelease=false,resumeRebuild=false,touching=false,gestureMoved=false;
uint32_t storedBest=0,shownScore=UINT32_MAX,shownBest=UINT32_MAX;
int overlayKind=-1;
lv_point_t pressPoint{};
const char *stateName(State state){
    switch(state){case State::Ready:return "ready";case State::Serving:return "serving";case State::Playing:return "playing";
        case State::Paused:return "paused";case State::LevelCleared:return "levelcleared";case State::Won:return "won";default:return "gameover";}
}
bool pointerDown(){for(auto *i=lv_indev_get_next(nullptr);i;i=lv_indev_get_next(i))if(i->driver->type==LV_INDEV_TYPE_POINTER&&i->proc.state==LV_INDEV_STATE_PR)return true;return false;}
void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius=8){
    auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,radius,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t *text(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font=nullptr,uint32_t color=Ink,lv_text_align_t align=LV_TEXT_ALIGN_LEFT){
    if(!font)font=F14;auto *o=lv_label_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,font->line_height+2);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_align(o,align,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);lv_label_set_text(o,value.c_str());lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t *button(lv_obj_t *parent,const char *value,int x,int y,int w,int h,int command,bool primary=false,bool dark=false){
    auto *o=box(parent,x,y,w,h,primary?Blue:dark?Header:Light);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,(void *)(intptr_t)command);Ui::attachPressFeedback(o);
    const auto *f=h<30?F14:F16;text(o,value,2,(h-f->line_height)/2,w-4,f,primary||dark?Ink:Bg,LV_TEXT_ALIGN_CENTER);return o;
}
void loadBest(){
    if(loaded)return;Preferences p;if(!p.begin("breakout",false))return;
    if(!p.isKey("best"))loaded=true;else{const uint32_t v=p.getUInt("best",UINT32_MAX);if(v!=UINT32_MAX){storedBest=v;loaded=true;}}
    p.end();if(loaded&&storedBest>game.best)game.best=storedBest;
}
void persistBest(){loadBest();if(!loaded||game.best<=storedBest)return;Preferences p;if(!p.begin("breakout",false))return;if(p.putUInt("best",game.best)==sizeof(uint32_t))storedBest=game.best;p.end();}
void rect(lv_draw_ctx_t *ctx,int x,int y,int w,int h,uint32_t color,int radius=0){
    if(w<=0||h<=0)return;lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=radius;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+h-1)};lv_draw_rect(ctx,&d,&a);
}
void drawText(lv_draw_ctx_t *ctx,const char *value,int x,int y,int w,uint32_t color=Muted){
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=F14;d.color=lv_color_hex(color);d.align=LV_TEXT_ALIGN_CENTER;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+F14->line_height+1)};lv_draw_label(ctx,&d,&a,value,nullptr);
}
void drawStage(lv_event_t *e){
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);const int x=a.x1,y=a.y1;
    for(int row=0;row<5;++row)for(int col=0;col<10;++col)rect(ctx,x+14+col*31,y+28+row*34,1,1,0x35515C);
    rect(ctx,x+3,y+22,2,186,0x39545A);rect(ctx,x+315,y+22,2,186,0x39545A);
    static const uint32_t colors[]={0xD58E79,0xD8B56B,0x8BA78B,0x76A9B5};
    for(int i=0;i<game.brickCount;++i){const auto &b=game.bricks[i];if(b.removed)continue;
        const uint32_t color=colors[b.type%4];rect(ctx,x+(int)b.x,y+(int)b.y,(int)b.w,(int)b.h,color,3);
        rect(ctx,x+(int)b.x+3,y+(int)b.y+2,(int)b.w-6,2,0xE8DCC1,1);
        if(b.hp>1){rect(ctx,x+(int)b.x+18,y+(int)b.y+6,3,3,Ink,1);rect(ctx,x+(int)b.x+24,y+(int)b.y+6,3,3,Ink,1);}
    }
    rect(ctx,x+(int)(game.paddleX-game.paddleWidth/2),y+(int)PaddleY,(int)game.paddleWidth,(int)PaddleHeight,0x82C4BE,4);
    rect(ctx,x+(int)(game.paddleX-game.paddleWidth/2)+5,y+(int)PaddleY+1,(int)game.paddleWidth-10,2,0xD1EFE0,1);
    rect(ctx,x+(int)game.ballX-4,y+(int)game.ballY-4,8,8,Ink,4);
    rect(ctx,x,y,320,20,Header);char info[32];snprintf(info,sizeof(info),"第 %u 关  余 %u",(unsigned)game.level,(unsigned)game.remaining);
    drawText(ctx,info,x+4,y+2,160);
    snprintf(info,sizeof(info),"生命 %u",(unsigned)game.lives);drawText(ctx,info,x+234,y+2,80);
    const State phase=game.state==State::Paused?game.pausedState:game.state;
    if(phase==State::Serving||phase==State::Ready)drawText(ctx,"拖动挡板，点击发球",x+30,y+144,260);
}
void drawIcon(lv_event_t *e){
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);rect(ctx,a.x1,a.y1,56,56,Header,14);
    static const uint32_t c[]={0xD58E79,0xD8B56B,0x82B4A6};
    for(int row=0;row<2;++row)for(int col=0;col<3;++col)rect(ctx,a.x1+8+col*14,a.y1+10+row*9,12,7,c[(row+col)%3],2);
    rect(ctx,a.x1+26,a.y1+32,6,6,Ink,3);rect(ctx,a.x1+15,a.y1+44,29,5,0x82C4BE,3);
}
void closeOverlay(){Ui::cancelAnimations();if(overlay)lv_obj_del(overlay);overlay=nullptr;overlayKind=-1;game.endDrag();touching=false;blockUntilRelease=pointerDown();}
void openOverlay(int kind){
    closeOverlay();overlay=box(pageRoot,0,0,320,240,0x071924,0);lv_obj_set_style_bg_opa(overlay,145,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
    auto *card=box(overlay,28,36,264,168,White,14);
    if(kind==(int)State::Ready){
        text(card,"打砖块",12,8,240,F16,Bg,LV_TEXT_ALIGN_CENTER);button(card,"返回",204,4,48,28,Back);
        text(card,"拖动挡板，点击发球",8,34,248,F14,Bg,LV_TEXT_ALIGN_CENTER);
        text(card,"接住小球，打碎全部砖块",8,54,248,F14,Blue,LV_TEXT_ALIGN_CENTER);
        text(card,"加固砖块需要多次击中",8,74,248,F14,Blue,LV_TEXT_ALIGN_CENTER);
        text(card,"三次机会，挑战三个关卡",8,96,248,F14,Blue,LV_TEXT_ALIGN_CENTER);
        button(card,"开始游戏",16,128,232,32,Start,true);
    }else{
        const bool paused=kind==(int)State::Paused,clear=kind==(int)State::LevelCleared,won=kind==(int)State::Won;
        text(card,paused?"已暂停":clear?"本关通过":won?"全部通关":"游戏结束",12,16,240,F16,Bg,LV_TEXT_ALIGN_CENTER);
        text(card,String("得分 ")+String((unsigned long)game.score)+"   最高 "+String((unsigned long)game.best),8,48,248,F16,Bg,LV_TEXT_ALIGN_CENTER);
        text(card,clear?"准备进入下一关":won?"所有砖块已清除":"继续守住挡板",8,78,248,F14,Blue,LV_TEXT_ALIGN_CENTER);
        if(paused){button(card,"继续",12,112,64,40,Resume,true);button(card,"重新开始",80,112,100,40,Restart);button(card,"返回",184,112,68,40,Back);}
        else{button(card,clear?"下一关":"再来一局",16,112,136,40,clear?NextLevel:Restart,true);button(card,"返回",160,112,88,40,Back);}
    }
    overlayKind=kind;
}
bool modal(){return !game.active();}
void apply(){
    if(!pageRoot)return;
    if(shownScore!=game.score){lv_label_set_text_fmt(scoreLabel,"分 %lu",(unsigned long)game.score);shownScore=game.score;}
    if(shownBest!=game.best){lv_label_set_text_fmt(bestLabel,"高 %lu",(unsigned long)game.best);shownBest=game.best;}
    if(modal()){if(overlayKind!=(int)game.state)openOverlay((int)game.state);}else if(overlay)closeOverlay();
    if(stage)lv_obj_invalidate(stage);
}
void event(lv_event_t *e){
    const int command=(int)(intptr_t)lv_event_get_user_data(e);lastInputAt=millis();
    switch(command){case Back:{game.pause(millis());persistBest();lv_event_t forwarded=*e;forwarded.user_data=(void *)(uintptr_t)Ui::Back;Ui::dispatchAction(&forwarded);return;}
        case Pause:game.pause(millis());persistBest();break;case Resume:game.resume(millis());break;
        case Start:game.start(millis(),millis()*2654435761u+0x9E3779B9u);break;case Restart:persistBest();game.restart(millis());break;
        case NextLevel:game.nextLevel(millis());break;default:return;}
    game.endDrag();touching=false;blockUntilRelease=pointerDown();apply();
}
void touchStage(lv_event_t *e){
    const auto code=lv_event_get_code(e);if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_RELEASED&&code!=LV_EVENT_PRESS_LOST)return;
    auto *input=lv_indev_get_act();if(!input)return;lv_point_t p;lv_indev_get_point(input,&p);lv_area_t a;lv_obj_get_coords(stage,&a);
    if(code==LV_EVENT_PRESSED){if(!blockUntilRelease&&game.beginDrag(p.x-a.x1,p.y-a.y1)){pressPoint=p;touching=true;gestureMoved=false;}}
    else if(code==LV_EVENT_PRESSING){if(touching&&(abs(p.x-pressPoint.x)>10||abs(p.y-pressPoint.y)>10))gestureMoved=true;if(!blockUntilRelease&&game.drag(p.x-a.x1,p.y-a.y1)){lastInputAt=millis();apply();}}
    else{
        const bool inside=p.x>=a.x1&&p.x<=a.x2&&p.y>=a.y1&&p.y<=a.y2;
        const bool launch=code==LV_EVENT_RELEASED&&touching&&!gestureMoved&&!blockUntilRelease&&inside&&abs(p.x-pressPoint.x)<=10&&abs(p.y-pressPoint.y)<=10;
        if(code==LV_EVENT_RELEASED&&!blockUntilRelease)game.drag(p.x-a.x1,p.y-a.y1);
        game.endDrag();touching=false;if(code==LV_EVENT_RELEASED)blockUntilRelease=false;
        if(launch){game.launch(millis());lastInputAt=millis();}if(modal())persistBest();apply();
    }
}
void onTick(lv_timer_t *){if(!pageRoot)return;if(blockUntilRelease&&!pointerDown())blockUntilRelease=false;if(game.tick(millis())){if(modal())persistBest();apply();}}
} // namespace
void createIcon(lv_obj_t *parent,int x,int y){auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,56,56);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(o,drawIcon,LV_EVENT_DRAW_MAIN,nullptr);}
void build(lv_obj_t *parent){
    loadBest();if(!prepared){game.prepare(millis()*2654435761u+0x9E3779B9u);prepared=true;}if(resumeRebuild){game.resume(millis());resumeRebuild=false;}
    pageRoot=parent;stage=overlay=scoreLabel=bestLabel=nullptr;overlayKind=-1;shownScore=shownBest=UINT32_MAX;touching=false;blockUntilRelease=pointerDown();game.endDrag();
    lv_obj_set_style_bg_color(parent,lv_color_hex(Header),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);lv_obj_clear_flag(parent,LV_OBJ_FLAG_SCROLLABLE);
    button(parent,"返回",0,0,40,32,Back,false,true);text(parent,"打砖块",45,6,90,F16);scoreLabel=text(parent,"",140,8,54,F14);bestLabel=text(parent,"",200,8,56,F14,Muted);button(parent,"暂停",260,0,60,32,Pause,false,true);
    stage=box(parent,0,32,320,208,Bg,0);lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);lv_obj_clear_flag(stage,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(stage,drawStage,LV_EVENT_DRAW_MAIN,nullptr);lv_obj_add_event_cb(stage,touchStage,LV_EVENT_ALL,nullptr);
    if(timer)lv_timer_del(timer);timer=lv_timer_create(onTick,20,nullptr);apply();
}
void refresh(bool force){if(force)apply();}
void cancel(){
    if(timer){lv_timer_del(timer);timer=nullptr;}if(pageRoot){game.endDrag();const bool pausedNow=game.pause(millis());resumeRebuild=app==App::Breakout&&pausedNow;persistBest();}else if(app!=App::Breakout)resumeRebuild=false;
    pageRoot=stage=overlay=scoreLabel=bestLabel=nullptr;overlayKind=-1;touching=false;blockUntilRelease=pointerDown();
}
bool back(){return false;}bool canBack(){return false;}bool inputActive(){return touching||game.dragging||blockUntilRelease;}
Status status(){return {stateName(game.state),game.score,game.best,game.level,game.lives,game.remaining};}
uint32_t bestScore(){loadBest();return game.best>storedBest?game.best:storedBest;}
#ifdef GIFT_HOST_TEST
const BreakoutEngine::Game &debugGame(){return game;}
void debugVariant(int which){
    const auto best=game.best;game=Game();game.best=best;game.prepare(20261113u);prepared=true;resumeRebuild=false;loadBest();
    if(which){game.start(millis(),20261113u);
        if(which==2){game.launch(millis());game.pause(millis());}
        else if(which==3){game.lives=0;game.state=State::GameOver;}
        else if(which==4||which==5){for(auto &b:game.bricks)b.removed=true;game.remaining=0;game.score=480;game.best=game.best>game.score?game.best:game.score;game.state=which==4?State::LevelCleared:State::Won;if(which==5)game.level=3;}
        else if(which==6){game.launch(millis());game.ballX=175;game.ballY=128;}
        else if(which==7){game.launch(millis());game.lives=1;game.ballX=10;game.ballY=205;game.ballVX=0;game.ballVY=180;}
    }
    game.endDrag();touching=false;blockUntilRelease=pointerDown();apply();
}
#endif
} // namespace BreakoutPage
