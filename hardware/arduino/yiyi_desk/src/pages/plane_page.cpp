#include "mini_apps.h"
#include "plane_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

namespace PlanePage {
namespace {
using namespace PlaneEngine;
constexpr uint32_t Bg=0x102B40, Header=0x173B50, Ink=0xFFF1CD, Muted=0xA6C5D0;
constexpr uint32_t Blue=0x3F849E, Light=0xDCE9E9, White=0xFFFDF3, Red=0xFF7773;
const lv_font_t *F14=&fd_sans_bold_14_ui, *F16=&fd_sans_bold_16_ui;
enum Command { Back=1, Pause, Resume, Restart, Start };
Game game;
lv_obj_t *pageRoot=nullptr, *stage=nullptr, *overlay=nullptr;
lv_obj_t *scoreLabel=nullptr, *bestLabel=nullptr;
lv_timer_t *timer=nullptr;
bool prepared=false, loaded=false, blockUntilRelease=false, resumeRebuild=false;
uint32_t storedBest=0, shownScore=UINT32_MAX, shownBest=UINT32_MAX;
int overlayKind=-1;

const char *stateName(State state) {
    switch(state) {
        case State::Ready:return "ready";case State::Playing:return "playing";
        case State::Paused:return "paused";case State::Won:return "won";default:return "gameover";
    }
}
bool pointerDown() {
    for(auto *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))
        if(input->driver->type==LV_INDEV_TYPE_POINTER&&input->proc.state==LV_INDEV_STATE_PR)return true;
    return false;
}
void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius=8) {
    auto *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_radius(obj,radius,0);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return obj;
}
lv_obj_t *text(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font=nullptr,
               uint32_t color=Ink,lv_text_align_t align=LV_TEXT_ALIGN_LEFT) {
    if(!font)font=F14;
    auto *obj=lv_label_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,font->line_height+2);
    lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);lv_obj_set_style_text_align(obj,align,0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);lv_label_set_text(obj,value.c_str());lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);return obj;
}
lv_obj_t *button(lv_obj_t *parent,const char *value,int x,int y,int w,int h,int command,bool primary=false,bool dark=false) {
    auto *obj=box(parent,x,y,w,h,primary?Blue:dark?Header:Light);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(obj,event,LV_EVENT_CLICKED,(void *)(intptr_t)command);Ui::attachPressFeedback(obj);
    text(obj,value,2,(h-F16->line_height)/2,w-4,F16,primary||dark?Ink:Bg,LV_TEXT_ALIGN_CENTER);return obj;
}
void loadBest() {
    if(loaded)return;Preferences prefs;if(!prefs.begin("plane",false))return;
    if(!prefs.isKey("best"))loaded=true;
    else {const uint32_t value=prefs.getUInt("best",UINT32_MAX);if(value!=UINT32_MAX){storedBest=value;loaded=true;}}
    prefs.end();if(loaded&&storedBest>game.best)game.best=storedBest;
}
void persistBest() {
    loadBest();if(!loaded||game.best<=storedBest)return;Preferences prefs;if(!prefs.begin("plane",false))return;
    if(prefs.putUInt("best",game.best)==sizeof(uint32_t))storedBest=game.best;prefs.end();
}
void rect(lv_draw_ctx_t *ctx,int x,int y,int w,int h,uint32_t color,int radius=0) {
    if(w<=0||h<=0)return;lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.bg_color=lv_color_hex(color);d.radius=radius;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+h-1)};lv_draw_rect(ctx,&d,&a);
}
void triangle(lv_draw_ctx_t *ctx,int x1,int y1,int x2,int y2,int x3,int y3,uint32_t color) {
    lv_point_t p[]={{(lv_coord_t)x1,(lv_coord_t)y1},{(lv_coord_t)x2,(lv_coord_t)y2},{(lv_coord_t)x3,(lv_coord_t)y3}};
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.bg_color=lv_color_hex(color);lv_draw_polygon(ctx,&d,p,3);
}
void aircraft(lv_draw_ctx_t *ctx,int x,int y,uint32_t color,bool up,int size=1) {
    const int d=up?-1:1;
    rect(ctx,x-3*size,y-8*size,6*size,19*size,color,2*size);
    triangle(ctx,x,y+d*13*size,x-3*size,y+d*5*size,x+3*size,y+d*5*size,color);
    triangle(ctx,x-2*size,y+d*2*size,x-13*size,y-d*6*size,x-2*size,y-d*5*size,color);
    triangle(ctx,x+2*size,y+d*2*size,x+13*size,y-d*6*size,x+2*size,y-d*5*size,color);
    rect(ctx,x-7*size,y-d*8*size,14*size,3*size,color,size);
    rect(ctx,x-size,y+d*3*size,2*size,5*size,up?Blue:Bg,size);
}
void boss(lv_draw_ctx_t *ctx,int x,int y) {
    rect(ctx,x-29,y-5,58,15,0xD5A48F,4);rect(ctx,x-22,y-13,13,27,0xF4CFAC,4);rect(ctx,x+9,y-13,13,27,0xF4CFAC,4);
    rect(ctx,x-8,y-16,16,32,Ink,6);rect(ctx,x-4,y-10,8,9,Blue,3);
    rect(ctx,x-19,y+12,7,5,Red,2);rect(ctx,x+12,y+12,7,5,Red,2);
}
void drawText(lv_draw_ctx_t *ctx,const char *value,int x,int y,int w,uint32_t color=Muted) {
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=F14;d.color=lv_color_hex(color);d.align=LV_TEXT_ALIGN_LEFT;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+F14->line_height+1)};lv_draw_label(ctx,&d,&a,value,nullptr);
}
void drawStage(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);const int ox=a.x1,oy=a.y1;
    for(int i=0;i<31;++i) {
        const int x=9+(i*71)%303,y=22+(i*47+game.elapsedMs/(i%3?55:35))%184;
        rect(ctx,ox+x,oy+y,i%5?1:2,i%3?2:5,i%4?0x2A4A60:0x547A8B,1);
    }
    for(const auto &bullet:game.shots)if(bullet.active)rect(ctx,ox+(int)bullet.x-1,oy+(int)bullet.y-4,3,8,0xA6E3E0,1);
    for(const auto &enemy:game.enemies)if(enemy.active) {
        if(enemy.kind==2)boss(ctx,ox+(int)enemy.x,oy+(int)enemy.y);
        else {
            aircraft(ctx,ox+(int)enemy.x,oy+(int)enemy.y,enemy.kind==1?0xF2BD8B:0xE4D7B2,false);
            if(enemy.kind==1)rect(ctx,ox+(int)enemy.x-9,oy+(int)enemy.y+2,18,3,0xC47C72,1);
        }
    }
    for(const auto &bullet:game.hostile)if(bullet.active)rect(ctx,ox+(int)bullet.x-3,oy+(int)bullet.y-3,6,7,Red,3);
    if(game.lives) {
        const uint32_t color=game.invulnerableMs&&game.elapsedMs/90%2?0x73A7BC:Ink;
        rect(ctx,ox+(int)game.shipX-3,oy+(int)game.shipY+11,6,5,game.elapsedMs/80%2?Blue:0x80C9D7,2);
        aircraft(ctx,ox+(int)game.shipX,oy+(int)game.shipY,color,true);
        if(game.invulnerableMs) {
            lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_TRANSP;d.border_opa=150;d.border_color=lv_color_hex(0x80C9D7);d.border_width=1;d.radius=18;
            lv_area_t shield={(lv_coord_t)(ox+game.shipX-18),(lv_coord_t)(oy+game.shipY-18),(lv_coord_t)(ox+game.shipX+18),(lv_coord_t)(oy+game.shipY+18)};
            lv_draw_rect(ctx,&d,&shield);
        }
    }
    // The narrow flight HUD stays readable while incoming planes enter from
    // above it. Animation derives from simulation time, so pause freezes it.
    rect(ctx,ox,oy,320,20,Header);
    drawText(ctx,"生命",ox+9,oy+2,30);
    for(int i=0;i<3;++i) {
        const uint32_t color=i<game.lives?Ink:0x3D6070;
        triangle(ctx,ox+48+i*16,oy+4,ox+42+i*16,oy+15,ox+54+i*16,oy+15,color);
        rect(ctx,ox+47+i*16,oy+11,3,6,color,1);
    }
    if(game.wave<4) {
        char value[24];snprintf(value,sizeof(value),"第 %u 波 / 3",(unsigned)game.wave);drawText(ctx,value,ox+103,oy+2,126);
    } else {
        drawText(ctx,"首领",ox+103,oy+2,42);rect(ctx,ox+147,oy+7,160,6,0x335A6C,3);
        for(const auto &enemy:game.enemies)if(enemy.active&&enemy.kind==2)rect(ctx,ox+147,oy+7,160*enemy.hp/48,6,Red,3);
    }
}
void drawIcon(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    rect(ctx,a.x1,a.y1,56,56,Header,14);rect(ctx,a.x1+14,a.y1+8,2,7,0x80C9D7,1);rect(ctx,a.x1+40,a.y1+18,2,7,Red,1);
    aircraft(ctx,a.x1+28,a.y1+31,Ink,true);
}
void closeOverlay() {
    Ui::cancelAnimations();if(overlay)lv_obj_del(overlay);overlay=nullptr;overlayKind=-1;
    game.endDrag();blockUntilRelease=pointerDown();
}
void openOverlay(int kind) {
    closeOverlay();overlay=box(pageRoot,0,0,320,240,0x071724,0);lv_obj_set_style_bg_opa(overlay,150,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
    auto *card=box(overlay,28,36,264,168,White,14);
    if(kind==(int)State::Ready) {
        text(card,"飞机大战",12,8,240,F16,Bg,LV_TEXT_ALIGN_CENTER);button(card,"返回",204,4,48,28,Back);
        text(card,"按住拖动飞机",8,34,248,F14,Bg,LV_TEXT_ALIGN_CENTER);
        text(card,"自动射击，躲避敌机",8,54,248,F14,Blue,LV_TEXT_ALIGN_CENTER);
        text(card,"三次受击后结束",8,74,248,F14,Blue,LV_TEXT_ALIGN_CENTER);
        text(card,"三波敌机后挑战首领",8,96,248,F14,Blue,LV_TEXT_ALIGN_CENTER);
        button(card,"开始游戏",16,128,232,32,Start,true);
    } else {
        const bool paused=kind==(int)State::Paused,won=kind==(int)State::Won;
        text(card,paused?"已暂停":won?"全部通关":"游戏结束",12,16,240,F16,Bg,LV_TEXT_ALIGN_CENTER);
        text(card,String("得分 ")+String((unsigned long)game.score)+"   最高 "+String((unsigned long)game.best),8,48,248,F16,Bg,LV_TEXT_ALIGN_CENTER);
        if(won)text(card,"首领已击败",8,78,248,F14,Blue,LV_TEXT_ALIGN_CENTER);
        if(paused) {
            button(card,"继续",12,112,64,40,Resume,true);button(card,"重新开始",80,112,100,40,Restart);button(card,"返回",184,112,68,40,Back);
        } else {
            button(card,"再来一局",16,112,136,40,Restart,true);button(card,"返回",160,112,88,40,Back);
        }
    }
    overlayKind=kind;
}
void apply() {
    if(!pageRoot)return;
    if(shownScore!=game.score){lv_label_set_text_fmt(scoreLabel,"分 %lu",(unsigned long)game.score);shownScore=game.score;}
    if(shownBest!=game.best){lv_label_set_text_fmt(bestLabel,"最高 %lu",(unsigned long)game.best);shownBest=game.best;}
    if(game.state==State::Playing){if(overlay)closeOverlay();}
    else if(overlayKind!=(int)game.state)openOverlay((int)game.state);
    lv_obj_invalidate(stage);
}
void event(lv_event_t *e) {
    const int command=(int)(intptr_t)lv_event_get_user_data(e);lastInputAt=millis();
    switch(command) {
        case Back:{game.pause(millis());persistBest();lv_event_t forwarded=*e;forwarded.user_data=(void *)(uintptr_t)Ui::Back;Ui::dispatchAction(&forwarded);return;}
        case Pause:game.pause(millis());persistBest();break;
        case Resume:game.resume(millis());break;
        case Start:game.start(millis(),millis()*2654435761u+0x9E3779B9u);break;
        case Restart:persistBest();game.restart(millis());break;
        default:return;
    }
    game.endDrag();blockUntilRelease=pointerDown();apply();
}
void touchStage(lv_event_t *e) {
    const auto code=lv_event_get_code(e);
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_RELEASED&&code!=LV_EVENT_PRESS_LOST)return;
    auto *input=lv_indev_get_act();if(!input)return;lv_point_t p;lv_indev_get_point(input,&p);lv_area_t a;lv_obj_get_coords(stage,&a);
    if(code==LV_EVENT_PRESSED) {
        if(!blockUntilRelease&&game.beginDrag(p.x-a.x1,p.y-a.y1))lastInputAt=millis();
    } else if(code==LV_EVENT_PRESSING) {
        if(!blockUntilRelease&&game.drag(p.x-a.x1,p.y-a.y1)){lastInputAt=millis();apply();}
    } else {
        if(code==LV_EVENT_RELEASED&&!blockUntilRelease)game.drag(p.x-a.x1,p.y-a.y1);
        game.endDrag();if(code==LV_EVENT_RELEASED)blockUntilRelease=false;
        if(game.state==State::GameOver)persistBest();apply();
    }
}
void onTick(lv_timer_t *) {
    if(!pageRoot)return;if(blockUntilRelease&&!pointerDown())blockUntilRelease=false;
    if(game.tick(millis())){if(game.state==State::GameOver||game.state==State::Won)persistBest();apply();}
}
} // namespace

