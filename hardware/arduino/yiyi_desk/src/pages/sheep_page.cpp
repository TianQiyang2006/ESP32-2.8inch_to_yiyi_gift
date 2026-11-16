#include "mini_apps.h"
#include "sheep_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

namespace SheepPage {
namespace {
using namespace SheepEngine;
constexpr uint32_t Bg=0xF4F5E8, Ink=0x29473B, Muted=0x778970;
constexpr uint32_t Green=0x41694C, Light=0xE0EAD6, White=0xFFFFFF;
const lv_font_t *F14=&fd_sans_bold_14_ui, *F16=&fd_sans_bold_16_ui;
enum Command { Back=1, Pause, Resume, Restart, Start, NextLevel, Undo, Shuffle };
Game game;
lv_obj_t *pageRoot=nullptr, *board=nullptr, *trayView=nullptr, *overlay=nullptr;
lv_obj_t *scoreLabel=nullptr, *bestLabel=nullptr, *undoKey=nullptr, *shuffleKey=nullptr;
lv_timer_t *timer=nullptr;
bool prepared=false, loaded=false, blockUntilRelease=false, touching=false;
uint32_t storedBest=0, shownScore=UINT32_MAX, shownBest=UINT32_MAX;
int overlayKind=-1, pressedTile=-1;
lv_point_t pressPoint{};

const char *stateName(State state) {
    switch(state) {
        case State::Ready:return "ready"; case State::Playing:return "playing";
        case State::Paused:return "paused"; case State::LevelCleared:return "levelcleared";
        case State::Won:return "won"; default:return "gameover";
    }
}
bool pointerDown() {
    for(auto *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))
        if(input->driver->type==LV_INDEV_TYPE_POINTER&&input->proc.state==LV_INDEV_STATE_PR)return true;
    return false;
}
void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius=8) {
    auto *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_radius(obj,radius,0);lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
lv_obj_t *text(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font=nullptr,
               uint32_t color=Ink,lv_text_align_t align=LV_TEXT_ALIGN_LEFT) {
    if(!font)font=F14;
    auto *obj=lv_label_create(parent);lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,font->line_height+2);
    lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_obj_set_style_text_align(obj,align,0);lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj,value.c_str());lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
