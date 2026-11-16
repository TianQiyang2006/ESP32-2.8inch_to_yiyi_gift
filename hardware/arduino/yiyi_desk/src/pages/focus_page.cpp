#include "mini_apps.h"
#include "page_api.h"
#include "focus_service.h"
#include "timer_page.h"
#include "../app_state.h"
#include "../audio_board.h"
#include "../clock_source.h"
#include "../display.h"
#include "../power_status.h"
#include "../gift_assets/gift_fonts.h"
#include "../saved_design/saved_resources.h"
#include "../ui_widgets.h"
#include <WiFi.h>
#include <algorithm>

// Production widgets, using the approved prototypes' geometry and typography.
// The native review runner renders this same implementation.
namespace FocusApps {
namespace {
using FocusEngine::Stage;
using FocusEngine::Phase;
using Action = FocusService::Action;
enum class View { List, Time, AlarmSettings, Repeat, More, Sounds, Number,
                  Pomodoro, Durations, Reminders, Presets, End, Delete };
enum Command { Back=1, NewAlarm, EditAlarm=10, ToggleAlarm=20, CancelSnooze=30,
    TimeNext=40, SaveAlarm, OpenRepeat, OpenSounds, AlarmVolume, SnoozeLength,
    OpenMore, RingLength, Fade, DeleteAlarm, ConfirmDelete, RepeatOnce,
    RepeatDaily, RepeatWeekdays, RepeatWeekend, RepeatDay=70, RepeatDone=80,
    ToneSelect=90, TonePreview=100, ToneDone=110, NumberMinus, NumberPlus,
    NumberDone, Stop, Snooze, PomoSettings, PomoStart, PomoPause, PomoEnd,
    EndYes, EndNo, SkipRest, Presets, ClassicPreset, DeepPreset, CustomPreset,
    SettingFocus, SettingShort, SettingLong, SettingRounds, SettingsNext,
    SettingsSave, PomoVolume, AutoRest, AutoFocus, CancelDraft, Main,
    AlarmMaster, PomoMaster, DeleteFromList=200 };
constexpr uint32_t Bg=0xF7F7EE, Ink=0x213F32, Muted=0x51685A;
constexpr uint32_t Light=0xE7EEDD, Green=0x315C43, White=0xFFFFFF;
constexpr uint32_t FocusInk=0x663B29, Accent=0xA24A32, FocusLight=0xFFF0E2;
const lv_font_t *F14=saved_gift_font_bold_14_0;
const lv_font_t *F16=&fd_sans_bold_16;
const char *toneNames[]={"经典滴滴","温柔钟声","渐快提醒"};
const char *dayNames[]={"日","一","二","三","四","五","六"};
lv_obj_t *pageRoot=nullptr,*canvas=nullptr,*overlay=nullptr;
lv_obj_t *timeLabel=nullptr,*detailLabel=nullptr,*progress=nullptr,*numberCaption=nullptr;
lv_obj_t *hourRoller=nullptr,*minuteRoller=nullptr;
lv_obj_t *wheelDigits[2][3]{};
View view=View::List,numberReturn=View::AlarmSettings,soundReturn=View::AlarmSettings;
View deleteReturn=View::List;
bool pomodoro=false,redraw=false,wasRunning=false;
unsigned editing=0,numberField=0,selectedTone=0;
int numberValue=0,numberMin=0,numberMax=100,numberStep=1;
FocusEngine::Alarm draft;
FocusEngine::Preferences prefs;
FocusService::Snapshot drawn;
uint32_t refreshAt=0,overlayRevision=0;
uint32_t notificationSeen=0;
bool pendingNotification=false,reportedWriteError=false;
String overlayNotice;
String drawnStatus;
Stage drawnStage=Stage::Ready;

void draw();
void event(lv_event_t *e);
String two(unsigned n){return n<10?"0"+String(n):String(n);}
String hm(unsigned minute){return two(minute/60)+":"+two(minute%60);}
String countdown(uint64_t ms){uint64_t seconds=(ms+999)/1000;return two(unsigned(seconds/60))+":"+two(unsigned(seconds%60));}
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius=10){
    auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,radius,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t *text(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font=nullptr,uint32_t color=Ink,lv_text_align_t align=LV_TEXT_ALIGN_LEFT){
    if(!font)font=F14;
    auto *o=lv_label_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,font->line_height+2);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_align(o,align,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);lv_label_set_text(o,value.c_str());lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
void bind(lv_obj_t *o,int command){lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,(void*)(intptr_t)command);}
lv_obj_t *button(lv_obj_t *parent,const String &value,int x,int y,int w,int h,int command,bool primary=false,bool rest=false){
    auto *o=box(parent,x,y,w,h,primary?(pomodoro&&!rest?Accent:Green):Light,8);bind(o,command);Ui::attachPressFeedback(o);
    text(o,value,4,(h-F16->line_height)/2,w-8,F16,primary?White:Ink,LV_TEXT_ALIGN_CENTER);return o;
}
void hit(lv_obj_t *parent,int x,int y,int w,int h,int command){auto *o=box(parent,x,y,w,h,Bg,0);lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);bind(o,command);}
void toggle(lv_obj_t *parent,int x,int y,bool on,int command){
    auto *track=box(parent,x,y,48,26,on?Green:0xCBD3C7,13);box(track,on?24:2,2,22,22,White,11);
    hit(parent,x-2,y-9,52,44,command);
}
void header(const String &title,const String &right="",bool hasBack=true){
    if(hasBack){static const lv_point_t points[]={{7,0},{0,7},{7,14}};auto *o=lv_line_create(canvas);lv_line_set_points(o,points,3);
        lv_obj_set_pos(o,14,11);lv_obj_set_style_line_color(o,lv_color_hex(Ink),0);lv_obj_set_style_line_width(o,3,0);lv_obj_set_style_line_rounded(o,true,0);hit(canvas,0,0,42,40,Back);}
    text(canvas,title,hasBack?43:12,9,210,F16);
    if(right.length())text(canvas,right,260,11,48,F14,Muted,LV_TEXT_ALIGN_RIGHT);
}
void footer(const String &left,int leftCommand,const String &right,int rightCommand,bool rest=false,int leftWidth=96){
    button(canvas,left,12,198,leftWidth,40,leftCommand);button(canvas,right,20+leftWidth,198,288-leftWidth,40,rightCommand,true,rest);
}
void go(View next){AudioBoard::stopRingtone(1);view=next;redraw=true;}
bool submit(Action a,unsigned index=0,const FocusEngine::Alarm *alarm=nullptr,const FocusEngine::Preferences *p=nullptr){
    if(FocusService::act(a,index,alarm,p)){redraw=true;return true;}
    notifyUser("操作未保存，请检查存储和时间",true);return false;
}
String repeatText(const FocusEngine::Alarm &a){
    if(a.once)return "仅一次";if(a.days==127)return "每天";if(a.days==62)return "工作日";if(a.days==65)return "周末";
    String result="周";for(unsigned i=1;i<=7;++i)if(a.days&(1u<<(i%7)))result+=dayNames[i%7];return result;
}
String absoluteTime(int64_t epoch){auto d=DeskLogic::beijingDate(epoch);return String(d.month)+"月"+String(d.day)+"日 周"+dayNames[FocusEngine::weekday(FocusEngine::midnight(epoch))]+" "+two(d.hour)+":"+two(d.minute);}
String audioState(){
    auto p=PowerStatus::get();if(p.installed&&p.sampled&&(p.protection.critical||p.protection.high||p.protection.sensorFault))return "电源保护 · 暂无声音";
    if(!AudioBoard::ready()||AudioBoard::error().length())return "音频故障 · 请检查声音";
    if(!FocusService::snapshot().engine.state.audio.alarm)return "闹钟总音量已静音";
    return "正在响铃";
}
String listStatus(const FocusService::Snapshot &s){
    if(!s.storageReady)return "存储不可用 · 设置未生效";if(s.writeFailed)return "保存失败 · 请检查存储";if(!s.schedulerReady)return "后台调度不可用";
    int64_t now=ClockSource::now();if(!FocusEngine::validTime(now))return "未校时 · 等待联网校时";
    int64_t next=0;for(auto &a:s.engine.state.alarms){int64_t n=a.snoozeEpoch?a.snoozeEpoch:FocusEngine::next(a,now);if(n&&(!next||n<next))next=n;}
    String clock=s.engine.state.lastSync?(WiFi.status()==WL_CONNECTED?"已校时":"离线计时"):"时间有效";
    if(!next)return clock+" · 暂无开启的闹钟";
    auto date=DeskLogic::beijingDate(next);
    String when=FocusEngine::midnight(next)==FocusEngine::midnight(now)?"今天":FocusEngine::midnight(next)==FocusEngine::midnight(now)+86400?"明天":String(date.month)+"月"+String(date.day)+"日";
    return clock+" · 下次"+when+hm(unsigned(date.hour*60+date.minute));
}
void list(){
    header("闹钟");button(canvas,"设置",150,2,66,36,AlarmMaster);button(canvas,"+ 新建",228,2,80,36,NewAlarm);
    drawnStatus=listStatus(drawn);text(canvas,drawnStatus,14,44,294,F14,Green);
    if(!drawn.engine.count()){box(canvas,12,68,296,140,White);text(canvas,"还没有闹钟",24,94,272,F16,Ink,LV_TEXT_ALIGN_CENTER);text(canvas,"点击右上角新建提醒",24,133,272,F14,Muted,LV_TEXT_ALIGN_CENTER);}
    else{
        auto *scroll=box(canvas,0,68,320,140,Bg,0);lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(scroll,LV_DIR_VER);lv_obj_set_scrollbar_mode(scroll,LV_SCROLLBAR_MODE_AUTO);
        unsigned order[FocusEngine::AlarmCount],n=0;for(unsigned i=0;i<FocusEngine::AlarmCount;++i)if(drawn.engine.state.alarms[i].used)order[n++]=i;
        int64_t now=ClockSource::now();std::sort(order,order+n,[&](unsigned i,unsigned j){auto &a=drawn.engine.state.alarms[i];auto &b=drawn.engine.state.alarms[j];bool aa=a.enabled||a.snoozeEpoch,bb=b.enabled||b.snoozeEpoch;if(aa!=bb)return aa;auto an=a.snoozeEpoch?a.snoozeEpoch:FocusEngine::next(a,now),bn=b.snoozeEpoch?b.snoozeEpoch:FocusEngine::next(b,now);return an==bn?a.minute<b.minute:an<bn;});
        for(unsigned row=0;row<n;++row){unsigned i=order[row];auto &a=drawn.engine.state.alarms[i];int y=row*74;
            auto *card=box(scroll,12,y,296,66,White);bind(card,EditAlarm+i);
            text(card,hm(a.minute),12,1,168,&fd_mont_sb_34);String detail=repeatText(a)+" · "+toneNames[a.tone];
            if(a.snoozeEpoch)detail=FocusEngine::validTime(now)?"稍后 "+hm(unsigned(DeskLogic::beijingDate(a.snoozeEpoch).hour*60+DeskLogic::beijingDate(a.snoozeEpoch).minute))+" 再响":"贪睡待校时";
            else if(a.missed)detail="上次未处理 · "+repeatText(a);
            text(card,detail,12,45,220,F14,Muted);button(card,"删除",178,12,52,38,DeleteFromList+i);toggle(card,236,18,a.enabled||a.snoozeEpoch,ToggleAlarm+i);
        }
    }
    text(canvas,"点击编辑 · 上滑查看更多 · 最多5组",12,218,296,F14,Muted,LV_TEXT_ALIGN_CENTER);
}
void rollerChanged(lv_event_t *){
    draft.minute=lv_roller_get_selected(hourRoller)*60+lv_roller_get_selected(minuteRoller);
    unsigned values[]={unsigned(draft.minute/60),unsigned(draft.minute%60)},counts[]={24,60};
    for(unsigned col=0;col<2;++col)for(int i=0;i<3;++i)if(wheelDigits[col][i])lv_label_set_text(wheelDigits[col][i],two((values[col]+counts[col]+i-1)%counts[col]).c_str());
}
lv_obj_t *roller(int x,int selected,int count){
    auto *r=lv_roller_create(canvas);lv_obj_remove_style_all(r);lv_obj_set_pos(r,x,55);lv_obj_set_size(r,88,122);
    lv_obj_set_style_text_font(r,&lv_font_montserrat_48,0);lv_obj_set_style_text_color(r,lv_color_hex(Muted),0);
    lv_obj_set_style_text_line_space(r,-5,0);lv_obj_set_style_bg_opa(r,LV_OPA_TRANSP,0);
    lv_obj_set_style_bg_opa(r,LV_OPA_TRANSP,LV_PART_SELECTED);lv_obj_set_style_text_font(r,&lv_font_montserrat_48,LV_PART_SELECTED);lv_obj_set_style_text_color(r,lv_color_hex(Ink),LV_PART_SELECTED);
    // A native roller owns all touch/drag/selection logic. Separate, non-clickable
    // glyphs keep the approved small neighbours and large selected value exact.
    lv_obj_set_style_opa(r,LV_OPA_TRANSP,0);
    String options;for(int i=0;i<count;++i){if(i)options+='\n';options+=two(i);}lv_roller_set_options(r,options.c_str(),LV_ROLLER_MODE_INFINITE);lv_roller_set_selected(r,selected,LV_ANIM_OFF);
    lv_obj_add_event_cb(r,rollerChanged,LV_EVENT_VALUE_CHANGED,nullptr);return r;
}
void timePicker(){
    header("设置时间","1/2");text(canvas,"小时",64,36,80,F14,Muted,LV_TEXT_ALIGN_CENTER);text(canvas,"分钟",176,36,80,F14,Muted,LV_TEXT_ALIGN_CENTER);
    box(canvas,46,90,228,58,Light,12);hourRoller=roller(60,draft.minute/60,24);minuteRoller=roller(172,draft.minute%60,60);
    for(unsigned col=0;col<2;++col){int x=col?176:64;wheelDigits[col][0]=text(canvas,"",x,57,80,&fd_sans_bold_22,Muted,LV_TEXT_ALIGN_CENTER);wheelDigits[col][1]=text(canvas,"",col?172:60,88,88,&lv_font_montserrat_48,Ink,LV_TEXT_ALIGN_CENTER);wheelDigits[col][2]=text(canvas,"",x,153,80,&fd_sans_bold_22,Muted,LV_TEXT_ALIGN_CENTER);}rollerChanged(nullptr);
    text(canvas,":",144,88,32,&lv_font_montserrat_48,Ink,LV_TEXT_ALIGN_CENTER);
    text(canvas,"精确到分钟 · 24小时制",12,181,296,F14,Muted,LV_TEXT_ALIGN_CENTER);footer("取消",CancelDraft,"下一步",TimeNext);
}
void row(const char *name,const String &value,int index,int command,bool switchRow=false,bool enabled=false,lv_obj_t *parent=nullptr){
    int y=(parent?0:39)+index*38;auto *r=box(parent?parent:canvas,12,y,296,35,White,6);bind(r,command);lv_obj_set_ext_click_area(r,1);
    text(r,name,11,7,112,F16);if(switchRow)toggle(r,236,5,enabled,command);else text(r,value,126,7,157,F16,pomodoro?FocusInk:Green,LV_TEXT_ALIGN_RIGHT);
}
void alarmSettings(){header("提醒设置");text(canvas,hm(draft.minute),238,10,70,F16,Green,LV_TEXT_ALIGN_RIGHT);
    // Keep the approved first screen exact; secondary options follow the four
    // original rows in the scrollable body, leaving the fixed footer in place.
    auto *scroll=box(canvas,0,39,320,149,Bg,0);lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(scroll,LV_DIR_VER);lv_obj_set_scrollbar_mode(scroll,LV_SCROLLBAR_MODE_AUTO);
    row("重复",repeatText(draft)+" >",0,OpenRepeat,false,false,scroll);row("铃声",String(toneNames[draft.tone])+" >",1,OpenSounds,false,false,scroll);row("闹铃音量",String(draft.volume)+"% >",2,AlarmVolume,false,false,scroll);row("贪睡时长",String(draft.snoozeMinutes)+"分钟 >",3,SnoozeLength,false,false,scroll);
    row("更多设置",">",4,OpenMore,false,false,scroll);footer("取消",CancelDraft,"保存并开启",SaveAlarm);
}
void repeat(){header("重复设置");const char *names[]={"仅一次","每天","周一至周五","周末"};int cmds[]={RepeatOnce,RepeatDaily,RepeatWeekdays,RepeatWeekend};
    for(int i=0;i<4;++i)button(canvas,names[i],12+(i%2)*152,41+(i/2)*43,144,39,cmds[i],i==0?draft.once:!draft.once&&draft.days==(i==1?127:i==2?62:65),true);
    text(canvas,"自选星期",16,130,130,F14,Muted);for(int i=0;i<7;++i)button(canvas,dayNames[i],12+i*43,151,39,40,RepeatDay+i,!draft.once&&(draft.days&(1u<<i)),true);footer("返回",Back,"确认重复",RepeatDone);
}
void more(){header("更多设置");row("响铃时长",String(draft.ringMinutes)+"分钟 >",0,RingLength);row("10秒渐强","",1,Fade,true,draft.fade);if(draft.snoozeEpoch)row("本次贪睡","取消 >",2,CancelSnooze+editing);if(draft.used)row("删除闹钟","删除 >",3,DeleteAlarm);footer("返回",Back,"完成",Back);}
void sounds(){header("选择铃声");const char *details[]={"清晰双音 · 默认","柔和叮咚","短鸣间隔逐渐缩短"};
    for(int i=0;i<3;++i){int y=42+i*51;auto *r=box(canvas,12,y,296,47,i==int(selectedTone)?Light:White,8);bind(r,ToneSelect+i);box(r,11,15,16,16,i==int(selectedTone)?Green:0xCCD5C8,8);if(i==int(selectedTone))box(r,16,20,6,6,White,3);text(r,toneNames[i],37,2,192,F16);text(r,details[i],37,26,192,F14,Muted);button(r,"试听",236,5,52,36,TonePreview+i);}
    footer("取消",Back,"使用此铃声",ToneDone,true);
}
void volumeChanged(lv_event_t *e){auto *slider=lv_event_get_target(e);int raw=lv_slider_get_value(slider);numberValue=std::max(numberMin,std::min(numberMax,((raw+2)/5)*5));lv_slider_set_value(slider,numberValue,LV_ANIM_OFF);lv_label_set_text(numberCaption,(String(numberValue)+"%").c_str());}
void number(){
    if(numberField==0||numberField==7||numberField==8||numberField==9){
        bool master=numberField>=8;
        header(master?(numberField==8?"闹钟总音量":"番茄总音量"):(numberField==0?"闹铃音量":"番茄时钟音量"));
        text(canvas,master?(numberField==8?"全部闹钟和试听":"完成提醒与试听"):(numberField==0?"独立闹铃音量":"独立提示音量"),12,45,296,F16,Muted,LV_TEXT_ALIGN_CENTER);
        numberCaption=text(canvas,String(numberValue)+"%",64,81,192,&fd_mont_sb_34,Ink,LV_TEXT_ALIGN_CENTER);
        button(canvas,"-",12,79,48,48,NumberMinus);button(canvas,"+",260,79,48,48,NumberPlus);
        auto *slider=lv_slider_create(canvas);lv_obj_remove_style_all(slider);lv_obj_set_pos(slider,30,141);lv_obj_set_size(slider,260,12);lv_slider_set_range(slider,numberMin,numberMax);lv_slider_set_value(slider,numberValue,LV_ANIM_OFF);
        lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,0);lv_obj_set_style_bg_color(slider,lv_color_hex(Light),0);lv_obj_set_style_radius(slider,6,0);
        lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_set_style_bg_color(slider,lv_color_hex(pomodoro?Accent:Green),LV_PART_INDICATOR);lv_obj_set_style_radius(slider,6,LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,LV_PART_KNOB);lv_obj_set_style_bg_color(slider,lv_color_hex(White),LV_PART_KNOB);lv_obj_set_style_radius(slider,12,LV_PART_KNOB);lv_obj_set_style_pad_all(slider,6,LV_PART_KNOB);lv_obj_set_style_border_width(slider,2,LV_PART_KNOB);lv_obj_set_style_border_color(slider,lv_color_hex(pomodoro?Accent:Green),LV_PART_KNOB);
        lv_obj_add_event_cb(slider,volumeChanged,LV_EVENT_VALUE_CHANGED,nullptr);
        text(canvas,String(numberMin)+"–100% · 5%/次",12,170,296,F14,Muted,LV_TEXT_ALIGN_CENTER);footer("取消",Back,master?"保存":"确认",NumberDone);return;
    }
    header("调整数值");const char *names[]={"闹铃音量","贪睡时长","响铃时长","专注时长","短休息","长休息","每组轮次","提示音量"};text(canvas,names[numberField],12,48,296,F16,Muted,LV_TEXT_ALIGN_CENTER);String unit=numberField==6?"轮":"分钟";text(canvas,String(numberValue)+unit,54,93,212,F16,Ink,LV_TEXT_ALIGN_CENTER);button(canvas,"-",12,86,48,48,NumberMinus);button(canvas,"+",260,86,48,48,NumberPlus);text(canvas,String(numberMin)+"–"+String(numberMax)+unit,12,154,296,F14,Muted,LV_TEXT_ALIGN_CENTER);footer("取消",Back,"确认",NumberDone);
}
void setNumber(unsigned field,int value,int low,int high,int step,View ret){numberField=field;numberValue=value;numberMin=low;numberMax=high;numberStep=step;numberReturn=ret;go(View::Number);}
void pomo(){
    auto &s=drawn.engine.state.session;auto stage=s.stage;bool rest=s.phase!=Phase::Focus;bool running=stage==Stage::FocusRun||stage==Stage::RestRun,paused=stage==Stage::FocusPause||stage==Stage::RestPause;
    if(stage==Stage::Complete)header("本组已完成","",false);else{header("番茄时钟");button(canvas,"设置",242,2,66,36,PomoSettings);}
    String title=stage==Stage::Ready?"准备专注":stage==Stage::FocusRun?"专注中":paused?"已暂停":stage==Stage::FocusDone?"专注完成":stage==Stage::RestRun?(s.phase==Phase::LongRest?"长休息中":"短休息中"):stage==Stage::RestDone?"准备下一轮":stage==Stage::Interrupted?"计时已中断":String("完整完成")+s.settings.rounds+"轮专注";
    uint32_t ink=rest?Ink:FocusInk;box(canvas,12,44,296,122,rest?Light:FocusLight,12);text(canvas,title,24,51,160,F16,ink);text(canvas,stage==Stage::Complete?"":String("第")+s.round+"/"+s.settings.rounds+"轮",184,52,112,F14,ink,LV_TEXT_ALIGN_RIGHT);
    String value=stage==Stage::Complete?String(s.completed)+"/"+s.settings.rounds:countdown(drawn.engine.remaining(FocusService::monotonic()));timeLabel=text(canvas,value,24,76,272,&lv_font_montserrat_48,ink,LV_TEXT_ALIGN_CENTER);detailLabel=text(canvas,"",24,135,272,F14,Muted,LV_TEXT_ALIGN_CENTER);box(canvas,28,158,264,4,rest?0xCDD9C6:0xE7D4C1,2);progress=box(canvas,28,158,1,4,rest?Green:Accent,2);
    String note;
    if(stage==Stage::Ready){auto today=drawn.engine.today(ClockSource::now());note=FocusEngine::validTime(ClockSource::now())?String("今日完成")+today.count+"个 · "+today.minutes+"分钟":"日期待校准 · 完成记录仍保存";footer("切换预设",Presets,"开始专注",PomoStart,false,112);}
    else if(stage==Stage::Complete){note="休息好后，再开启下一组";footer("回到首页",Main,"开始新一组",PomoStart,true,128);}
    else if(stage==Stage::Interrupted){note="本轮未完成，不计入完成统计";footer("结束计时",PomoEnd,"重新开始本轮",PomoStart,rest);}
    else if(stage==Stage::FocusDone){note="本轮已记1个番茄 · 等待开始休息";footer("跳过休息",SkipRest,"开始休息",PomoStart,true,128);}
    else if(rest){note=String("休息结束后")+(s.phase==Phase::LongRest?"本组完成":String("进入第")+String(s.round+1)+"/"+s.settings.rounds+"轮");footer("跳过休息",SkipRest,paused?"继续休息":"暂停休息",paused?PomoStart:PomoPause,true,128);}
    else{note=paused?String("继续后从")+value+"接着计时":"返回桌面仍会继续计时";footer("结束计时",PomoEnd,stage==Stage::RestDone?"开始专注":paused?"继续专注":"暂停",running?PomoPause:PomoStart);}
    if(!s.settings.volume||!drawn.engine.state.audio.pomodoro){note="提示音已静音 · 仅显示完成提醒";text(canvas,LV_SYMBOL_MUTE,15,175,20,&lv_font_montserrat_14,Muted);}text(canvas,note,12,174,296,F14,Muted,LV_TEXT_ALIGN_CENTER);drawnStage=stage;
}
void updatePomo(const FocusService::Snapshot &s){
    if(!timeLabel||!detailLabel||!progress)return;auto &session=s.engine.state.session;uint64_t left=s.engine.remaining(FocusService::monotonic());if(session.stage!=Stage::Complete)lv_label_set_text(timeLabel,countdown(left).c_str());String detail;
    if(session.stage==Stage::Complete)detail="累计专注"+String(session.minutes)+"分钟";
    else if(session.stage==Stage::Ready||session.stage==Stage::RestDone)detail=String("专注")+session.settings.focus+"分 · 短休"+session.settings.shortRest+"分";
    else if(session.stage==Stage::FocusPause||session.stage==Stage::RestPause)detail="暂停期间不计时";
    else if(session.stage==Stage::Interrupted)detail="重启或计时中断 · 请重新开始";
    else if(session.stage==Stage::FocusDone)detail=String("接下来：")+(session.phase==Phase::LongRest?"长休息":"短休息")+String(unsigned(session.total/60000))+"分钟";
    else if(session.phase!=Phase::Focus)detail="本轮已完成 · 放松一下";
    else{uint64_t elapsed=(session.total-left)/1000;detail=String("已专注")+String(unsigned(elapsed/60))+"分"+String(unsigned(elapsed%60))+"秒";}
    lv_label_set_text(detailLabel,detail.c_str());unsigned width=session.stage==Stage::Complete?264:session.total?unsigned(264*(session.total-left)/session.total):0;lv_obj_set_width(progress,std::max(1u,width));if(width)lv_obj_clear_flag(progress,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(progress,LV_OBJ_FLAG_HIDDEN);
}
void durations(){header("时长与轮次","1/2");button(canvas,"总音量",174,2,78,34,PomoMaster);row("专注时长",String(prefs.focus)+"分钟 >",0,SettingFocus);row("短休息",String(prefs.shortRest)+"分钟 >",1,SettingShort);row("长休息",String(prefs.longRest)+"分钟 >",2,SettingLong);row("每组轮次",String(prefs.rounds)+"轮 >",3,SettingRounds);footer("取消",CancelDraft,"下一步",SettingsNext);}
void reminders(){header("提醒与衔接","2/2");row("提示铃声",String(toneNames[prefs.tone])+" >",0,OpenSounds);row("提示音量",prefs.volume?String(prefs.volume)+"% >":"已静音 >",1,PomoVolume);row("自动休息","",2,AutoRest,true,prefs.autoRest);row("自动专注","",3,AutoFocus,true,prefs.autoFocus);footer("取消",CancelDraft,"保存设置",SettingsSave);}
void presets(){header("切换预设");row("经典预设","25 / 5 / 15 >",0,ClassicPreset);row("深度预设","50 / 10 / 20 >",1,DeepPreset);row("自定义","设置时长 >",2,CustomPreset);text(canvas,"选择后进入草稿，保存才生效",12,167,296,F14,Muted,LV_TEXT_ALIGN_CENTER);footer("取消",CancelDraft,"自定义",CustomPreset);}
void confirmation(bool deleting){header(deleting?"删除闹钟":"结束计时","",false);box(canvas,12,50,296,125,White);text(canvas,deleting?"确定删除这条闹钟？":"确定结束本次计时？",24,74,272,F16,Ink,LV_TEXT_ALIGN_CENTER);text(canvas,deleting?"同时取消该闹钟的贪睡":"已完成记录保留，本轮不计入",24,118,272,F14,Muted,LV_TEXT_ALIGN_CENTER);footer("取消",deleting?Back:EndNo,deleting?"确认删除":"结束计时",deleting?ConfirmDelete:EndYes);}
void draw(){
    if(!pageRoot||!lv_obj_is_valid(pageRoot))return;Ui::cancelAnimations();lv_obj_clean(pageRoot);canvas=pageRoot;lv_obj_set_style_bg_color(canvas,lv_color_hex(Bg),0);lv_obj_set_style_bg_opa(canvas,LV_OPA_COVER,0);lv_obj_clear_flag(canvas,LV_OBJ_FLAG_SCROLLABLE);timeLabel=detailLabel=progress=hourRoller=minuteRoller=nullptr;drawn=FocusService::snapshot();
    switch(view){case View::List:list();break;case View::Time:timePicker();break;case View::AlarmSettings:alarmSettings();break;case View::Repeat:repeat();break;case View::More:more();break;case View::Sounds:sounds();break;case View::Number:number();break;case View::Pomodoro:pomo();updatePomo(drawn);break;case View::Durations:durations();break;case View::Reminders:reminders();break;case View::Presets:presets();break;case View::End:confirmation(false);break;case View::Delete:confirmation(true);break;}redraw=false;
}
void event(lv_event_t *e){
    int command=int(intptr_t(lv_event_get_user_data(e)));
    if(command>=DeleteFromList&&command<DeleteFromList+5){editing=command-DeleteFromList;draft=FocusService::snapshot().engine.state.alarms[editing];deleteReturn=View::List;go(View::Delete);return;}
    if(command>=EditAlarm&&command<EditAlarm+5){editing=command-EditAlarm;draft=FocusService::snapshot().engine.state.alarms[editing];go(View::Time);return;}
    if(command>=ToggleAlarm&&command<ToggleAlarm+5){submit(Action::ToggleAlarm,command-ToggleAlarm);return;}
    if(command>=CancelSnooze&&command<CancelSnooze+5){if(submit(Action::CancelSnooze,command-CancelSnooze)){draft.snoozeEpoch=0;go(View::List);}return;}
    if(command>=RepeatDay&&command<RepeatDay+7){draft.once=false;draft.days^=1u<<(command-RepeatDay);redraw=true;return;}
    if(command>=ToneSelect&&command<ToneSelect+3){selectedTone=command-ToneSelect;AudioBoard::stopRingtone(1);redraw=true;return;}
    if(command>=TonePreview&&command<TonePreview+3){unsigned volume=pomodoro?prefs.volume:draft.volume;auto audio=FocusService::snapshot().engine.state.audio;unsigned master=pomodoro?audio.pomodoro:audio.alarm;if(!volume||!master){notifyUser("当前已静音，请先调整音量",false);return;}if(!AudioBoard::ringtone(command-TonePreview,volume,8000,1,0,pomodoro?AudioBoard::Reminder::Pomodoro:AudioBoard::Reminder::Alarm))notifyUser("暂时无法试听，请检查音频",true);return;}
    switch(command){
        case Back:if(!back()){lv_event_t forwarded=*e;forwarded.user_data=(void*)(uintptr_t)Ui::Back;Ui::dispatchAction(&forwarded);}break;
        case NewAlarm:{int free=FocusService::snapshot().engine.freeAlarm();if(free<0){notifyUser("最多设置5组闹钟",false);break;}editing=free;draft=FocusEngine::Alarm{};go(View::Time);break;}
        case TimeNext:rollerChanged(nullptr);go(View::AlarmSettings);break;
        case SaveAlarm:draft.enabled=true;if(submit(Action::SaveAlarm,editing,&draft)){auto s=FocusService::snapshot();int64_t next=FocusEngine::next(s.engine.state.alarms[editing],ClockSource::now());notifyUser(next?"下次："+absoluteTime(next):"已保存 · 等待校时后生效",false);go(View::List);}break;
        case OpenRepeat:go(View::Repeat);break;
        case OpenMore:go(View::More);break;
        case OpenSounds:soundReturn=view;selectedTone=pomodoro?prefs.tone:draft.tone;go(View::Sounds);break;
        case AlarmVolume:setNumber(0,draft.volume,5,100,5,view);break;
        case SnoozeLength:setNumber(1,draft.snoozeMinutes,5,15,5,view);break;
        case RingLength:setNumber(2,draft.ringMinutes,1,5,2,view);break;
        case Fade:draft.fade=!draft.fade;redraw=true;break;
        case DeleteAlarm:deleteReturn=view;go(View::Delete);break;
        case ConfirmDelete:if(submit(Action::DeleteAlarm,editing))go(View::List);break;
        case RepeatOnce:draft.once=true;draft.days=0;redraw=true;break;
        case RepeatDaily:case RepeatWeekdays:case RepeatWeekend:draft.once=false;draft.days=command==RepeatDaily?127:command==RepeatWeekdays?62:65;redraw=true;break;
        case RepeatDone:if(!draft.once&&!draft.days)notifyUser("请至少选择一天",true);else go(View::AlarmSettings);break;
        case ToneDone:if(pomodoro)prefs.tone=selectedTone;else draft.tone=selectedTone;go(soundReturn);break;
        case NumberMinus:case NumberPlus:numberValue=std::max(numberMin,std::min(numberMax,numberValue+(command==NumberPlus?numberStep:-numberStep)));redraw=true;break;
        case NumberDone:if(numberField>=8){if(submit(numberField==8?Action::AlarmMasterVolume:Action::PomodoroMasterVolume,unsigned(numberValue)))go(numberReturn);break;}switch(numberField){case 0:draft.volume=numberValue;break;case 1:draft.snoozeMinutes=numberValue;break;case 2:draft.ringMinutes=numberValue;break;case 3:prefs.focus=numberValue;break;case 4:prefs.shortRest=numberValue;break;case 5:prefs.longRest=numberValue;break;case 6:prefs.rounds=numberValue;break;case 7:prefs.volume=numberValue;break;}go(numberReturn);break;
        case Stop:submit(Action::StopAlarm);break;
        case Snooze:if(submit(Action::SnoozeAlarm)){auto s=FocusService::snapshot();int64_t next=0;for(auto &a:s.engine.state.alarms)if(a.snoozeEpoch&&(!next||a.snoozeEpoch<next))next=a.snoozeEpoch;if(next)notifyUser("再次响铃："+absoluteTime(next),false);}break;
        case PomoSettings:prefs=FocusService::snapshot().engine.state.preferences;if(FocusService::snapshot().engine.state.session.active)notifyUser("时长铃声下组生效，总音量可调整",false);go(View::Durations);break;
        case PomoStart:submit(Action::Start);break;
        case PomoPause:submit(Action::Pause);break;
        case PomoEnd:{auto stage=FocusService::snapshot().engine.state.session.stage;wasRunning=stage==Stage::FocusRun||stage==Stage::RestRun;if(wasRunning&&!submit(Action::Pause))break;go(View::End);break;}
        case EndYes:if(submit(Action::End))go(View::Pomodoro);break;
        case EndNo:if(wasRunning&&!submit(Action::Start))break;go(View::Pomodoro);break;
        case SkipRest:submit(Action::SkipRest);break;
        case Presets:prefs=FocusService::snapshot().engine.state.preferences;go(View::Presets);break;
        case ClassicPreset:prefs.focus=25;prefs.shortRest=5;prefs.longRest=15;prefs.rounds=4;go(View::Durations);break;
        case DeepPreset:prefs.focus=50;prefs.shortRest=10;prefs.longRest=20;prefs.rounds=4;go(View::Durations);break;
        case CustomPreset:go(View::Durations);break;
        case SettingFocus:setNumber(3,prefs.focus,5,90,1,view);break;
        case SettingShort:setNumber(4,prefs.shortRest,1,30,1,view);break;
        case SettingLong:setNumber(5,prefs.longRest,5,60,1,view);break;
        case SettingRounds:setNumber(6,prefs.rounds,2,8,1,view);break;
        case SettingsNext:go(View::Reminders);break;
        case SettingsSave:if(submit(Action::Configure,0,nullptr,&prefs)){notifyUser(FocusService::snapshot().engine.state.session.active?"设置已保存，下组生效":"设置已保存",false);go(View::Pomodoro);}break;
        case PomoVolume:setNumber(7,prefs.volume,0,100,5,view);break;
        case AutoRest:prefs.autoRest=!prefs.autoRest;redraw=true;break;
        case AutoFocus:prefs.autoFocus=!prefs.autoFocus;redraw=true;break;
        case CancelDraft:go(pomodoro?View::Pomodoro:View::List);break;
        case Main:if(submit(Action::End))go(View::Pomodoro);break;
        case AlarmMaster:setNumber(8,FocusService::snapshot().engine.state.audio.alarm,0,100,5,view);break;
        case PomoMaster:setNumber(9,FocusService::snapshot().engine.state.audio.pomodoro,0,100,5,view);break;
    }
}
void ringing(const FocusService::Snapshot &s){
    if(!s.engine.runtime.ringing){if(overlay){lv_obj_del(overlay);overlay=nullptr;Display::setBrightness(uint8_t(settings.brightness));}return;}
    String notice=audioState();
    if(overlay&&overlayRevision==s.engine.runtime.ringRevision&&notice==overlayNotice){lv_obj_move_foreground(overlay);return;}
    overlayNotice=notice;
    if(overlay)lv_obj_del(overlay);overlay=box(lv_layer_top(),0,0,320,240,Bg,0);lv_obj_add_flag(overlay,LV_OBJ_FLAG_CLICKABLE);overlayRevision=s.engine.runtime.ringRevision;Display::setBrightness(uint8_t(std::max(80,int(settings.brightness))));
    int lead=s.engine.leadAlarm();auto &a=s.engine.state.alarms[lead];bool previousPomodoro=pomodoro;pomodoro=false;
    text(overlay,"闹钟响了",12,15,296,F16,Ink,LV_TEXT_ALIGN_CENTER);box(overlay,12,44,296,132,White,14);text(overlay,s.engine.ringingCount()>1?String(s.engine.ringingCount())+"组同时提醒":"起床提醒",24,48,272,F16,Muted,LV_TEXT_ALIGN_CENTER);text(overlay,hm(a.minute),24,71,272,&lv_font_montserrat_48,Ink,LV_TEXT_ALIGN_CENTER);
    int64_t occurrence=s.engine.runtime.occurrences[lead];auto d=DeskLogic::beijingDate(occurrence);text(overlay,String(d.month)+"月"+two(d.day)+"日 周"+dayNames[FocusEngine::weekday(FocusEngine::midnight(occurrence))]+" · "+toneNames[a.tone],24,133,272,F14,Green,LV_TEXT_ALIGN_CENTER);text(overlay,audioState(),24,154,272,F14,Muted,LV_TEXT_ALIGN_CENTER);button(overlay,String("稍后")+a.snoozeMinutes+"分钟",12,190,148,46,Snooze);button(overlay,"停止",168,190,140,46,Stop,true);pomodoro=previousPomodoro;
}
} // namespace
void begin(){FocusService::begin();}
void poll(){
    FocusService::poll();auto s=FocusService::snapshot();
    if(s.engine.runtime.notificationRevision!=notificationSeen){notificationSeen=s.engine.runtime.notificationRevision;pendingNotification=true;}
    if(s.writeFailed&&!reportedWriteError)notifyUser("计时记录保存失败，正在重试",true);
    reportedWriteError=s.writeFailed;
    if(pendingNotification&&!s.engine.runtime.ringing){pendingNotification=false;if(app!=App::Pomodoro){auto stage=s.engine.state.session.stage;notifyUser(stage==Stage::Complete?"番茄时钟：本组已完成":stage==Stage::FocusDone?"专注完成，可以开始休息":"番茄时钟：已切换下一阶段",false);}}
    TimerPage::poll();ringing(s);refresh();
}
void buildAlarm(lv_obj_t *parent){begin();pageRoot=parent;pomodoro=false;view=View::List;draw();}
void buildPomodoro(lv_obj_t *parent){begin();pageRoot=parent;pomodoro=true;view=View::Pomodoro;draw();}
bool canBack(){return pageRoot&&view!=(pomodoro?View::Pomodoro:View::List);}
bool back(){
    if(!canBack())return false;if(view==View::Sounds)go(soundReturn);else if(view==View::Number)go(numberReturn);else if(view==View::Repeat||view==View::More)go(View::AlarmSettings);else if(view==View::AlarmSettings)go(View::Time);else if(view==View::Reminders)go(View::Durations);else if(view==View::Delete)go(deleteReturn);else if(view==View::End){if(wasRunning&&!submit(Action::Start))return true;go(View::Pomodoro);}else go(pomodoro?View::Pomodoro:View::List);return true;
}
void refresh(bool force){
    if(!pageRoot||!lv_obj_is_valid(pageRoot))return;uint32_t now=millis();if(!force&&!redraw&&now-refreshAt<250)return;refreshAt=now;auto s=FocusService::snapshot();
    if(view==View::Pomodoro&&(drawnStage!=s.engine.state.session.stage||(drawnStage==Stage::Ready&&memcmp(s.engine.state.daily,drawn.engine.state.daily,sizeof(s.engine.state.daily)))))redraw=true;
    if(view==View::List&&(s.engine.runtime.ringRevision!=drawn.engine.runtime.ringRevision||s.writeFailed!=drawn.writeFailed||s.engine.state.lastSync!=drawn.engine.state.lastSync||memcmp(s.engine.state.alarms,drawn.engine.state.alarms,sizeof(s.engine.state.alarms))||listStatus(s)!=drawnStatus))redraw=true;
    if(redraw)draw();else if(view==View::Pomodoro)updatePomo(s);
}
void cancel(){AudioBoard::stopRingtone(1);pageRoot=canvas=nullptr;timeLabel=detailLabel=progress=hourRoller=minuteRoller=nullptr;redraw=false;}
} // namespace FocusApps
