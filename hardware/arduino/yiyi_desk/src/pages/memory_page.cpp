#include "mini_apps.h"
#include "memory_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>
#include <math.h>

namespace MemoryPage {
namespace {
using namespace MemoryEngine;
constexpr uint32_t Bg=0xF7F1ED, Ink=0x574356, Muted=0x947E92;
constexpr uint32_t Green=0x926A88, Light=0xEADCE6, White=0xFFFEFA;
const lv_font_t *F14=&fd_sans_bold_14_ui,*F16=&fd_sans_bold_16_ui;
enum Command {Back=1,Pause,Resume,Restart,Start,NextLevel};
Game game;
lv_obj_t *pageRoot=nullptr,*stage=nullptr,*overlay=nullptr,*scoreLabel=nullptr,*bestLabel=nullptr;
lv_timer_t *timer=nullptr;
bool prepared=false,loaded=false,blockUntilRelease=false,touching=false,resumeRebuild=false;
uint32_t storedBest=0,shownScore=UINT32_MAX,shownBest=UINT32_MAX;
int overlayKind=-1;
lv_point_t pressPoint{};
int pressedCard=-1; bool moved=false;

const char *stateName(State state) {
    switch(state) {
        case State::Ready:return "ready";case State::Playing:return "playing";
        case State::Comparing:return "comparing";case State::Paused:return "paused";
        case State::LevelCleared:return "levelcleared";default:return "won";
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
    Preferences prefs;if(!prefs.begin("memory",false))return;
    if(!prefs.isKey("best"))loaded=true;
    else {const uint32_t value=prefs.getUInt("best",UINT32_MAX);if(value!=UINT32_MAX){storedBest=value;loaded=true;}}
    prefs.end();if(loaded&&storedBest>game.best)game.best=storedBest;
}
void persistBest() {
    loadBest();if(!loaded||game.best<=storedBest)return;
    Preferences prefs;if(!prefs.begin("memory",false))return;
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
void polygon(lv_draw_ctx_t *ctx,const lv_point_t *points,int count,uint32_t color) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;lv_draw_polygon(ctx,&d,points,count);
}
void drawText(lv_draw_ctx_t *ctx,const char *value,int x,int y,int w,uint32_t color=Muted,lv_text_align_t align=LV_TEXT_ALIGN_CENTER) {
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=F14;d.color=lv_color_hex(color);d.align=align;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+F14->line_height+1)};lv_draw_label(ctx,&d,&a,value,nullptr);
}
void motif(lv_draw_ctx_t *ctx,int x,int y,uint8_t face) {
    static const uint32_t colors[]={0xBE6C71,0x69918B,0xB49C58,0x8B82A5,0x75995B,0xD28C57,0x5B93AB,0xB6779D,0x8B9577,0xBEA469};
    const uint32_t c=colors[face%10];
    switch(face%10) {
        case 0: // heart
            rect(ctx,x-10,y-7,11,11,c,6);rect(ctx,x-1,y-7,11,11,c,6);
            {lv_point_t p[]={{(lv_coord_t)(x-9),(lv_coord_t)y},{(lv_coord_t)(x+9),(lv_coord_t)y},{(lv_coord_t)x,(lv_coord_t)(y+10)}};polygon(ctx,p,3,c);}break;
        case 1: // leaf
            rect(ctx,x-10,y-8,20,16,c,9);line(ctx,x-7,y+6,x+7,y-6,White,2);break;
        case 2: // star
            {lv_point_t p[10];for(int i=0;i<10;++i){const float a=i*0.62831853f-1.5707963f,r=i%2?5.f:11.f;p[i]={(lv_coord_t)(x+cosf(a)*r),(lv_coord_t)(y+sinf(a)*r)};}
            for(int i=0;i<10;++i){lv_point_t triangle[]={{(lv_coord_t)x,(lv_coord_t)y},p[i],p[(i+1)%10]};polygon(ctx,triangle,3,c);}}break;
        case 3: // moon
            rect(ctx,x-9,y-10,19,20,c,10);rect(ctx,x-2,y-11,15,16,White,8);break;
        case 4: // tree
            {lv_point_t p[]={{(lv_coord_t)x,(lv_coord_t)(y-11)},{(lv_coord_t)(x-11),(lv_coord_t)(y+6)},{(lv_coord_t)(x+11),(lv_coord_t)(y+6)}};polygon(ctx,p,3,c);}rect(ctx,x-2,y+4,4,8,c,1);break;
        case 5: // flower
            for(int i=0;i<4;++i)rect(ctx,x-5+(i==1?6:i==3?-6:0),y-5+(i==0?-6:i==2?6:0),10,10,c,5);
            rect(ctx,x-3,y-3,6,6,0xF3D992,3);break;
        case 6: // diamond
            {lv_point_t p[]={{(lv_coord_t)x,(lv_coord_t)(y-11)},{(lv_coord_t)(x+11),(lv_coord_t)y},{(lv_coord_t)x,(lv_coord_t)(y+11)},{(lv_coord_t)(x-11),(lv_coord_t)y}};polygon(ctx,p,4,c);}line(ctx,x-7,y,x+7,y,White,1);break;
        case 7: // bow
            {lv_point_t left[]={{(lv_coord_t)(x-11),(lv_coord_t)(y-8)},{(lv_coord_t)x,(lv_coord_t)y},{(lv_coord_t)(x-11),(lv_coord_t)(y+8)}};
            lv_point_t right[]={{(lv_coord_t)(x+11),(lv_coord_t)(y-8)},{(lv_coord_t)(x+11),(lv_coord_t)(y+8)},{(lv_coord_t)x,(lv_coord_t)y}};polygon(ctx,left,3,c);polygon(ctx,right,3,c);}rect(ctx,x-3,y-3,6,6,White,3);break;
        case 8: // house
            {lv_point_t p[]={{(lv_coord_t)x,(lv_coord_t)(y-11)},{(lv_coord_t)(x-12),(lv_coord_t)(y-1)},{(lv_coord_t)(x+12),(lv_coord_t)(y-1)}};polygon(ctx,p,3,c);}rect(ctx,x-8,y-1,16,12,c,2);rect(ctx,x-2,y+3,4,8,White,1);break;
        default: // sun
            rect(ctx,x-7,y-7,14,14,c,7);for(int i=0;i<8;++i){float a=i*0.78539816f;line(ctx,x+(int)(cosf(a)*10),y+(int)(sinf(a)*10),x+(int)(cosf(a)*12),y+(int)(sinf(a)*12),c,2);}break;
    }
}
void drawStage(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    char info[64];snprintf(info,sizeof(info),"第%u关  配对 %u/%u",game.level,game.pairs,game.totalPairs);
    drawText(ctx,info,a.x1+10,a.y1+4,174,Ink,LV_TEXT_ALIGN_LEFT);
    snprintf(info,sizeof(info),"%u步 %lu秒",game.moves,(unsigned long)(game.elapsedMs/1000));
    drawText(ctx,info,a.x1+185,a.y1+4,126,Muted,LV_TEXT_ALIGN_RIGHT);
    for(int i=0;i<game.count;++i) {
        const int x=a.x1+23+(i%4)*70,y=a.y1+game.top()+(i/4)*36;
        rect(ctx,x,y,64,30,game.visible(i)?White:Light,6);
        if(game.visible(i)) {
            motif(ctx,x+32,y+15,game.cards[i].face);
            if(game.cards[i].matched){rect(ctx,x+4,y+13,3,5,0x7BA689,1);}
        } else {
            rect(ctx,x+23,y+7,18,16,0xCAB1C4,5);rect(ctx,x+29,y+11,6,8,Light,3);
        }
    }
}
void drawIcon(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    rect(ctx,a.x1,a.y1,56,56,Light,14);rect(ctx,a.x1+8,a.y1+13,25,32,0xC8ACC0,5);rect(ctx,a.x1+23,a.y1+8,25,32,White,5);motif(ctx,a.x1+35,a.y1+23,0);
}
void closeOverlay() {
    Ui::cancelAnimations();if(overlay)lv_obj_del(overlay);overlay=nullptr;overlayKind=-1;
    touching=false;blockUntilRelease=pointerDown();
}
void openOverlay(int kind) {
    closeOverlay();overlay=box(pageRoot,0,0,320,240,Ink,0);lv_obj_set_style_bg_opa(overlay,140,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
    auto *card=box(overlay,28,36,264,168,White,14);
    if(kind==(int)State::Ready) {
        text(card,"记忆翻牌",12,8,240,F16,Green,LV_TEXT_ALIGN_CENTER);button(card,"返回",204,4,48,28,Back);
        text(card,"翻开两张，寻找相同图案",8,34,248,F14,Ink,LV_TEXT_ALIGN_CENTER);
        text(card,"配对成功可获得100分",8,54,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"图案不同会短暂停留",8,74,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"三关挑战，步数越少越好",8,96,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        button(card,"开始游戏",16,128,232,32,Start,true);
    } else {
        const bool paused=kind==(int)State::Paused,clear=kind==(int)State::LevelCleared;
        text(card,paused?"已暂停":clear?"目标达成":"全部通关",12,16,240,F16,Green,LV_TEXT_ALIGN_CENTER);
        text(card,String("得分 ")+String((unsigned long)game.score)+"   最高 "+String((unsigned long)game.best),8,48,248,F16,Ink,LV_TEXT_ALIGN_CENTER);
        text(card,clear?"准备进入下一关":paused?"继续寻找相同图案":"三关配对全部完成",8,78,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        if(paused) {
            button(card,"继续",12,112,64,40,Resume,true);button(card,"重新开始",80,112,100,40,Restart);button(card,"返回",184,112,68,40,Back);
        } else {
            button(card,clear?"下一关":"再来一局",16,112,136,40,clear?NextLevel:Restart,true);button(card,"返回",160,112,88,40,Back);
        }
    }
    overlayKind=kind;
}
bool modal(){return game.state==State::Ready||game.state==State::Paused||game.state==State::LevelCleared||game.state==State::Won;}
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
    const auto code=lv_event_get_code(e);
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_RELEASED&&code!=LV_EVENT_PRESS_LOST)return;
    auto *input=lv_indev_get_act();if(!input)return;
    lv_point_t p;lv_indev_get_point(input,&p);lv_area_t area;lv_obj_get_coords(stage,&area);
    if(code==LV_EVENT_PRESSED) {
        if(blockUntilRelease||game.state!=State::Playing)return;
        pressPoint=p;pressedCard=game.hitTest(p.x-area.x1,p.y-area.y1);touching=true;moved=false;
    } else if(code==LV_EVENT_PRESSING) {
        if(touching&&(abs(p.x-pressPoint.x)>10||abs(p.y-pressPoint.y)>10))moved=true;
    } else if(code==LV_EVENT_RELEASED) {
        const int released=game.hitTest(p.x-area.x1,p.y-area.y1);
        const bool tap=touching&&!blockUntilRelease&&!moved&&pressedCard>=0&&released==pressedCard&&abs(p.x-pressPoint.x)<=10&&abs(p.y-pressPoint.y)<=10;
        touching=false;blockUntilRelease=false;
        if(tap){game.select(pressedCard,millis());lastInputAt=millis();if(modal())persistBest();apply();}
        pressedCard=-1;
    } else {touching=false;pressedCard=-1;}
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
    button(parent,"返回",0,0,40,32,Back);text(parent,"记忆翻牌",45,6,90,F16);
    scoreLabel=text(parent,"",140,8,54,F14,Green);bestLabel=text(parent,"",200,8,56,F14,Muted);button(parent,"暂停",260,0,60,32,Pause);
    stage=box(parent,0,32,320,208,Bg,0);lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);lv_obj_clear_flag(stage,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(stage,drawStage,LV_EVENT_DRAW_MAIN,nullptr);lv_obj_add_event_cb(stage,touchStage,LV_EVENT_ALL,nullptr);
    if(timer)lv_timer_del(timer);timer=lv_timer_create(onTick,40,nullptr);apply();
}
void refresh(bool force){if(force)apply();}
void cancel() {
    if(timer){lv_timer_del(timer);timer=nullptr;}
    if(pageRoot){const bool pausedNow=game.pause(millis());resumeRebuild=app==App::Memory&&pausedNow;persistBest();}
    else if(app!=App::Memory)resumeRebuild=false;
    pageRoot=stage=overlay=scoreLabel=bestLabel=nullptr;overlayKind=-1;touching=false;blockUntilRelease=pointerDown();
}
bool back(){return false;}bool canBack(){return false;}bool inputActive(){return touching||blockUntilRelease;}
Status status(){return {stateName(game.state),game.score,game.best,game.elapsedMs,game.moves,game.level,game.pairs,game.totalPairs};}
uint32_t bestScore(){loadBest();return game.best>storedBest?game.best:storedBest;}
#ifdef GIFT_HOST_TEST
const MemoryEngine::Game &debugGame(){return game;}
void debugVariant(int which) {
    const auto best=game.best;game=Game{};game.best=best;game.prepare(20261113u);prepared=true;resumeRebuild=false;loadBest();
    if(which) {
        game.start(millis(),20261113u);
        if(which==2)game.pause(millis());
        else if(which==3){game.select(0,millis());for(int i=1;i<game.count;++i)if(game.cards[i].face!=game.cards[0].face){game.select(i,millis());break;}}
        else if(which==4){game.score=800;game.pairs=game.totalPairs;game.state=State::LevelCleared;}
        else if(which==5){game.level=3;game.totalPairs=10;game.count=20;game.score=3000;game.state=State::Won;}
        else if(which==6){game.state=State::LevelCleared;game.nextLevel(millis());}
    }
    touching=false;pressedCard=-1;blockUntilRelease=pointerDown();apply();
}
#endif
} // namespace MemoryPage
