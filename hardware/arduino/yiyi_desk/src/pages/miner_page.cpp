#include "mini_apps.h"
#include "miner_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

namespace MinerPage {
namespace {
using namespace MinerEngine;
constexpr uint32_t Bg=0xF5F0E2, Ink=0x4A4739, Muted=0x8A8066;
constexpr uint32_t Green=0x556845, Light=0xE8E6D3, White=0xFFFEF7;
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
        case State::Casting:return "casting";case State::Reeling:return "reeling";
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
    Preferences prefs;if(!prefs.begin("miner",false))return;
    if(!prefs.isKey("best"))loaded=true;
    else {const uint32_t value=prefs.getUInt("best",UINT32_MAX);if(value!=UINT32_MAX){storedBest=value;loaded=true;}}
    prefs.end();if(loaded&&storedBest>game.best)game.best=storedBest;
}
void persistBest() {
    loadBest();if(!loaded||game.best<=storedBest)return;
    Preferences prefs;if(!prefs.begin("miner",false))return;
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
void drawOre(lv_draw_ctx_t *ctx,int x,int y,int radius,Kind kind) {
    if(kind==Kind::Diamond) {
        lv_point_t p[]={{(lv_coord_t)x,(lv_coord_t)(y-radius)},{(lv_coord_t)(x+radius),(lv_coord_t)(y-2)},
                       {(lv_coord_t)(x+radius-2),(lv_coord_t)(y+4)},{(lv_coord_t)x,(lv_coord_t)(y+radius)},
                       {(lv_coord_t)(x-radius+2),(lv_coord_t)(y+4)},{(lv_coord_t)(x-radius),(lv_coord_t)(y-2)}};
        polygon(ctx,p,6,0x77ADAD);line(ctx,x-radius+2,y-2,x+radius-2,y-2,0xDFF6EA,1);
        line(ctx,x,y-radius+2,x-3,y-2,White,1);line(ctx,x-3,y-2,x,y+radius-2,0xBFE0CF,1);
    } else if(kind==Kind::Gold) {
        lv_point_t p[]={{(lv_coord_t)(x-radius),(lv_coord_t)(y+3)},{(lv_coord_t)(x-radius+3),(lv_coord_t)(y-radius+4)},
                       {(lv_coord_t)(x+2),(lv_coord_t)(y-radius)},{(lv_coord_t)(x+radius),(lv_coord_t)(y-3)},
                       {(lv_coord_t)(x+radius-2),(lv_coord_t)(y+radius-3)},{(lv_coord_t)(x-4),(lv_coord_t)(y+radius)}};
        polygon(ctx,p,6,0xD8AB51);line(ctx,x-radius+5,y-2,x-2,y-radius+4,0xF7D789,2);
        line(ctx,x+radius-3,y+1,x+radius-5,y+radius-5,0xB48A40,2);
    } else {
        rect(ctx,x-radius,y-radius+2,radius*2,radius*2-3,0x94958A,radius/2);
        line(ctx,x-radius+4,y-3,x-1,y-radius+5,0xC6C5B8,2);line(ctx,x+3,y,x+radius-4,y+radius-4,0x787F73,2);
    }
}
void drawStage(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    const int ox=a.x1,oy=a.y1;
    rect(ctx,ox,oy,320,53,0xEEEBD9);rect(ctx,ox,oy+53,320,155,0xE5DAC0);
    rect(ctx,ox,oy+53,320,5,0xC5BC96);
    for(int row=0;row<5;++row)for(int col=0;col<9;++col)
        rect(ctx,ox+14+col*37+(row%2?13:0),oy+74+row*29,3+(col%3),2,0xD4C6A9,1);
    char info[48];snprintf(info,sizeof(info),"第%u关  目标 %u",(unsigned)game.level,(unsigned)game.target);
    drawText(ctx,info,ox+8,oy+3,142,Ink,LV_TEXT_ALIGN_LEFT);
    snprintf(info,sizeof(info),"%u秒",(unsigned)game.secondsLeft());
    drawText(ctx,info,ox+260,oy+3,52,game.secondsLeft()<=10?0xB3694C:Ink);
    const State phase=game.state==State::Paused?game.pausedState:game.state;
    drawText(ctx,phase==State::Playing||phase==State::Ready?"点屏幕放钩":phase==State::Casting?"瞄准宝藏":"正在收回",ox+8,oy+26,116);
    // A small winch and miner sit above the earth. The visible rope endpoint is
    // exactly the swept collision point owned by the engine.
    rect(ctx,ox+148,oy+21,24,19,0x77865B,5);rect(ctx,ox+151,oy+10,18,16,0xDDBA91,6);
    rect(ctx,ox+147,oy+6,26,10,0xD4A148,6);rect(ctx,ox+145,oy+13,30,4,0xB58E40,2);
    rect(ctx,ox+158,oy+8,6,6,0xFFF4BC,3);rect(ctx,ox+169,oy+30,12,9,0x98805A,4);
    for(int i=0;i<game.oreCount;++i)if(!game.ores[i].removed&&i!=game.caught) {
        const auto &ore=game.ores[i];drawOre(ctx,ox+(int)ore.x,oy+(int)ore.y,(int)ore.radius,ore.kind);
    }
    const int hx=ox+(int)game.hookX,hy=oy+(int)game.hookY;
    line(ctx,ox+(int)OriginX,oy+(int)OriginY,hx,hy,0x756950,2);
    const float dx=sinf(game.angle),dy=cosf(game.angle);
    const int lx=hx+(int)(-dy*6+dx*5),ly=hy+(int)(dx*6+dy*5);
    const int rx=hx+(int)(dy*6+dx*5),ry=hy+(int)(-dx*6+dy*5);
    line(ctx,lx,ly,hx,hy,0x566253,2);line(ctx,hx,hy,rx,ry,0x566253,2);
    if(game.caught>=0&&game.caught<game.oreCount) {
        const auto &ore=game.ores[game.caught];drawOre(ctx,hx,hy,(int)ore.radius,ore.kind);
    }
}
void drawIcon(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    rect(ctx,a.x1,a.y1,56,56,0xE8DDC0,14);line(ctx,a.x1+28,a.y1+7,a.x1+28,a.y1+28,0x756950,3);
    drawOre(ctx,a.x1+28,a.y1+39,13,Kind::Gold);line(ctx,a.x1+21,a.y1+29,a.x1+28,a.y1+24,Ink,2);line(ctx,a.x1+28,a.y1+24,a.x1+35,a.y1+29,Ink,2);
}
void closeOverlay() {
    Ui::cancelAnimations();if(overlay)lv_obj_del(overlay);overlay=nullptr;overlayKind=-1;
    touching=false;blockUntilRelease=pointerDown();
}
void openOverlay(int kind) {
    closeOverlay();overlay=box(pageRoot,0,0,320,240,Ink,0);lv_obj_set_style_bg_opa(overlay,140,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
    auto *card=box(overlay,28,36,264,168,White,14);
    if(kind==(int)State::Ready) {
        text(card,"黄金矿工",12,8,240,F16,Green,LV_TEXT_ALIGN_CENTER);button(card,"返回",204,4,48,28,Back);
        text(card,"点击屏幕，沿方向放钩",8,34,248,F14,Ink,LV_TEXT_ALIGN_CENTER);
        text(card,"收回黄金与钻石获得分数",8,54,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"石头价值低，收回更慢",8,74,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"每关60秒，达到目标过关",8,96,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        button(card,"开始游戏",16,128,232,32,Start,true);
    } else {
        const bool paused=kind==(int)State::Paused,clear=kind==(int)State::LevelCleared,won=kind==(int)State::Won;
        text(card,paused?"已暂停":clear?"目标达成":won?"全部通关":"时间到了",12,16,240,F16,Green,LV_TEXT_ALIGN_CENTER);
        text(card,String("得分 ")+String((unsigned long)game.score)+"   最高 "+String((unsigned long)game.best),8,48,248,F16,Ink,LV_TEXT_ALIGN_CENTER);
        text(card,clear?"准备进入下一关":won?"三关宝藏已收集":paused?"继续收集地下宝藏":"再来试试新的方向",8,78,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
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
    const auto code=lv_event_get_code(e);if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_RELEASED&&code!=LV_EVENT_PRESS_LOST)return;
    auto *input=lv_indev_get_act();if(!input)return;
    lv_point_t p;lv_indev_get_point(input,&p);lv_area_t area;lv_obj_get_coords(stage,&area);
    if(code==LV_EVENT_PRESSED) {
        if(blockUntilRelease||game.state!=State::Playing)return;pressPoint=p;touching=true;
    } else if(code==LV_EVENT_RELEASED) {
        const bool inside=p.x>=area.x1&&p.x<=area.x2&&p.y>=area.y1&&p.y<=area.y2;
        const bool cast=touching&&!blockUntilRelease&&inside&&abs(p.x-pressPoint.x)<=10&&abs(p.y-pressPoint.y)<=10;
        touching=false;blockUntilRelease=false;
        if(cast){game.cast(millis());lastInputAt=millis();if(modal())persistBest();apply();}
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
    button(parent,"返回",0,0,40,32,Back);text(parent,"黄金矿工",45,6,90,F16);
    scoreLabel=text(parent,"",140,8,54,F14,Green);bestLabel=text(parent,"",200,8,56,F14,Muted);button(parent,"暂停",260,0,60,32,Pause);
    stage=box(parent,0,32,320,208,Bg,0);lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);lv_obj_clear_flag(stage,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(stage,drawStage,LV_EVENT_DRAW_MAIN,nullptr);lv_obj_add_event_cb(stage,touchStage,LV_EVENT_ALL,nullptr);
    if(timer)lv_timer_del(timer);timer=lv_timer_create(onTick,40,nullptr);apply();
}
void refresh(bool force){if(force)apply();}
void cancel() {
    if(timer){lv_timer_del(timer);timer=nullptr;}
    if(pageRoot){const bool pausedNow=game.pause(millis());resumeRebuild=app==App::Miner&&pausedNow;persistBest();}
    else if(app!=App::Miner)resumeRebuild=false;
    pageRoot=stage=overlay=scoreLabel=bestLabel=nullptr;overlayKind=-1;touching=false;blockUntilRelease=pointerDown();
}
bool back(){return false;}bool canBack(){return false;}bool inputActive(){return touching||blockUntilRelease;}
Status status(){return {stateName(game.state),game.score,game.best,game.secondsLeft(),game.target,game.level,game.remaining};}
uint32_t bestScore(){loadBest();return game.best>storedBest?game.best:storedBest;}
#ifdef GIFT_HOST_TEST
const MinerEngine::Game &debugGame(){return game;}
void debugVariant(int which) {
    const auto best=game.best;game=Game{};game.best=best;game.prepare(20261113u);prepared=true;resumeRebuild=false;loadBest();
    if(which) {
        game.start(millis(),20261113u);
        if(which==2)game.pause(millis());
        else if(which==3){game.elapsedMs=LevelDurationMs;game.state=State::GameOver;}
        else if(which==4){game.score=game.target;game.best=game.best>game.score?game.best:game.score;game.state=State::LevelCleared;}
        else if(which==5){game.level=3;game.target=1250;game.score=1250;game.best=game.best>game.score?game.best:game.score;game.state=State::Won;}
        else if(which==6){game.cast(millis());game.tick(millis()+250);game.lastTickAt=millis();}
        else if(which==7){game.cast(millis());game.tick(millis()+480);game.lastTickAt=millis();}
        else if(which==8){game.elapsedMs=LevelDurationMs-5;game.lastTickAt=millis();}
    }
    touching=false;blockUntilRelease=pointerDown();apply();
}
#endif
} // namespace MinerPage