lv_obj_t *button(lv_obj_t *parent,const char *value,int x,int y,int w,int h,int command,bool primary=false) {
    auto *obj=box(parent,x,y,w,h,primary?Green:Light);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj,event,LV_EVENT_CLICKED,(void *)(intptr_t)command);Ui::attachPressFeedback(obj);
    const auto *font=h<30?F14:F16;
    text(obj,value,2,(h-font->line_height)/2,w-4,font,primary?White:Ink,LV_TEXT_ALIGN_CENTER);
    return obj;
}
void loadBest() {
    if(loaded)return;
    Preferences prefs;if(!prefs.begin("sheep",false))return;
    if(!prefs.isKey("best"))loaded=true;
    else {const uint32_t value=prefs.getUInt("best",UINT32_MAX);if(value!=UINT32_MAX){storedBest=value;loaded=true;}}
    prefs.end();if(loaded&&storedBest>game.best)game.best=storedBest;
}
void persistBest() {
    loadBest();if(!loaded||game.best<=storedBest)return;
    Preferences prefs;if(!prefs.begin("sheep",false))return;
    if(prefs.putUInt("best",game.best)==sizeof(uint32_t))storedBest=game.best;
    prefs.end();
}
void rect(lv_draw_ctx_t *ctx,int x,int y,int w,int h,uint32_t color,int radius=0) {
    if(w<=0||h<=0)return;
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.bg_color=lv_color_hex(color);d.radius=radius;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+h-1)};
    lv_draw_rect(ctx,&d,&a);
}
void line(lv_draw_ctx_t *ctx,int x1,int y1,int x2,int y2,uint32_t color,int width=2) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=lv_color_hex(color);d.width=width;d.round_start=d.round_end=true;
    lv_point_t p={(lv_coord_t)x1,(lv_coord_t)y1},q={(lv_coord_t)x2,(lv_coord_t)y2};lv_draw_line(ctx,&d,&p,&q);
}
void triangle(lv_draw_ctx_t *ctx,int x1,int y1,int x2,int y2,int x3,int y3,uint32_t color) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.bg_color=lv_color_hex(color);
    lv_point_t p[]={{(lv_coord_t)x1,(lv_coord_t)y1},{(lv_coord_t)x2,(lv_coord_t)y2},{(lv_coord_t)x3,(lv_coord_t)y3}};
    lv_draw_polygon(ctx,&d,p,3);
}
uint32_t soften(uint32_t color,bool blocked) {
    if(!blocked)return color;
    const uint32_t r=(((color>>16)&255)+2*((Bg>>16)&255))/3;
    const uint32_t g=(((color>>8)&255)+2*((Bg>>8)&255))/3;
    const uint32_t b=((color&255)+2*(Bg&255))/3;
    return (r<<16)|(g<<8)|b;
}
void symbol(lv_draw_ctx_t *ctx,int x,int y,uint8_t type,bool blocked=false) {
    const auto green=soften(0x66915B,blocked),dark=soften(Ink,blocked),cream=soften(0xFFF7DF,blocked);
    switch(type%TypeCount) {
        case 0: // carrot
            triangle(ctx,x-7,y-5,x+7,y-5,x-2,y+11,soften(0xDE9151,blocked));
            line(ctx,x,y-5,x-4,y-12,green,3);line(ctx,x,y-5,x+5,y-12,green,3);
            line(ctx,x-3,y,x+2,y+1,cream,1);break;
        case 1: // leaf
            rect(ctx,x-9,y-9,18,17,green,9);line(ctx,x-6,y+9,x+6,y-6,cream,2);
            line(ctx,x-2,y+4,x-5,y-2,cream,1);break;
        case 2: // flower
            for(int i=0;i<4;++i)rect(ctx,x-5+(i==1?6:i==3?-6:0),y-5+(i==0?-6:i==2?6:0),10,10,soften(0xCC8D9C,blocked),5);
            rect(ctx,x-4,y-4,8,8,soften(0xE5BF68,blocked),4);break;
        case 3: // apple
            rect(ctx,x-9,y-6,18,17,soften(0xC97767,blocked),7);
            rect(ctx,x+1,y-12,8,5,green,3);line(ctx,x,y-7,x,y-11,dark,2);
            rect(ctx,x-5,y-3,2,5,cream,1);break;
        case 4: // mushroom
            rect(ctx,x-4,y-1,8,12,soften(0xD8BC8D,blocked),2);
            rect(ctx,x-11,y-10,22,14,soften(0xB47769,blocked),9);
            rect(ctx,x-6,y-6,4,4,cream,2);rect(ctx,x+3,y-4,3,3,cream,2);break;
        case 5: // sheep
            rect(ctx,x-10,y-7,19,16,cream,7);rect(ctx,x-7,y-10,11,12,cream,6);
            rect(ctx,x+3,y-4,10,12,dark,4);rect(ctx,x+7,y-2,2,2,cream,1);
            line(ctx,x-6,y+8,x-6,y+11,dark);line(ctx,x+3,y+8,x+3,y+11,dark);break;
        case 6: // bell
            rect(ctx,x-3,y-11,6,5,soften(0xB69552,blocked),3);
            rect(ctx,x-8,y-8,16,16,soften(0xD9B564,blocked),7);
            rect(ctx,x-10,y+5,20,4,soften(0xB69552,blocked),2);
            rect(ctx,x-2,y+9,4,3,dark,2);break;
        case 7: // water drop
            triangle(ctx,x,y-12,x-9,y+1,x+9,y+1,soften(0x79AFC0,blocked));
            rect(ctx,x-9,y-3,18,15,soften(0x79AFC0,blocked),8);
            line(ctx,x-4,y+1,x-4,y+5,cream,2);break;
        default: // wheat
            line(ctx,x,y+12,x,y-11,green,2);
            for(int i=0;i<3;++i){rect(ctx,x-8,y-8+i*6,7,4,soften(0xC5A25A,blocked),3);rect(ctx,x+1,y-11+i*6,7,4,soften(0xC5A25A,blocked),3);}break;
    }
}
void tile(lv_draw_ctx_t *ctx,int x,int y,uint8_t type,bool blocked,bool selected=false) {
    rect(ctx,x,y+3,32,32,blocked?0xC6CEBA:0x94AA83,5);
    rect(ctx,x,y,32,31,blocked?0xDDE3D2:selected?0xE5EFCF:White,5);
    rect(ctx,x+2,y+2,28,27,blocked?0xE5E9DB:selected?0xE5EFCF:0xFFFEF6,4);
    symbol(ctx,x+16,y+15,type,blocked);
}
void drawText(lv_draw_ctx_t *ctx,const char *value,int x,int y,int w,uint32_t color=Muted) {
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=F14;d.color=lv_color_hex(color);d.align=LV_TEXT_ALIGN_CENTER;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+F14->line_height+1)};
    lv_draw_label(ctx,&d,&a,value,nullptr);
}
void drawBoard(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    drawText(ctx,"第",a.x1+1,a.y1+18,30);drawText(ctx,game.level==1?"1":"2",a.x1+1,a.y1+36,30,Green);
    drawText(ctx,"关",a.x1+1,a.y1+54,30);drawText(ctx,"余",a.x1+1,a.y1+91,30);
    char remaining[8];snprintf(remaining,sizeof(remaining),"%u",(unsigned)game.remaining);
    drawText(ctx,remaining,a.x1+1,a.y1+109,30,Green);
    // Geometry is owned by the engine; paint low to high in the same order as
    // hit testing, including greyed lower cards which cannot yet be selected.
    for(unsigned layer=0;layer<3;++layer)for(unsigned i=0;i<game.tileCount;++i) {
        const auto &t=game.tiles[i];if(t.removed||t.layer!=layer)continue;
        tile(ctx,a.x1+t.x,a.y1+t.y,t.type,!game.exposed(i),touching&&pressedTile==(int)i);
    }
}
void drawTray(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    for(int i=0;i<TrayCapacity;++i) {
        const int x=a.x1+8+i*40,y=a.y1+2;
        rect(ctx,x,y,32,34,0xC3D1B3,5);rect(ctx,x+1,y+1,30,31,0xD9E3CD,4);
        if(i<game.trayCount)tile(ctx,x,y,game.tray[i],false);
    }
}
void drawIcon(lv_event_t *e) {
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    rect(ctx,a.x1,a.y1,56,56,Light,14);
    rect(ctx,a.x1+10,a.y1+14,36,30,0xFAF9E9,12);
    rect(ctx,a.x1+17,a.y1+9,22,20,0xFAF9E9,9);
    rect(ctx,a.x1+29,a.y1+24,18,23,Ink,8);
    rect(ctx,a.x1+38,a.y1+29,3,3,White,2);
    rect(ctx,a.x1+9,a.y1+32,11,7,0xFAF9E9,4);
    line(ctx,a.x1+20,a.y1+43,a.x1+20,a.y1+48,Ink,3);
    line(ctx,a.x1+35,a.y1+44,a.x1+35,a.y1+48,Ink,3);
}
void closeOverlay() {
    Ui::cancelAnimations();if(overlay)lv_obj_del(overlay);
    overlay=nullptr;overlayKind=-1;touching=false;pressedTile=-1;blockUntilRelease=pointerDown();
}
void openOverlay(int kind) {
    closeOverlay();overlay=box(pageRoot,0,0,320,240,Ink,0);
    lv_obj_set_style_bg_opa(overlay,135,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
    auto *card=box(overlay,28,36,264,168,White,14);
    if(kind==(int)State::Ready) {
        text(card,"羊了个羊",12,8,240,F16,Green,LV_TEXT_ALIGN_CENTER);
        button(card,"返回",204,4,48,28,Back);
        text(card,"点击未被遮挡的牌",8,34,248,F14,Ink,LV_TEXT_ALIGN_CENTER);
        text(card,"三张相同自动消除",8,54,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"七格放满则结束",8,74,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"每关可撤回、洗牌各一次",8,96,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        button(card,"开始游戏",16,128,232,32,Start,true);
    } else {
        const bool paused=kind==(int)State::Paused,clear=kind==(int)State::LevelCleared,won=kind==(int)State::Won;
        text(card,paused?"已暂停":clear?"第一关通过":won?"全部通关":"槽位已满",12,16,240,F16,Green,LV_TEXT_ALIGN_CENTER);
        text(card,String("得分 ")+String((unsigned long)game.score)+"   最高 "+String((unsigned long)game.best),8,48,248,F16,Ink,LV_TEXT_ALIGN_CENTER);
        if(clear||won)text(card,clear?"准备挑战第二关":"所有卡牌已消除",8,78,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        if(paused) {
            button(card,"继续",12,112,64,40,Resume,true);button(card,"重新开始",80,112,100,40,Restart);button(card,"返回",184,112,68,40,Back);
        } else if(!clear&&!won&&game.undoAvailable) {
            button(card,"撤回",12,112,64,40,Undo,true);button(card,"重新开始",80,112,100,40,Restart);button(card,"返回",184,112,68,40,Back);
        } else {
            button(card,clear?"下一关":"再来一局",16,112,136,40,clear?NextLevel:Restart,true);
            button(card,"返回",160,112,88,40,Back);
        }
    }
    overlayKind=kind;
}
void setEnabled(lv_obj_t *key,bool enabled) {
    if(enabled)lv_obj_clear_state(key,LV_STATE_DISABLED);else lv_obj_add_state(key,LV_STATE_DISABLED);
    lv_obj_set_style_opa(key,enabled?LV_OPA_COVER:100,0);
}
void apply() {
    if(!pageRoot)return;
    if(shownScore!=game.score){lv_label_set_text_fmt(scoreLabel,"分 %lu",(unsigned long)game.score);shownScore=game.score;}
    if(shownBest!=game.best){lv_label_set_text_fmt(bestLabel,"最高 %lu",(unsigned long)game.best);shownBest=game.best;}
    setEnabled(undoKey,game.state==State::Playing&&game.undoAvailable);
    setEnabled(shuffleKey,game.state==State::Playing&&game.shuffleAvailable&&game.remaining>=2);
    if(game.state==State::Playing){if(overlay)closeOverlay();}
    else if(overlayKind!=(int)game.state)openOverlay((int)game.state);
    lv_obj_invalidate(board);lv_obj_invalidate(trayView);
}
void event(lv_event_t *e) {
    const int command=(int)(intptr_t)lv_event_get_user_data(e);lastInputAt=millis();
    switch(command) {
        case Back:{game.pause();persistBest();lv_event_t forwarded=*e;forwarded.user_data=(void *)(uintptr_t)Ui::Back;Ui::dispatchAction(&forwarded);return;}
        case Pause:game.pause();persistBest();break;
        case Resume:game.resume();break;
        case Start:game.start(millis()*2654435761u+0x9E3779B9u);break;
        case Restart:persistBest();game.restart();break;
        case NextLevel:game.nextLevel();break;
        case Undo:game.undo();break;
        case Shuffle:game.shuffle();break;
        default:return;
    }
    touching=false;pressedTile=-1;blockUntilRelease=pointerDown();apply();
}
void touchBoard(lv_event_t *e) {
    const auto code=lv_event_get_code(e);
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_RELEASED&&code!=LV_EVENT_PRESS_LOST)return;
    auto *input=lv_indev_get_act();if(!input)return;
    lv_point_t p;lv_indev_get_point(input,&p);lv_area_t a;lv_obj_get_coords(board,&a);
    if(code==LV_EVENT_PRESSED) {
        if(blockUntilRelease||game.state!=State::Playing)return;
        pressPoint=p;pressedTile=game.hitTest(p.x-a.x1,p.y-a.y1);
        touching=pressedTile>=0&&game.canSelect(pressedTile);lv_obj_invalidate(board);
    } else if(code==LV_EVENT_RELEASED) {
        const int hit=game.hitTest(p.x-a.x1,p.y-a.y1);
        const bool select=touching&&!blockUntilRelease&&hit==pressedTile&&abs(p.x-pressPoint.x)<=10&&abs(p.y-pressPoint.y)<=10;
        touching=false;blockUntilRelease=false;
        if(select&&game.select(pressedTile)) {
            lastInputAt=millis();if(game.state!=State::Playing)persistBest();
        }
        pressedTile=-1;apply();
    } else {touching=false;pressedTile=-1;lv_obj_invalidate(board);}
}
void onTick(lv_timer_t *) {if(blockUntilRelease&&!pointerDown())blockUntilRelease=false;}
} // namespace

void createIcon(lv_obj_t *parent,int x,int y) {
    auto *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,56,56);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(obj,drawIcon,LV_EVENT_DRAW_MAIN,nullptr);
}
void build(lv_obj_t *parent) {
    loadBest();if(!prepared){game.prepare(millis()*2654435761u+0x9E3779B9u);prepared=true;}
    pageRoot=parent;board=trayView=overlay=scoreLabel=bestLabel=undoKey=shuffleKey=nullptr;overlayKind=-1;
    shownScore=shownBest=UINT32_MAX;touching=false;pressedTile=-1;blockUntilRelease=pointerDown();
    lv_obj_set_style_bg_color(parent,lv_color_hex(Bg),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);lv_obj_clear_flag(parent,LV_OBJ_FLAG_SCROLLABLE);
    button(parent,"返回",0,0,40,32,Back);text(parent,"羊了个羊",45,6,90,F16);
    scoreLabel=text(parent,"",140,8,54,F14,Green);bestLabel=text(parent,"",200,8,56,F14,Muted);button(parent,"暂停",260,0,60,32,Pause);
    board=box(parent,0,32,320,142,Bg,0);lv_obj_add_flag(board,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);lv_obj_clear_flag(board,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(board,drawBoard,LV_EVENT_DRAW_MAIN,nullptr);lv_obj_add_event_cb(board,touchBoard,LV_EVENT_ALL,nullptr);
    trayView=box(parent,12,174,296,38,Light,7);lv_obj_add_event_cb(trayView,drawTray,LV_EVENT_DRAW_MAIN,nullptr);
    undoKey=button(parent,"撤回",10,214,88,26,Undo);shuffleKey=button(parent,"洗牌",110,214,100,26,Shuffle);button(parent,"重开",222,214,88,26,Restart);
    if(timer)lv_timer_del(timer);timer=lv_timer_create(onTick,20,nullptr);apply();
}
void refresh(bool force){if(force)apply();}
void cancel() {
    if(timer){lv_timer_del(timer);timer=nullptr;}
    if(pageRoot){if(app!=App::Sheep)game.pause();persistBest();}
    pageRoot=board=trayView=overlay=scoreLabel=bestLabel=undoKey=shuffleKey=nullptr;overlayKind=-1;
    touching=false;pressedTile=-1;blockUntilRelease=pointerDown();
}
bool back(){return false;}
bool canBack(){return false;}
bool inputActive(){return touching||blockUntilRelease;}
Status status(){return {stateName(game.state),game.score,game.best,game.remaining,game.level,game.trayCount,game.undoAvailable,game.shuffleAvailable};}
uint32_t bestScore(){loadBest();return game.best>storedBest?game.best:storedBest;}
#ifdef GIFT_HOST_TEST
const SheepEngine::Game &debugGame(){return game;}
void debugVariant(int which) {
    const auto best=game.best;game=Game{};game.best=best;game.prepare(20261113u);prepared=true;loadBest();
    if(which){game.start(20261113u);if(which==2)game.pause();
        else if(which==3)game.state=State::GameOver;
        else if(which==4)game.state=State::LevelCleared;
        else if(which==5)game.state=State::Won;}
    touching=false;pressedTile=-1;blockUntilRelease=pointerDown();apply();
}
#endif
} // namespace SheepPage
