#include "mini_apps.h"
#include "mines_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

namespace MinesPage {
namespace {
using namespace MinesEngine;
constexpr uint32_t Bg=0xEDF1E6,Ink=0x304C43,Muted=0x788B7B,Green=0x466D59,Light=0xDCE7D8,White=0xFFFEF5;
const lv_font_t *F14=&fd_sans_bold_14_ui,*F16=&fd_sans_bold_16_ui;
enum Command{Back=1,Pause,Resume,Restart,Start,Mode};
Game game;
lv_obj_t *pageRoot=nullptr,*stage=nullptr,*overlay=nullptr,*scoreLabel=nullptr,*bestLabel=nullptr,*modeLabel=nullptr;
lv_timer_t *timer=nullptr;
bool prepared=false,loaded=false,resumeRebuild=false,blockUntilRelease=false,touching=false,longHandled=false,dragCancelled=false;
uint32_t storedBest=0,shownScore=UINT32_MAX,shownBest=UINT32_MAX,pressedAt=0;
int overlayKind=-1,pressedCell=-1;
lv_point_t pressPoint{};
const char *stateName(State state){switch(state){case State::Ready:return "ready";case State::Playing:return "playing";case State::Paused:return "paused";case State::Won:return "won";default:return "gameover";}}
bool pointerDown(){for(auto *input=lv_indev_get_next(nullptr);input;input=lv_indev_get_next(input))if(input->driver->type==LV_INDEV_TYPE_POINTER&&input->proc.state==LV_INDEV_STATE_PR)return true;return false;}
void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius=8){
    auto *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_radius(obj,radius,0);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return obj;
}
lv_obj_t *text(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font=nullptr,uint32_t color=Ink,lv_text_align_t align=LV_TEXT_ALIGN_LEFT){
    if(!font)font=F14;auto *obj=lv_label_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,font->line_height+2);
    lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);lv_obj_set_style_text_align(obj,align,0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);lv_label_set_text(obj,value.c_str());lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);return obj;
}
lv_obj_t *button(lv_obj_t *parent,const char *value,int x,int y,int w,int h,int command,bool primary=false){
    auto *obj=box(parent,x,y,w,h,primary?Green:Light);lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(obj,event,LV_EVENT_CLICKED,(void *)(intptr_t)command);
    if(command!=Mode)Ui::attachPressFeedback(obj);
    const auto *font=h<30?F14:F16;text(obj,value,2,(h-font->line_height)/2,w-4,font,primary?White:Ink,LV_TEXT_ALIGN_CENTER);return obj;
}
void loadBest(){
    if(loaded)return;Preferences prefs;if(!prefs.begin("mines",false))return;
    if(!prefs.isKey("best"))loaded=true;
    else{const uint32_t value=prefs.getUInt("best",UINT32_MAX);if(value!=UINT32_MAX){storedBest=value<=MaximumScore?value:0;loaded=true;}}
    prefs.end();if(loaded&&storedBest>game.best)game.best=storedBest;
}
void persistBest(){
    loadBest();if(!loaded||game.best>MaximumScore||game.best<=storedBest)return;Preferences prefs;if(!prefs.begin("mines",false))return;
    if(prefs.putUInt("best",game.best)==sizeof(uint32_t))storedBest=game.best;prefs.end();
}
void rect(lv_draw_ctx_t *ctx,int x,int y,int w,int h,uint32_t color,int radius=0){
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;d.bg_color=lv_color_hex(color);d.radius=radius;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+h-1)};lv_draw_rect(ctx,&d,&a);
}
void line(lv_draw_ctx_t *ctx,int x1,int y1,int x2,int y2,uint32_t color,int width=2){
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=lv_color_hex(color);d.width=width;d.round_start=d.round_end=true;
    lv_point_t a={(lv_coord_t)x1,(lv_coord_t)y1},b={(lv_coord_t)x2,(lv_coord_t)y2};lv_draw_line(ctx,&d,&a,&b);
}
void flag(lv_draw_ctx_t *ctx,int x,int y){
    line(ctx,x-4,y-8,x-4,y+8,Ink,2);line(ctx,x-8,y+9,x+2,y+9,Ink,2);
    lv_point_t p[]={{(lv_coord_t)(x-3),(lv_coord_t)(y-8)},{(lv_coord_t)(x+7),(lv_coord_t)(y-4)},{(lv_coord_t)(x-3),(lv_coord_t)y}};
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(0xC88364);d.bg_opa=LV_OPA_COVER;lv_draw_polygon(ctx,&d,p,3);
}
void mine(lv_draw_ctx_t *ctx,int x,int y){
    for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)if(dx||dy)line(ctx,x+dx*4,y+dy*4,x+dx*9,y+dy*9,Ink,2);
    rect(ctx,x-6,y-6,13,13,Ink,6);rect(ctx,x-3,y-4,3,3,White,1);
}
void drawText(lv_draw_ctx_t *ctx,const char *value,int x,int y,int w,uint32_t color=Ink,const lv_font_t *font=nullptr,lv_text_align_t align=LV_TEXT_ALIGN_CENTER){
    if(!font)font=F14;lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=font;d.color=lv_color_hex(color);d.align=align;
    lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+font->line_height+1)};lv_draw_label(ctx,&d,&a,value,nullptr);
}
void drawStage(lv_event_t *e){
    auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);const int ox=a.x1,oy=a.y1;
    char value[40];snprintf(value,sizeof(value),"%lu秒",(unsigned long)(game.elapsedMs/1000));drawText(ctx,value,ox+100,oy+7,93,Muted);
    snprintf(value,sizeof(value),"余雷 %u",(unsigned)(MineCount-game.flags));drawText(ctx,value,ox+205,oy+7,103,Green);
    rect(ctx,ox+BoardX-2,oy+BoardY-2,Cols*CellSize+4,Rows*CellSize+4,0xC6D3C0,5);
    static const uint32_t numbers[]={Ink,0x487A9E,0x527549,0xB46C54,0x7D6694,0x9A6747,0x3F8584,Ink,Ink};
    for(int id=0;id<Cells;++id){
        const auto &cell=game.cells[id];const int x=ox+BoardX+(id%Cols)*CellSize,y=oy+BoardY+(id/Cols)*CellSize;
        const bool showMine=cell.mine&&(cell.revealed||game.state==State::GameOver);
        uint32_t color=cell.revealed?0xF8F8EE:0xB6CBAA;
        if(showMine)color=id==game.exploded?0xE5AD92:0xDFDDD0;
        if(touching&&!dragCancelled&&pressedCell==id&&!longHandled)color=0x94B48A;
        rect(ctx,x+1,y+1,26,26,color,3);
        if(!cell.revealed&&!showMine)rect(ctx,x+3,y+2,22,2,0xCFDEC4,1);
        if(showMine)mine(ctx,x+14,y+14);
        else if(cell.flagged){flag(ctx,x+14,y+13);if(game.state==State::GameOver&&!cell.mine){line(ctx,x+4,y+4,x+24,y+24,0xB46C54,2);line(ctx,x+24,y+4,x+4,y+24,0xB46C54,2);}}
        else if(cell.revealed&&cell.adjacent){char n[2]={(char)('0'+cell.adjacent),0};drawText(ctx,n,x+1,y+4,26,numbers[cell.adjacent],F16);}
    }
}
void drawIcon(lv_event_t *e){auto *ctx=lv_event_get_draw_ctx(e);lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);rect(ctx,a.x1,a.y1,56,56,Light,14);rect(ctx,a.x1+8,a.y1+10,18,18,0xADC6A2,3);rect(ctx,a.x1+29,a.y1+10,18,18,White,3);rect(ctx,a.x1+8,a.y1+31,18,18,White,3);rect(ctx,a.x1+29,a.y1+31,18,18,0xADC6A2,3);flag(ctx,a.x1+37,a.y1+38);drawText(ctx,"1",a.x1+8,a.y1+31,18,0x487A9E,F16);}
void resetContact(){touching=false;pressedCell=-1;longHandled=false;dragCancelled=false;blockUntilRelease=pointerDown();}
void closeOverlay(){Ui::cancelAnimations();if(overlay)lv_obj_del(overlay);overlay=nullptr;overlayKind=-1;resetContact();}
void openOverlay(int kind){
    closeOverlay();overlay=box(pageRoot,0,0,320,240,Ink,0);lv_obj_set_style_bg_opa(overlay,140,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);
    auto *card=box(overlay,28,36,264,168,White,14);
    if(kind==(int)State::Ready){
        text(card,"扫雷",12,8,240,F16,Green,LV_TEXT_ALIGN_CENTER);button(card,"返回",204,4,48,28,Back);
        text(card,"首点周围安全",8,34,248,F14,Ink,LV_TEXT_ALIGN_CENTER);text(card,"点击翻开，长按插旗",8,54,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        text(card,"也可切换标记模式",8,74,248,F14,Muted,LV_TEXT_ALIGN_CENTER);text(card,"翻开所有安全格获胜",8,96,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        button(card,"开始游戏",16,128,232,32,Start,true);
    }else{
        const bool paused=kind==(int)State::Paused,won=kind==(int)State::Won;
        text(card,paused?"已暂停":won?"全部通关":"踩到地雷",12,16,240,F16,Green,LV_TEXT_ALIGN_CENTER);
        text(card,String("得分 ")+String((unsigned long)game.score)+"   最高 "+String((unsigned long)game.best),8,48,248,F16,Ink,LV_TEXT_ALIGN_CENTER);
        if(won)text(card,"所有安全格已翻开",8,78,248,F14,Muted,LV_TEXT_ALIGN_CENTER);
        if(paused){button(card,"继续",12,112,64,40,Resume,true);button(card,"重新开始",80,112,100,40,Restart);button(card,"返回",184,112,68,40,Back);}
        else{button(card,"再来一局",16,112,136,40,Restart,true);button(card,"返回",160,112,88,40,Back);}
    }
    overlayKind=kind;
}
void apply(){
    if(!pageRoot)return;
    if(shownScore!=game.score){lv_label_set_text_fmt(scoreLabel,"分 %lu",(unsigned long)game.score);shownScore=game.score;}
    if(shownBest!=game.best){lv_label_set_text_fmt(bestLabel,"高 %lu",(unsigned long)game.best);shownBest=game.best;}
    lv_label_set_text(modeLabel,game.flagMode?"标记开":"标记关");
    lv_obj_set_style_bg_color(lv_obj_get_parent(modeLabel),lv_color_hex(game.flagMode?Green:Light),0);
    lv_obj_set_style_text_color(modeLabel,lv_color_hex(game.flagMode?White:Ink),0);
    if(game.state==State::Playing){if(overlay)closeOverlay();}else if(overlayKind!=(int)game.state)openOverlay((int)game.state);
    if(stage)lv_obj_invalidate(stage);
}
void event(lv_event_t *e){
    const int command=(int)(intptr_t)lv_event_get_user_data(e);lastInputAt=millis();
    switch(command){
        case Back:{game.pause(millis());persistBest();lv_event_t forwarded=*e;forwarded.user_data=(void *)(uintptr_t)Ui::Back;Ui::dispatchAction(&forwarded);return;}
        case Pause:game.pause(millis());persistBest();break;case Resume:game.resume(millis());break;
        case Restart:persistBest();game.restart(millis());break;case Start:game.start(millis(),millis()*2654435761u+0x9E3779B9u);break;
        case Mode:game.toggleMode();break;default:return;
    }
    resetContact();apply();
}
void touchStage(lv_event_t *e){
    const auto code=lv_event_get_code(e);
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_RELEASED&&code!=LV_EVENT_PRESS_LOST)return;
    auto *input=lv_indev_get_act();if(!input)return;lv_point_t p;lv_indev_get_point(input,&p);lv_area_t a;lv_obj_get_coords(stage,&a);
    const int hit=game.hitTest(p.x-a.x1,p.y-a.y1);
    if(code==LV_EVENT_PRESSED){
        if(blockUntilRelease||game.state!=State::Playing)return;
        pressedCell=hit;touching=hit>=0;longHandled=false;dragCancelled=false;pressedAt=millis();pressPoint=p;if(touching)lv_obj_invalidate(stage);
    }else if(code==LV_EVENT_PRESSING){
        if(!touching)return;
        if(abs(p.x-pressPoint.x)>10||abs(p.y-pressPoint.y)>10||hit!=pressedCell)dragCancelled=true;
        if(!blockUntilRelease&&!dragCancelled&&!longHandled&&millis()-pressedAt>=500){
            longHandled=true;game.toggleFlag(pressedCell,millis());lastInputAt=millis();apply();
        }
    }else{
        const bool select=code==LV_EVENT_RELEASED&&touching&&!blockUntilRelease&&!longHandled&&!dragCancelled&&hit==pressedCell&&abs(p.x-pressPoint.x)<=10&&abs(p.y-pressPoint.y)<=10;
        // A release can be the first sampled input after the 500ms threshold;
        // classify that hold here too instead of revealing an intended flag.
        if(select){if(game.flagMode||millis()-pressedAt>=500)game.toggleFlag(pressedCell,millis());else game.reveal(pressedCell,millis());lastInputAt=millis();}
        touching=false;pressedCell=-1;if(code==LV_EVENT_RELEASED)blockUntilRelease=false;
        if(game.state==State::Won||game.state==State::GameOver)persistBest();apply();
    }
}
void onTick(lv_timer_t *){
    if(!pageRoot)return;if(blockUntilRelease&&!pointerDown())blockUntilRelease=false;
    const uint32_t before=game.elapsedMs/1000;game.tick(millis());if(before!=game.elapsedMs/1000)apply();
}
} // namespace
void createIcon(lv_obj_t *parent,int x,int y){auto *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,56,56);lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(obj,drawIcon,LV_EVENT_DRAW_MAIN,nullptr);}
void build(lv_obj_t *parent){
    loadBest();if(!prepared){game.prepare(millis()*2654435761u+0x9E3779B9u);prepared=true;}
    if(resumeRebuild){game.resume(millis());resumeRebuild=false;}
    pageRoot=parent;stage=overlay=scoreLabel=bestLabel=modeLabel=nullptr;overlayKind=-1;shownScore=shownBest=UINT32_MAX;resetContact();
    lv_obj_set_style_bg_color(parent,lv_color_hex(Bg),0);lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);lv_obj_clear_flag(parent,LV_OBJ_FLAG_SCROLLABLE);
    button(parent,"返回",0,0,40,32,Back);text(parent,"扫雷",45,6,90,F16);scoreLabel=text(parent,"",140,8,54,F14,Green);bestLabel=text(parent,"",200,8,56,F14,Muted);button(parent,"暂停",260,0,60,32,Pause);
    stage=box(parent,0,32,320,208,Bg,0);lv_obj_add_flag(stage,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_PRESS_LOCK);lv_obj_clear_flag(stage,LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(stage,drawStage,LV_EVENT_DRAW_MAIN,nullptr);lv_obj_add_event_cb(stage,touchStage,LV_EVENT_ALL,nullptr);
    auto *mode=button(parent,"",8,35,84,26,Mode);modeLabel=lv_obj_get_child(mode,0);
    if(timer)lv_timer_del(timer);timer=lv_timer_create(onTick,40,nullptr);apply();
}
void refresh(bool force){if(force)apply();}
void cancel(){
    if(timer){lv_timer_del(timer);timer=nullptr;}
    // Root may already have cleaned its LVGL children before a repeated leave
    // callback. Never dereference cached widgets during cancellation.
    if(pageRoot){const bool pausedNow=game.pause(millis());resumeRebuild=app==App::Mines&&pausedNow;persistBest();}
    else if(app!=App::Mines)resumeRebuild=false;
    pageRoot=stage=overlay=scoreLabel=bestLabel=modeLabel=nullptr;overlayKind=-1;resetContact();
}
bool back(){return false;}bool canBack(){return false;}bool inputActive(){return touching||blockUntilRelease;}
Status status(){return {stateName(game.state),game.score,game.best,game.elapsedMs,game.remaining,game.flags};}
uint32_t bestScore(){loadBest();return game.best>storedBest?game.best:storedBest;}
#ifdef GIFT_HOST_TEST
const MinesEngine::Game &debugGame(){return game;}
void debugVariant(int which){
    const auto best=game.best;game=Game();game.best=best;game.prepare(20261113u);prepared=true;resumeRebuild=false;loadBest();
    if(which){
        game.start(millis(),20261113u);
        if(which==2)game.pause(millis());
        else if(which>=3){
            game.reveal(25,millis());
            if(which==3){for(int id=0;id<Cells;++id)if(game.cells[id].mine){game.reveal(id,millis());break;}}
            else if(which==4){for(int id=0;id<Cells;++id)if(!game.cells[id].mine)game.reveal(id,millis());}
            else if(which==5){for(int id=0;id<Cells;++id)if(game.cells[id].mine){game.toggleFlag(id,millis());break;}}
        }
    }
    resetContact();apply();
}
#endif
} // namespace MinesPage
