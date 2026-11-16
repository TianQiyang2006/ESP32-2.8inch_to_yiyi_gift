#include "mini_apps.h"
#include "pin_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

namespace PinPage {
namespace {
using namespace PinEngine;
constexpr uint32_t Bg=0xF4F1E8, Ink=0x423B53, Muted=0x8D8290;
constexpr uint32_t Green=0x776184, Light=0xE7DFE8, White=0xFFFEF7;
const lv_font_t *F14=&fd_sans_bold_14_ui,*F16=&fd_sans_bold_16_ui;
enum Command {Back=1,Pause,Resume,Restart,Start,NextLevel};
Game game;
lv_obj_t *pageRoot=nullptr,*stage=nullptr,*overlay=nullptr,*scoreLabel=nullptr,*bestLabel=nullptr;
lv_timer_t *timer=nullptr;
bool prepared=false,loaded=false,blockUntilRelease=false,touching=false,resumeRebuild=false;
uint32_t storedBest=0,shownScore=UINT32_MAX,shownBest=UINT32_MAX;
int overlayKind=-1;
lv_point_t pressPoint{};

const char *stateName(State state) {
    switch(state) {
        case State::Ready:return "ready";case State::Playing:return "playing";
        case State::Flying:return "flying";
        case State::Paused:return "paused";case State::LevelCleared:return "levelcleared";
        case State::Won:return "won";default:return "gameover";
    }
}
bool pointerDown() {
    for(auto *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))
        if(input->driver->type==LV_INDEV_TYPE_POINTER&&input->proc.state==LV_INDEV_STATE_PR)return true;
    return false;
}
void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius=8) {
    auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,radius,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t *text(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font=nullptr,
               uint32_t color=Ink,lv_text_align_t align=LV_TEXT_ALIGN_LEFT) {
    if(!font)font=F14;
    auto *o=lv_label_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,font->line_height+2);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_align(o,align,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);lv_label_set_text(o,value.c_str());lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t *button(lv_obj_t *parent,const char *value,int x,int y,int w,int h,int command,bool primary=false) {
    auto *o=box(parent,x,y,w,h,primary?Green:Light);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,(void *)(intptr_t)command);Ui::attachPressFeedback(o);
    const auto *font=h<30?F14:F16;text(o,value,2,(h-font->line_height)/2,w-4,font,primary?White:Ink,LV_TEXT_ALIGN_CENTER);return o;
}
void loadBest() {
    if(loaded)return;
    Preferences prefs;if(!prefs.begin("pin",false))return;
    if(!prefs.isKey("best"))loaded=true;
    else {const uint32_t value=prefs.getUInt("best",UINT32_MAX);if(value!=UINT32_MAX){storedBest=value;loaded=true;}}
    prefs.end();if(loaded&&storedBest>game.best)game.best=storedBest;
}
void persistBest() {
    loadBest();if(!loaded||game.best<=storedBest)return;
    Preferences prefs;if(!prefs.begin("pin",false))return;
    if(prefs.putUInt("best",game.best)==sizeof(uint32_t))storedBest=game.best;
    prefs.end();
}
void rect(lv_draw_ctx_t *ctx,int x,int y,int w,int h,uint32_t color,int radius=0) {
    if(w<=0||h<=0)return;
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=radius;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+h-1)};lv_draw_rect(ctx,&d,&a);
}
void line(lv_draw_ctx_t *ctx,int x1,int y1,int x2,int y2,uint32_t color,int width=2) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=lv_color_hex(color);d.width=width;d.round_start=d.round_end=true;
    lv_point_t p={(lv_coord_t)x1,(lv_coord_t)y1},q={(lv_coord_t)x2,(lv_coord_t)y2};lv_draw_line(ctx,&d,&p,&q);
}
void circle(lv_draw_ctx_t *ctx,int x,int y,int radius,uint32_t color) {
    rect(ctx,x-radius,y-radius,2*radius+1,2*radius+1,color,radius);
}
void drawText(lv_draw_ctx_t *ctx,const char *value,int x,int y,int w,uint32_t color=Muted,
              const lv_font_t *font=nullptr) {
    if(!font)font=F14;
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=font;d.color=lv_color_hex(color);d.align=LV_TEXT_ALIGN_CENTER;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+font->line_height+1)};
    lv_draw_label(ctx,&d,&a,value,nullptr);
}
void drawPin(lv_draw_ctx_t *ctx,int x,int y,float angle,uint32_t color) {
    const float direction=radians(angle),dx=sinf(direction),dy=cosf(direction);
    const int ax=x+(int)lroundf(dx*HubRadius),ay=y+(int)lroundf(dy*HubRadius);
    const int bx=x+(int)lroundf(dx*OuterRadius),by=y+(int)lroundf(dy*OuterRadius);
    line(ctx,ax,ay,bx,by,color,2);circle(ctx,bx,by,(int)HeadRadius,color);
}
void drawStage(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    const int ox=a.x1,oy=a.y1,cx=ox+(int)CenterX,cy=oy+(int)CenterY;
    char info[32];snprintf(info,sizeof(info),"第%u关",(unsigned)game.level);
    drawText(ctx,info,ox+8,oy+3,62,Ink);
    snprintf(info,sizeof(info),"余 %u 针",(unsigned)game.remaining);
    drawText(ctx,info,ox+243,oy+3,70,Ink);
    for(int i=0;i<game.attached;++i)drawPin(ctx,cx,cy,game.worldAngle(i),i<3?0x8C839A:0xA18782);
    circle(ctx,cx,cy,(int)HubRadius,Green);circle(ctx,cx-8,cy-9,4,0x9C88A7);
    snprintf(info,sizeof(info),"%u",(unsigned)game.remaining);
    drawText(ctx,info,cx-24,cy-10,48,White,F16);
    const State phase=game.state==State::Paused?game.pausedState:game.state;
    const bool flying=phase==State::Flying||phase==State::GameOver;
    const int tip=oy+(int)lroundf(flying?game.flightY:LaunchTipY),head=tip+(int)PinLength;
    const uint32_t color=phase==State::GameOver?0xB76A67:0xA8756D;
    if(game.remaining||phase==State::GameOver) {
        line(ctx,cx,tip,cx,head,color,2);circle(ctx,cx,head,(int)HeadRadius,color);
    }
    drawText(ctx,phase==State::Flying?"正在发针":"点击发针",ox+10,oy+180,110,Muted);
    snprintf(info,sizeof(info),"%u / 3",(unsigned)game.level);
    drawText(ctx,info,ox+222,oy+180,84,Muted);
}
void drawIcon(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    rect(ctx,a.x1,a.y1,56,56,Light,14);const int x=a.x1+28,y=a.y1+24;
    for(int i=0;i<5;++i) {
        const float angle=radians(32.0f+i*72.0f);const float dx=sinf(angle),dy=cosf(angle);
        const int bx=x+(int)(dx*20),by=y+(int)(dy*20);
        line(ctx,x+(int)(dx*10),y+(int)(dy*10),bx,by,Green,2);circle(ctx,bx,by,3,Green);
    }
    circle(ctx,x,y,11,Green);line(ctx,x,a.y1+43,x,a.y1+51,0xA8756D,2);circle(ctx,x,a.y1+51,3,0xA8756D);
}
void closeOverlay() {
    Ui::cancelAnimations();if(overlay)lv_obj_del(overlay);overlay=nullptr;overlayKind=-1;
    touching=false;blockUntilRelease=pointerDown();
}
void openOverlay(int kind) {
    closeOverlay();overlay=box(pageRoot,0,0,320,240,Ink,0);lv_obj_set_style_bg_opa(overlay,140,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
    auto *card=box(overlay,28,36,264,168,White,14);
    if(kind==(int)State::Ready) {
        text(card,"见缝插针",12,8,240,F16,Green,LV_TEXT_ALIGN_CENTER);button(card,"返回",204,4,48,28,Back);
        text(card,"点击屏幕向圆盘发针",8,34,248,F14,Ink,LV_TEXT_ALIGN_CENTER);
        text(card,"避开圆盘上的已有针",8,54,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"碰到已有针则本局结束",8,74,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"三关全部发完即可通关",8,96,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        button(card,"开始游戏",16,128,232,32,Start,true);
    } else {
        const bool paused=kind==(int)State::Paused,clear=kind==(int)State::LevelCleared,won=kind==(int)State::Won;
        text(card,paused?"已暂停":clear?"本关通过":won?"全部通关":"撞到针了",12,16,240,F16,Green,LV_TEXT_ALIGN_CENTER);
        text(card,String("得分 ")+String((unsigned long)game.score)+"   最高 "+String((unsigned long)game.best),8,48,248,F16,Ink,LV_TEXT_ALIGN_CENTER);
        text(card,clear?"准备挑战下一关":won?"所有针已发完":paused?"继续寻找空位":"再试一次，找准空位",8,78,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        if(paused) {
            button(card,"继续",12,112,64,40,Resume,true);button(card,"重新开始",80,112,100,40,Restart);button(card,"返回",184,112,68,40,Back);
        } else {
            button(card,clear?"下一关":"再来一局",16,112,136,40,clear?NextLevel:Restart,true);button(card,"返回",160,112,88,40,Back);
        }
    }
    overlayKind=kind;
}
bool modal(){return game.state==State::Ready||game.state==State::Paused||game.state==State::LevelCleared||game.state==State::Won||game.state==State::GameOver;}
void apply() {
    if(!pageRoot)return;
    if(shownScore!=game.score){lv_label_set_text_fmt(scoreLabel,"分 %lu",(unsigned long)game.score);shownScore=game.score;}
    if(shownBest!=game.best){lv_label_set_text_fmt(bestLabel,"高 %lu",(unsigned long)game.best);shownBest=game.best;}
    if(modal()){if(overlayKind!=(int)game.state)openOverlay((int)game.state);}else if(overlay)closeOverlay();
    if(stage)lv_obj_invalidate(stage);
}
void event(lv_event_t *e) {
    const int command=(int)(intptr_t)lv_event_get_user_data(e);lastInputAt=millis();
    switch(command) {
        case Back:{game.pause(millis());persistBest();lv_event_t forwarded=*e;forwarded.user_data=(void *)(uintptr_t)Ui::Back;Ui::dispatchAction(&forwarded);return;}
        case Pause:game.pause(millis());persistBest();break;case Resume:game.resume(millis());break;
        case Restart:persistBest();game.restart(millis());break;case Start:game.start(millis(),millis()*2654435761u+0x9E3779B9u);break;
        case NextLevel:game.nextLevel(millis());break;default:return;
    }
    touching=false;blockUntilRelease=pointerDown();apply();
}
void touchStage(lv_event_t *e) {
    const auto code=lv_event_get_code(e);if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_RELEASED&&code!=LV_EVENT_PRESS_LOST)return;
    auto *input=lv_indev_get_act();if(!input)return;
    lv_point_t p;lv_indev_get_point(input,&p);lv_area_t area;lv_obj_get_coords(stage,&area);
    if(code==LV_EVENT_PRESSED) {
        if(blockUntilRelease||game.state!=State::Playing)return;pressPoint=p;touching=true;
    } else if(code==LV_EVENT_PRESSING) {
        if(touching&&(abs(p.x-pressPoint.x)>10||abs(p.y-pressPoint.y)>10))touching=false;
    } else if(code==LV_EVENT_RELEASED) {
        const bool inside=p.x>=area.x1&&p.x<=area.x2&&p.y>=area.y1&&p.y<=area.y2;
        const bool cast=touching&&!blockUntilRelease&&inside&&abs(p.x-pressPoint.x)<=10&&abs(p.y-pressPoint.y)<=10;
        touching=false;blockUntilRelease=false;
        if(cast){game.shoot(millis());lastInputAt=millis();if(modal())persistBest();apply();}
    } else touching=false;
}
void onTick(lv_timer_t *) {
    if(!pageRoot)return;
    if(blockUntilRelease&&!pointerDown())blockUntilRelease=false;
    if(game.tick(millis())){if(modal())persistBest();apply();}
}
} // namespace

void createIcon(lv_obj_t *parent,int x,int y) {
    auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,56,56);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(o,drawIcon,LV_EVENT_DRAW_MAIN,nullptr);
}
void build(lv_obj_t *parent) {
    loadBest();if(!prepared){game.prepare(millis()*2654435761u+0x9E3779B9u);prepared=true;}
    if(resumeRebuild){game.resume(millis());resumeRebuild=false;}
    pageRoot=parent;stage=overlay=scoreLabel=bestLabel=nullptr;overlayKind=-1;
    shownScore=shownBest=UINT32_MAX;touching=false;blockUntilRelease=pointerDown();
    lv_obj_set_style_bg_color(parent,lv_color_hex(Bg),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);lv_obj_clear_flag(parent,LV_OBJ_FLAG_SCROLLABLE);
    button(parent,"返回",0,0,40,32,Back);text(parent,"见缝插针",45,6,90,F16);
    scoreLabel=text(parent,"",140,8,54,F14,Green);bestLabel=text(parent,"",200,8,56,F14,Muted);button(parent,"暂停",260,0,60,32,Pause);
    stage=box(parent,0,32,320,208,Bg,0);lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);lv_obj_clear_flag(stage,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(stage,drawStage,LV_EVENT_DRAW_MAIN,nullptr);lv_obj_add_event_cb(stage,touchStage,LV_EVENT_ALL,nullptr);
    if(timer)lv_timer_del(timer);timer=lv_timer_create(onTick,20,nullptr);apply();
}
void refresh(bool force){if(force)apply();}
void cancel() {
    if(timer){lv_timer_del(timer);timer=nullptr;}
    if(pageRoot){const bool pausedNow=game.pause(millis());resumeRebuild=app==App::Pin&&pausedNow;persistBest();}
    else if(app!=App::Pin)resumeRebuild=false;
    pageRoot=stage=overlay=scoreLabel=bestLabel=nullptr;overlayKind=-1;touching=false;blockUntilRelease=pointerDown();
}
bool back(){return false;}bool canBack(){return false;}bool inputActive(){return touching||blockUntilRelease;}
Status status(){return {stateName(game.state),game.score,game.best,game.level,game.remaining,game.attached};}
uint32_t bestScore(){loadBest();return game.best>storedBest?game.best:storedBest;}
#ifdef GIFT_HOST_TEST
const PinEngine::Game &debugGame(){return game;}
void debugVariant(int which) {
    const auto best=game.best;game=Game{};game.best=best;game.prepare(20261113u);prepared=true;resumeRebuild=false;loadBest();
    if(which) {
        game.start(millis(),20261113u);
        if(which==2)game.pause(millis());
        else if(which==3){game.flightY=ContactTipY;game.state=State::GameOver;}
        else if(which==4){game.score=6;game.remaining=0;game.state=State::LevelCleared;}
        else if(which==5){game.level=3;game.score=24;game.remaining=0;game.state=State::Won;}
        else if(which==6){game.shoot(millis());game.tick(millis()+100);game.lastAt=millis();}
        else if(which==7){game.state=State::LevelCleared;game.nextLevel(millis());game.state=State::LevelCleared;game.nextLevel(millis());}
        if(game.score>game.best)game.best=game.score;
    }
    touching=false;blockUntilRelease=pointerDown();apply();
}
#endif
} // namespace PinPage
