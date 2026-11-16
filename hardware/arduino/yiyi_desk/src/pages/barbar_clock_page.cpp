// Native LVGL adaptation of aednlaxer/flutter-clock-challenge-barbar.
// Geometry, colors and notices: assets/barbar_clock_sources.md.
#include "mini_apps.h"
#include "assets/barbar_clock_digits.h"
#include "assets/barbar_clock_oswald14.h"
#include "assets/barbar_clock_weather16.h"
#include "../app_state.h"
#include "../clock_source.h"
#include "../gift_data.h"
#include "../touch.h"
#include "../ui_widgets.h"
#include "../../desk_logic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace BarbarClockPage {
namespace {
constexpr uint32_t Background=0x090042,Ink=0x00c2b7;
constexpr unsigned DigitMs=300,LayoutMs=360,DoubleTapMs=320;
constexpr int Starts[]={4,11,22,29,40,47};
struct Interval {
    float start,end;
    Interval(float first=0,float last=0):start(first),end(last){}
};
struct Column {Interval bars[4];};
struct Digit {
    Column from[6],to[6],current[6];
    int target=-2;
    uint32_t started=0;
    bool moving=false;
};
Digit digits[6];
lv_obj_t *view=nullptr,*temperature=nullptr,*date=nullptr,*weatherIcon=nullptr;
lv_timer_t *timer=nullptr;
bool seconds=true,saveFailed=false,pendingTap=false,initialized=false,timeWasValid=false;
bool layoutMoving=false;
float layout=1,layoutFrom=1,layoutTo=1;
uint32_t tappedAt=0,layoutAt=0,lastRevision=0;
int64_t lastEpoch=-1;

float ease(uint32_t elapsed,unsigned duration){
    if(elapsed>=duration)return 1;
    float remaining=1.f-float(elapsed)/duration;return 1-remaining*remaining;
}
Column shape(int digit,int column){
    Column result;
    result.bars[0]={0,0};result.bars[3]={128,128};
    result.bars[1]=result.bars[2]={64,64};
    if(digit<0){result.bars[0]={0,56};result.bars[3]={72,128};return result;}
    int middle=1;
    for(unsigned i=0;i<barbar_clock_digit_counts[digit][column];i++){
        auto source=barbar_clock_digit_segments[digit][column][i];
        Interval interval={float(source.start),float(source.end)};
        if(source.start==0)result.bars[0]=interval;
        else if(source.end==128)result.bars[3]=interval;
        else result.bars[middle++]=interval;
    }
    // Split a single middle bar into two coincident bars, so transitions from
    // two middles converge continuously. They are merged before drawing.
    if(middle==2)result.bars[2]=result.bars[1];
    return result;
}
bool sample(uint32_t now){
    bool changed=false;
    for(auto &digit:digits)if(digit.moving){
        float progress=ease(now-digit.started,DigitMs);
        for(int c=0;c<6;c++)for(int b=0;b<4;b++){
            auto &a=digit.from[c].bars[b];auto &z=digit.to[c].bars[b];
            digit.current[c].bars[b]={a.start+(z.start-a.start)*progress,a.end+(z.end-a.end)*progress};
        }
        if(progress==1)digit.moving=false;
        changed=true;
    }
    if(layoutMoving){
        float progress=ease(now-layoutAt,LayoutMs);layout=layoutFrom+(layoutTo-layoutFrom)*progress;
        if(progress==1)layoutMoving=false;
        changed=true;
    }
    return changed;
}
void target(Digit &digit,int value,bool animate,uint32_t now){
    if(digit.target==value)return;
    digit.target=value;digit.started=now;digit.moving=animate;
    for(int c=0;c<6;c++){
        digit.from[c]=digit.current[c];digit.to[c]=shape(value,c);
        if(!animate)digit.current[c]=digit.to[c];
    }
}
void changeLayout(bool shown,uint32_t now){
    sample(now);seconds=shown;layoutFrom=layout;layoutTo=shown?1.f:0.f;
    layoutAt=now;layoutMoving=layoutFrom!=layoutTo;lv_obj_invalidate(view);
}
bool setText(lv_obj_t *label,const char *value){
    if(!strcmp(lv_label_get_text(label),value))return false;
    lv_label_set_text(label,value);return true;
}
void footer(bool valid,const DeskLogic::Civil &civil){
    char value[40];
    if(valid){
        const char *days[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
        const char *months[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
        int weekday=int((DeskLogic::civilDays(civil.year,civil.month,civil.day)+4)%7);if(weekday<0)weekday+=7;
        snprintf(value,sizeof(value),"%s, %s %02d",days[weekday],months[civil.month-1],civil.day);
    }else snprintf(value,sizeof(value),"--, --- --");
    if(setText(date,value)){
        lv_obj_update_layout(date);lv_obj_set_x(date,305-lv_obj_get_width(date));
    }
    if(weather.valid)snprintf(value,sizeof(value),"%.0f°C",double(weather.temperature));
    else snprintf(value,sizeof(value),"--°C");
    setText(temperature,value);
    const char *glyph="";
    if(weather.valid){
        // Original weather_icon.dart codepoints, rendered from its bundled
        // Weather Icons Lite font. The provider supplies Chinese conditions.
        if(weather.condition.indexOf("雷")>=0)glyph="\xef\x80\x9e"; // f01e
        else if(weather.condition.indexOf("雪")>=0)glyph="\xef\x80\x9b"; // f01b
        else if(weather.condition.indexOf("雨")>=0)glyph="\xef\x80\x99"; // f019
        else if(weather.condition.indexOf("雾")>=0||weather.condition.indexOf("霾")>=0)glyph="\xef\x81\x8a"; // f04a
        else if(weather.condition.indexOf("风")>=0||weather.condition.indexOf("沙")>=0||weather.condition.indexOf("尘")>=0)glyph="\xef\x81\x90"; // f050
        else if(weather.condition.indexOf("云")>=0||weather.condition.indexOf("阴")>=0)glyph="\xef\x80\x93"; // f013
        else if(weather.condition.indexOf("晴")>=0)glyph=valid&&(civil.hour>=22||civil.hour<6)?"\xef\x80\xae":"\xef\x80\x8d"; // f02e / f00d
    }
    setText(weatherIcon,glyph);
    if(*glyph)lv_obj_clear_flag(weatherIcon,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(weatherIcon,LV_OBJ_FLAG_HIDDEN);
}
// Interior endpoints retain the original 28% top/bottom margins. A zero-length
// interval never acquires visible height when an endpoint touches an edge.
float endpoint(float y,int height){return height*(.28f+.44f*y/128.f);}
void rectangle(lv_draw_ctx_t *ctx,int x1,int y1,int x2,int y2,lv_opa_t opacity=255,int radius=0){
    if(x2<x1||y2<y1||!opacity)return;
    lv_draw_rect_dsc_t descriptor;lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color=lv_color_hex(Ink);descriptor.bg_opa=opacity;descriptor.radius=radius;
    lv_area_t area={(lv_coord_t)x1,(lv_coord_t)y1,(lv_coord_t)x2,(lv_coord_t)y2};
    lv_draw_rect(ctx,&descriptor,&area);
}
void paintBars(lv_draw_ctx_t *ctx,const lv_area_t &area,float amount,bool preview){
    const int width=area.x2-area.x1+1,height=area.y2-area.y1+1;
    float pitch=width/(39.6f+18*amount);
    for(int column=0;column<57;column++){
        bool extra=column>=35&&column<=52;
        float strength=extra?amount:1;
        if(strength<.01f)continue;
        float index=column<35?column:column<=52?35+(column-35)*amount:35+18*amount+(column-53);
        float x=area.x1+pitch*(.6f+index),barWidth=pitch*.4f*strength;
        int left=int(lroundf(x)),right=int(lroundf(x+barWidth))-1;
        if(right<left)right=left;
        Column full;full.bars[0]={0,128};
        const Column *geometry=&full;
        Column fixed;
        for(int d=0;d<6;d++)if(column>=Starts[d]&&column<Starts[d]+6){
            if(preview){const int time[]={1,0,2,4,3,6};fixed=shape(time[d],column-Starts[d]);geometry=&fixed;}
            else geometry=&digits[d].current[column-Starts[d]];
            break;
        }
        if(column==19||column==37){
            fixed=Column{};fixed.bars[0]={0,38.4f};fixed.bars[1]={51.2f,76.8f};fixed.bars[3]={89.6f,128};geometry=&fixed;
        }
        // Sort and merge overlapping intervals (e.g. the paired middle bars).
        // This keeps antialiased ends from being darkened by drawing twice.
        Interval intervals[4];int count=0;
        for(auto segment:geometry->bars)if(segment.end-segment.start>.01f)intervals[count++]=segment;
        for(int i=1;i<count;i++)for(int j=i;j>0&&intervals[j].start<intervals[j-1].start;j--){auto swap=intervals[j];intervals[j]=intervals[j-1];intervals[j-1]=swap;}
        int merged=0;
        for(int i=0;i<count;i++){
            if(merged&&intervals[i].start<=intervals[merged-1].end+.01f){
                if(intervals[i].end>intervals[merged-1].end)intervals[merged-1].end=intervals[i].end;
            }else intervals[merged++]=intervals[i];
        }
        for(int i=0;i<merged;i++){
            auto interval=intervals[i];
            int top=interval.start<=.01f?0:int(lroundf(endpoint(interval.start,height)));
            int bottom=interval.end>=127.99f?height:int(lroundf(endpoint(interval.end,height)));
            rectangle(ctx,left,area.y1+top,right,area.y1+bottom-1,(lv_opa_t)lroundf(strength*255),(right-left+1)/2);
        }
    }
}
void draw(lv_event_t *event){
    auto *ctx=lv_event_get_draw_ctx(event);lv_area_t area;lv_obj_get_coords(lv_event_get_target(event),&area);
    bool preview=lv_event_get_user_data(event)!=nullptr;
    paintBars(ctx,area,preview?0:layout,preview);
}
void clicked(lv_event_t *){
    if(Touch::swiped()){pendingTap=false;return;}
    if(auto *input=lv_indev_get_act()){
        lv_point_t point;lv_indev_get_point(input,&point);
        if(point.x<20){pendingTap=false;return;}
    }
    uint32_t now=lv_tick_get();lastInputAt=millis();
    if(!pendingTap||now-tappedAt>=DoubleTapMs){pendingTap=true;tappedAt=now;return;}
    pendingTap=false;JsonDocument patch;patch["clockMode"]["barbarSeconds"]=!seconds;String error;
    if(!GiftData::patch(patch.as<JsonVariantConst>(),error)){saveFailed=true;notifyUser(error,true);return;}
    saveFailed=false;lastRevision=GiftData::revision();changeLayout(!seconds,now);
}
void deleted(lv_event_t *){
    cancel();view=temperature=date=weatherIcon=nullptr;initialized=false;layoutMoving=false;
    for(auto &digit:digits)digit=Digit{};
}
void tick(lv_timer_t *){
    refresh();uint32_t now=lv_tick_get();
    if(pendingTap&&now-tappedAt>=DoubleTapMs)pendingTap=false;
    if(sample(now))lv_obj_invalidate(view);
}
lv_obj_t *surface(lv_obj_t *parent,int width,int height){
    auto *object=lv_obj_create(parent);lv_obj_remove_style_all(object);
    lv_obj_set_size(object,width,height);lv_obj_set_pos(object,0,0);
    lv_obj_set_style_bg_color(object,lv_color_hex(Background),0);lv_obj_set_style_bg_opa(object,255,0);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);return object;
}
}
bool active(){return view!=nullptr;}
bool showSeconds(){return view?seconds:bool(GiftData::get("clockMode")["barbarSeconds"]|true);}
bool storageFailed(){return saveFailed;}
void cancel(){if(timer){lv_timer_del(timer);timer=nullptr;}pendingTap=false;}
void createPreview(lv_obj_t *parent){
    auto *preview=surface(parent,84,44);lv_obj_add_event_cb(preview,draw,LV_EVENT_DRAW_MAIN,(void*)1);
}
void build(lv_obj_t *parent){
    seconds=GiftData::get("clockMode")["barbarSeconds"]|true;
    layout=layoutFrom=layoutTo=seconds?1.f:0.f;layoutMoving=false;
    pendingTap=saveFailed=initialized=timeWasValid=false;lastEpoch=-1;
    lastRevision=GiftData::revision();for(auto &digit:digits)digit=Digit{};
    view=surface(parent,320,240);lv_obj_add_flag(view,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(view,deleted,LV_EVENT_DELETE,nullptr);
    lv_obj_add_event_cb(view,clicked,LV_EVENT_CLICKED,nullptr);
    lv_obj_add_event_cb(view,draw,LV_EVENT_DRAW_MAIN,nullptr);
    temperature=Ui::label(view,"",36,210,62,&barbar_clock_oswald14,Ink);
    date=Ui::label(view,"",169,210,136,&barbar_clock_oswald14,Ink);
    weatherIcon=Ui::label(view,"",11,207,24,&barbar_clock_weather16,Ink);
    lv_obj_set_height(weatherIcon,24);lv_obj_set_style_text_align(weatherIcon,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_pad_top(weatherIcon,(24-barbar_clock_weather16.line_height)/2,0);
    lv_obj_set_style_bg_color(weatherIcon,lv_color_hex(Background),0);lv_obj_set_style_bg_opa(weatherIcon,255,0);
    lv_obj_clear_flag(weatherIcon,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_align(date,LV_TEXT_ALIGN_RIGHT,0);
    // Text widgets own opaque safe areas; all taps fall through to the clock.
    for(auto *label:{temperature,date}){
        lv_obj_clear_flag(label,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(label,LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(label,lv_color_hex(Background),0);lv_obj_set_style_bg_opa(label,255,0);
        lv_obj_set_style_pad_hor(label,3,0);lv_obj_set_style_pad_ver(label,2,0);
    }
    refresh();timer=lv_timer_create(tick,20,nullptr);
}
void refresh(){
    if(!view)return;
    uint32_t now=lv_tick_get();
    if(lastRevision!=GiftData::revision()){
        lastRevision=GiftData::revision();bool wanted=GiftData::get("clockMode")["barbarSeconds"]|true;
        if(wanted!=seconds)changeLayout(wanted,now);
    }
    int64_t epoch=ClockSource::now();bool valid=DeskLogic::timeValid(epoch);
    auto civil=DeskLogic::beijingDate(epoch);footer(valid,civil);
    if(initialized&&epoch==lastEpoch)return;
    sample(now);
    bool animate=initialized&&timeWasValid&&valid&&epoch==lastEpoch+1;
    const int values[]={civil.hour/10,civil.hour%10,civil.minute/10,civil.minute%10,civil.second/10,civil.second%10};
    bool changed=!initialized;
    for(int i=0;i<6;i++){
        int value=valid?values[i]:-1;changed|=digits[i].target!=value&&(i<4||layout>0);
        target(digits[i],value,animate&&(i<4||seconds),now);
    }
    lastEpoch=epoch;initialized=true;timeWasValid=valid;
    if(changed)lv_obj_invalidate(view);
}
}