void createIcon(lv_obj_t *parent,int x,int y) {
    auto *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,56,56);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(obj,drawIcon,LV_EVENT_DRAW_MAIN,nullptr);
}
void build(lv_obj_t *parent) {
    loadBest();if(!prepared){game.prepare(millis()*2654435761u+0x9E3779B9u);prepared=true;}
    if(resumeRebuild){game.resume(millis());resumeRebuild=false;}
    pageRoot=parent;stage=overlay=scoreLabel=bestLabel=nullptr;overlayKind=-1;shownScore=shownBest=UINT32_MAX;blockUntilRelease=pointerDown();game.endDrag();
    lv_obj_set_style_bg_color(parent,lv_color_hex(Header),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);lv_obj_clear_flag(parent,LV_OBJ_FLAG_SCROLLABLE);
    button(parent,"返回",0,0,40,32,Back,false,true);text(parent,"飞机大战",45,6,90,F16);
    scoreLabel=text(parent,"",140,8,54,F14);bestLabel=text(parent,"",200,8,56,F14,Muted);button(parent,"暂停",260,0,60,32,Pause,false,true);
    stage=box(parent,0,32,320,208,Bg,0);lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);lv_obj_clear_flag(stage,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(stage,drawStage,LV_EVENT_DRAW_MAIN,nullptr);lv_obj_add_event_cb(stage,touchStage,LV_EVENT_ALL,nullptr);
    if(timer)lv_timer_del(timer);timer=lv_timer_create(onTick,20,nullptr);apply();
}
void refresh(bool force){if(force)apply();}
void cancel() {
    if(timer){lv_timer_del(timer);timer=nullptr;}
    if(pageRoot){game.endDrag();const bool pausedNow=game.pause(millis());resumeRebuild=app==App::Plane&&pausedNow;persistBest();}
    else if(app!=App::Plane)resumeRebuild=false;
    pageRoot=stage=overlay=scoreLabel=bestLabel=nullptr;overlayKind=-1;blockUntilRelease=pointerDown();
}
bool back(){return false;}
bool canBack(){return false;}
bool inputActive(){return game.dragging||blockUntilRelease;}
Status status(){return {stateName(game.state),game.score,game.best,game.kills,game.lives,game.wave};}
uint32_t bestScore(){loadBest();return game.best>storedBest?game.best:storedBest;}
#ifdef GIFT_HOST_TEST
const PlaneEngine::Game &debugGame(){return game;}
void debugVariant(int which) {
    const auto best=game.best;game=Game();game.best=best;game.prepare(20261113u);prepared=true;resumeRebuild=false;loadBest();
    if(which) {
        game.start(millis(),20261113u);
        if(which==2)game.pause(millis());
        else if(which==3){game.lives=0;game.state=State::GameOver;}
        else if(which==4){game.wave=4;game.score=540;game.kills=31;if(game.best<game.score)game.best=game.score;game.state=State::Won;}
        else if(which==5){
            game.wave=4;game.spawned=1;game.score=110;game.kills=10;
            auto &enemy=game.enemies[0];enemy.active=true;enemy.kind=2;enemy.hp=34;enemy.x=160;enemy.y=44;enemy.vx=58;
            auto &bullet=game.hostile[0];bullet.active=true;bullet.x=145;bullet.y=108;bullet.vx=-28;bullet.vy=96;
        } else if(which==6){auto &bullet=game.hostile[0];bullet.active=true;bullet.x=160;bullet.y=160;bullet.vy=100;}
    }
    blockUntilRelease=pointerDown();apply();
}
#endif
} // namespace PlanePage
