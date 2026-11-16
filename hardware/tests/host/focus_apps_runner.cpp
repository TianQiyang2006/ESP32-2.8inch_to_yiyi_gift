#include "launcher_assertions.h"
// Runs the actual firmware service and widget event handlers. Fixtures below
// exist only in this host executable and are never linked into ESP32 firmware.
#include "Arduino.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/audio_board.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/pages/focus_service.h"
#include "hardware/arduino/yiyi_desk/src/pages/focus_storage.h"
#include "hardware/arduino/yiyi_desk/src/pages/timer_page.h"
#include "hardware/arduino/yiyi_desk/src/pages/timer_storage.h"
#include "hardware/arduino/yiyi_desk/src/pages/timer_service.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/src/tone_synth.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace FocusEngine;
using Action=FocusService::Action;
int64_t HostEpoch=DeskLogic::beijingEpoch(2026,11,1,23,0);
extern unsigned HostTone,HostToneVolume,HostTonePriority,HostToneDuration,HostToneCalls;
extern unsigned HostAlarmMaster,HostPomodoroMaster;
extern AudioBoard::Reminder HostToneSource;
extern bool HostAudioReady;
lv_color_t pixels[320*240],buffer[320*40];
std::string out;
unsigned frames=0;
void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *data){for(int y=area->y1;y<=area->y2;++y)for(int x=area->x1;x<=area->x2;++x){if(x>=0&&x<320&&y>=0&&y<240)pixels[y*320+x]=*data;++data;}lv_disp_flush_ready(drv);}
lv_obj_t *findLabel(lv_obj_t *obj,const char *value,unsigned &skip){
    if(lv_obj_check_type(obj,&lv_label_class)&&!strcmp(lv_label_get_text(obj),value)){if(!skip)return obj;--skip;}
    for(unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)if(auto *found=findLabel(lv_obj_get_child(obj,i),value,skip))return found;return nullptr;
}
lv_obj_t *label(const char *value,unsigned skip=0){return findLabel(root,value,skip);}
void click(const char *value,unsigned nth=0,bool top=false){std::cout<<"click "<<value<<"\n";unsigned skip=nth;auto *l=findLabel(top?lv_layer_top():root,value,skip);if(!l)std::cerr<<"Missing caption: "<<value<<"\n";assert(l);auto *target=l;while(target&&!lv_obj_has_flag(target,LV_OBJ_FLAG_CLICKABLE))target=lv_obj_get_parent(target);assert(target);lv_event_send(target,LV_EVENT_CLICKED,nullptr);FocusApps::refresh(true);TimerPage::refresh(true);lv_obj_update_layout(root);}
void checkLabels(lv_obj_t *obj,bool inRoller=false){
    inRoller=inRoller||lv_obj_check_type(obj,&lv_roller_class);
    if(lv_obj_check_type(obj,&lv_label_class)&&!inRoller&&!lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)){
        const char *value=lv_label_get_text(obj);auto *f=lv_obj_get_style_text_font(obj,0);lv_point_t size{};
        lv_txt_get_size(&size,value,f,lv_obj_get_style_text_letter_space(obj,0),lv_obj_get_style_text_line_space(obj,0),LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
        if(size.x>lv_obj_get_width(obj)){std::cerr<<"Clipped label: "<<value<<" "<<size.x<<"/"<<lv_obj_get_width(obj)<<"\n";assert(false);}
    }
    for(unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)checkLabels(lv_obj_get_child(obj,i),inRoller);
}
void save(const std::string &name,bool validate=true){
    if (name == "launcher-2") writeLauncherReport(root, out + "/launcher-2.json");lv_obj_update_layout(root);if(validate){checkLabels(root);checkLabels(lv_layer_top());}lv_refr_now(nullptr);std::ofstream file(out+"/"+name+".rgb565",std::ios::binary);file.write((char*)pixels,sizeof(pixels));++frames;std::cout<<"frame "<<name<<"\n";}
void show(App target){GiftUI::leaveView();Ui::cancelAnimations();Ui::clearStatusCluster();lv_obj_clean(root);app=target;page=6;SavedDesign::invalidate();assert(GiftUI::render(root));lv_obj_update_layout(root);}
void advance(uint32_t ms){HostMillis+=ms;HostEpoch+=ms/1000;FocusApps::poll();FocusApps::refresh(true);}
void stopAudio(){AudioBoard::stopRingtone(1);AudioBoard::stopRingtone(2);AudioBoard::stopRingtone(3);}
void set(const Engine &engine){stopAudio();FocusService::debugSet(engine);FocusApps::poll();}
void touch(int x,int y){lv_obj_update_layout(root);lv_obj_t *target=nullptr;
    auto search=[&](auto &self,lv_obj_t *p)->void{if(lv_obj_has_flag(p,LV_OBJ_FLAG_HIDDEN))return;lv_area_t a;lv_obj_get_coords(p,&a);if(x<a.x1||x>a.x2||y<a.y1||y>a.y2)return;if(lv_obj_has_flag(p,LV_OBJ_FLAG_CLICKABLE))target=p;for(unsigned i=0;i<lv_obj_get_child_cnt(p);++i)self(self,lv_obj_get_child(p,i));};search(search,root);assert(target);lv_event_send(target,LV_EVENT_CLICKED,nullptr);FocusApps::refresh(true);}
void openMore(){auto *caption=label("更多设置");assert(caption);auto *row=lv_obj_get_parent(caption);lv_obj_scroll_to_view(row,LV_ANIM_OFF);lv_obj_update_layout(root);lv_area_t a;lv_obj_get_coords(row,&a);assert(a.y1>=39&&a.y2<188);touch((a.x1+a.x2)/2,(a.y1+a.y2)/2);assert(label("响铃时长"));}
lv_obj_t *findSlider(lv_obj_t *p){if(lv_obj_check_type(p,&lv_slider_class))return p;for(unsigned i=0;i<lv_obj_get_child_cnt(p);++i)if(auto *s=findSlider(lv_obj_get_child(p,i)))return s;return nullptr;}
void slideTo(int value){auto *slider=findSlider(root);assert(slider);lv_slider_set_value(slider,value,LV_ANIM_OFF);lv_event_send(slider,LV_EVENT_VALUE_CHANGED,nullptr);}
Engine sample(){Engine e;e.state.lastSync=HostEpoch;Alarm a;a.volume=60;a.enabled=true;a.once=false;a.days=62;assert(e.saveAlarm(0,a,HostEpoch));a=Alarm{};a.minute=540;a.tone=1;assert(e.saveAlarm(1,a,HostEpoch));return e;}
void engineTests(){
    int64_t start=DeskLogic::beijingEpoch(2026,11,1,23,0),due=DeskLogic::beijingEpoch(2026,11,2,7,30);
    Engine e;assert(!e.count());Alarm a;a.enabled=true;assert(e.saveAlarm(0,a,start));assert(e.state.alarms[0].onceEpoch==due);
    e.tick(due-1,1000);assert(!e.runtime.ringing);e.tick(due,1100);assert(e.ringingCount()==1&&!e.state.alarms[0].enabled);assert(e.state.alarms[0].lastFired==due);e.stop();e.tick(due,1200);assert(!e.runtime.ringing);
    e=Engine{};a.once=false;a.days=62;assert(e.saveAlarm(0,a,start));assert(next(e.state.alarms[0],start)==due);e.tick(due+60,1);assert(e.runtime.ringing&&e.state.alarms[0].delayMs==60000);e.stop();e.tick(due-3600,2);e.tick(due,3);assert(!e.runtime.ringing);
    e=Engine{};assert(e.saveAlarm(0,a,start));e.tick(due+61,1);assert(!e.runtime.ringing&&e.state.alarms[0].missed);
    e=Engine{};assert(e.saveAlarm(0,a,start));a.volume=90;a.tone=2;assert(e.saveAlarm(1,a,start));e.tick(due,1000);assert(e.ringingCount()==2&&e.ringVolume()==90&&e.leadAlarm()==0);assert(e.snooze(due,1000));assert(!e.runtime.ringing);e.tick(due+7200,600999);assert(!e.runtime.ringing);e.tick(due+7200,601000);assert(e.ringingCount()==2);e.stop();assert(!e.state.alarms[0].snoozeEpoch);
    e=Engine{};a=Alarm{};a.enabled=true;assert(e.saveAlarm(0,a,start));e.tick(due,1000);assert(e.snooze(due,1000));Engine reboot;reboot.state=e.state;reboot.restore();reboot.tick(0,2000);assert(!reboot.runtime.ringing);reboot.tick(due+600,3000);assert(reboot.runtime.ringing);reboot.stop();reboot.tick(due+600,4000);assert(!reboot.runtime.ringing);
    assert(e.toggle(0,due+1));assert(!e.state.alarms[0].enabled&&!e.state.alarms[0].snoozeEpoch);
    e=Engine{};a=Alarm{};a.enabled=true;a.once=false;a.days=62;assert(e.saveAlarm(0,a,start));e.tick(DeskLogic::beijingEpoch(2026,11,7,8,0),100);assert(e.state.alarms[0].missed&&!e.runtime.ringing);
    e=Engine{};a=Alarm{};a.once=false;a.days=65;a.enabled=true;assert(e.saveAlarm(0,a,start));assert(next(e.state.alarms[0],DeskLogic::beijingEpoch(2026,11,2,8,0))==DeskLogic::beijingEpoch(2026,11,7,7,30));
    a.days=127;assert(e.saveAlarm(0,a,DeskLogic::beijingEpoch(2026,12,31,23,59)));assert(next(e.state.alarms[0],DeskLogic::beijingEpoch(2026,12,31,23,59))==DeskLogic::beijingEpoch(2027,1,1,7,30));
    e=Engine{};for(unsigned i=0;i<5;++i)assert(e.saveAlarm(i,a,start));assert(e.freeAlarm()==-1&&!e.saveAlarm(5,a,start));e.erase(2);assert(e.freeAlarm()==2);
    // 60 independent due-time boundaries, checked at 100 ms cadence.
    for(unsigned i=0;i<60;++i){e=Engine{};a=Alarm{};a.enabled=true;int64_t target=due+i*86400;assert(e.saveAlarm(0,a,target-30));e.tick(target-1,100);assert(!e.runtime.ringing);e.tick(target,1100);assert(e.runtime.ringing);e.stop();e.tick(target,1200);assert(!e.runtime.ringing);}
    e=Engine{};assert(e.start(100));assert(e.pause(438100));assert(e.remaining(999999)==1062000);assert(e.start(999999));assert(e.remaining(999999)==1062000);e.tick(start+100000,1000000);assert(e.remaining(1000000)==1061999);assert(e.pause(1000000));e.tick(start-500000,2000000);assert(e.remaining(2000000)==1061999);
    // One group is exactly four focus phases followed by the long rest.
    e=Engine{};uint64_t mono=100;assert(e.start(mono));for(unsigned round=1;round<=4;++round){mono=e.runtime.deadline;e.tick(start,mono);assert(e.state.session.stage==Stage::FocusDone&&e.state.session.completed==round);assert(e.state.session.phase==(round==4?Phase::LongRest:Phase::ShortRest));unsigned count=e.today(start).count;e.tick(start,mono);assert(e.today(start).count==count);assert(e.start(mono));mono=e.runtime.deadline;e.tick(start,mono);assert(e.state.session.stage==(round==4?Stage::Complete:Stage::RestDone));if(round<4)assert(e.start(mono));}assert(e.today(start).count==4&&e.today(start).minutes==100&&e.state.session.minutes==100);
    for(unsigned mask=0;mask<4;++mask){e=Engine{};e.state.preferences.autoRest=mask&1;e.state.preferences.autoFocus=mask&2;assert(e.start(10));uint64_t boundary=e.runtime.deadline;e.tick(start,boundary+400);assert(e.state.session.stage==((mask&1)?Stage::RestRun:Stage::FocusDone));if(mask&1)assert(e.runtime.deadline==boundary+300000);else assert(e.start(boundary+400));boundary=e.runtime.deadline;e.tick(start,boundary);assert(e.state.session.stage==((mask&2)?Stage::FocusRun:Stage::RestDone));}
    e=Engine{};assert(e.start(10));e.tick(start,e.runtime.deadline+60001);assert(e.state.session.stage==Stage::Interrupted&&e.today(start).count==0);assert(e.start(5000000));e.restore();assert(e.state.session.stage==Stage::Interrupted&&e.today(start).count==0);
    e=Engine{};assert(e.start(10));auto p=e.state.preferences;p.focus=50;assert(e.configure(p));assert(e.state.session.settings.focus==25);e.end();assert(e.start(100));assert(e.state.session.settings.focus==50);
    e=Engine{};e.restore();assert(e.start(10));mono=e.runtime.deadline;e.tick(0,mono);assert(e.pendingCount()==1);e.tick(start,mono+1000);assert(e.pendingCount()==0&&e.today(start).count==1);
    // Bounds, rollover-free 64-bit duration, and tone PCM generation.
    p=FocusEngine::Preferences{};p.focus=90;p.shortRest=30;p.longRest=60;p.rounds=8;assert(valid(p));e=Engine{};assert(e.configure(p)&&e.start((1ULL<<32)-500));assert(e.remaining((1ULL<<32)+500)==5399000);p.focus=4;assert(!e.configure(p));
    for(unsigned tone=0;tone<3;++tone){int peak=0;for(unsigned sample=0;sample<16000;++sample){int value=ToneSynth::pcm(tone,sample,100,0);peak=std::max(peak,std::abs(value));assert(std::abs(value)<=32767);}assert(peak>1000);assert(ToneSynth::pcm(tone,100,0,0)==0);}
    for(unsigned tone=0;tone<3;++tone)for(unsigned i=0;i<16000;++i){auto raw=ToneSynth::pcm(tone,i,60);assert(ToneSynth::applyMaster(raw,0)==0);assert(ToneSynth::applyMaster(raw,100)==raw);assert(ToneSynth::applyMaster(raw,101)==raw);assert(std::abs(ToneSynth::applyMaster(raw,5))<=std::abs(ToneSynth::applyMaster(raw,20)));assert(std::abs(ToneSynth::applyMaster(raw,20))<=std::abs(raw));}
    assert(ToneSynth::applyMaster(-32768,100)==-32768&&ToneSynth::applyMaster(32767,100)==32767);
    ::Preferences nvs;assert(nvs.begin("journal-test"));FocusStorage::Journal journal(nvs);State initial;assert(journal.load(initial)==FocusStorage::Result::Empty);e=sample();assert(journal.save(e.state));State loaded;assert(journal.load(loaded)==FocusStorage::Result::Ready&&loaded.alarms[0].minute==450);::Preferences::partialNextWriteBytes=9;e.state.alarms[0].minute=451;assert(!journal.save(e.state));FocusStorage::Journal recovered(nvs);assert(recovered.load(loaded)==FocusStorage::Result::Recovered&&loaded.alarms[0].minute==450);
    std::cout<<"PASS: alarm boundaries/grouping/snooze/calendar, timer state machine/64-bit/pause/rounds/stats, journal recovery, 3 PCM tones\n";
}
void uiTests(){
    Engine e;set(e);show(App::Alarm);save("alarm-00-empty");click("+ 新建");assert(label("07")&&label("30"));save("alarm-02-time");auto *tracked=lv_obj_create(root);Ui::clearOnDelete(tracked,&tracked);click("下一步");assert(!tracked);click("闹铃音量");assert(label("20%"));save("alarm-09-volume");slideTo(5);assert(label("5%"));save("alarm-10-low-volume");click("取消");click("闹铃音量");assert(label("20%"));slideTo(60);click("确认");click("重复");click("周一至周五");save("alarm-06-repeat");click("确认重复");save("alarm-03-settings");click("铃声");save("alarm-04-sounds");click("试听",1);assert(HostTone==1&&HostToneVolume==60&&HostTonePriority==1&&HostToneDuration==8000);click("温柔钟声");click("使用此铃声");assert(!HostTonePriority);openMore();save("alarm-07-more");click("响铃时长");click("+");click("确认");click("完成");::Preferences::failNextWrites=1;click("保存并开启");assert(label("保存并开启")&&FocusService::snapshot().engine.count()==0);click("保存并开启");assert(FocusService::snapshot().engine.count()==1&&FocusService::snapshot().engine.state.alarms[0].tone==1);
    e=sample();set(e);show(App::Alarm);save("alarm-01-list");touch(270,98);assert(!FocusService::snapshot().engine.state.alarms[0].enabled);touch(270,98);assert(FocusService::snapshot().engine.state.alarms[0].enabled);click("07:30");click("下一步");openMore();click("删除闹钟");save("alarm-08-delete");click("取消");click("删除闹钟");click("确认删除");assert(FocusService::snapshot().engine.count()==1);
    // Ring over a different application; alarm priority preempts preview/chime.
    e=sample();set(e);show(App::Settings);HostEpoch=DeskLogic::beijingEpoch(2026,11,2,7,30);FocusApps::poll();assert(FocusService::snapshot().engine.runtime.ringing&&HostTonePriority==3&&HostToneVolume==60);save("alarm-05-ringing",false);click("稍后10分钟",0,true);FocusApps::poll();assert(!FocusService::snapshot().engine.runtime.ringing&&FocusService::snapshot().engine.state.alarms[0].snoozeEpoch==HostEpoch+600);::Preferences::failNextWrites=2;advance(600000);assert(FocusService::snapshot().engine.runtime.ringing);click("停止",0,true);FocusApps::poll();assert(!HostTonePriority&&!FocusService::snapshot().engine.runtime.ringing);advance(2000);assert(!FocusService::snapshot().writeFailed);
    e=Engine{};e.state.preferences.volume=40;e.state.session.settings=e.state.preferences;HostEpoch=DeskLogic::beijingEpoch(2026,11,1,23,0);e.state.daily[0]={dateKey(HostEpoch),2,50};set(e);show(App::Pomodoro);save("pomo-01-ready");click("开始专注");advance(438000);assert(label("17:42"));save("pomo-02-running");click("暂停");save("pomo-03-paused");advance(40000);assert(label("17:42"));click("继续专注");assert(label("17:42"));
    click("结束计时");save("pomo-09-end-confirm");assert(FocusService::snapshot().engine.state.session.stage==Stage::FocusPause);click("取消");assert(FocusService::snapshot().engine.state.session.stage==Stage::FocusRun);click("设置");save("pomo-06-durations");click("专注时长");click("+");click("确认");click("下一步");save("pomo-07-reminders");click("自动休息");click("保存设置");assert(FocusService::snapshot().engine.state.session.settings.focus==25&&FocusService::snapshot().engine.state.preferences.focus==26);assert(label("17:42"));
    auto remaining=FocusService::snapshot().engine.remaining(FocusService::monotonic());advance(uint32_t(remaining));assert(label("05:00")&&FocusService::snapshot().engine.today(HostEpoch).count==3);save("pomo-04-focus-complete");click("开始休息");advance(24000);save("pomo-05-resting");click("暂停休息");save("pomo-10-rest-paused");click("继续休息");click("跳过休息");assert(FocusService::snapshot().engine.state.session.round==2);click("开始专注");
    // Leaving and re-entering must preserve the running countdown.
    remaining=FocusService::snapshot().engine.remaining(FocusService::monotonic());show(App::Settings);advance(2000);show(App::Pomodoro);assert(FocusService::snapshot().engine.remaining(FocusService::monotonic())==remaining-2000);
    click("结束计时");click("结束计时",1);assert(FocusService::snapshot().engine.state.session.stage==Stage::Ready);click("切换预设");save("pomo-11-presets");click("深度预设");click("下一步");click("保存设置");assert(label("50:00"));
    e=Engine{};e.state.session.active=false;e.state.session.phase=Phase::LongRest;e.state.session.stage=Stage::Complete;e.state.session.completed=4;e.state.session.minutes=100;e.state.session.remaining=0;set(e);show(App::Pomodoro);save("pomo-08-complete");click("开始新一组");assert(label("25:00"));e=FocusService::snapshot().engine;e.restore();set(e);show(App::Pomodoro);save("pomo-12-interrupted");click("重新开始本轮");assert(label("25:00"));
    e=Engine{};e.state.preferences.volume=0;e.state.session.settings=e.state.preferences;set(e);show(App::Pomodoro);save("pomo-13-muted");assert(label("提示音已静音 · 仅显示完成提醒"));
    // Independent volume drafts, slider quantisation, cancellation and persistence.
    e=Engine{};set(e);show(App::Pomodoro);click("设置");click("下一步");click("提示音量");assert(label("20%"));save("pomo-14-volume");slideTo(0);assert(label("0%"));save("pomo-15-muted-volume");click("取消");assert(FocusService::snapshot().engine.state.preferences.volume==20);click("提示音量");slideTo(14);assert(label("15%")&&lv_slider_get_value(findSlider(root))==15);click("确认");click("保存设置");assert(FocusService::snapshot().engine.state.preferences.volume==15);assert(FocusService::snapshot().engine.state.session.settings.volume==15);
    // App-wide levels are independent of each other, alarms and session drafts.
    e=sample();set(e);show(App::Alarm);save("alarm-13-master-entry");click("设置");assert(label("闹钟总音量")&&label("20%"));save("alarm-11-master");slideTo(0);save("alarm-12-master-muted");click("取消");assert(FocusService::snapshot().engine.state.audio.alarm==20);click("设置");slideTo(5);::Preferences::failNextWrites=1;click("保存");assert(FocusService::snapshot().engine.state.audio.alarm==20&&HostAlarmMaster==20&&label("闹钟总音量"));click("保存");auto audioState=FocusService::snapshot().engine.state;assert(audioState.audio.alarm==5&&audioState.audio.pomodoro==20&&audioState.alarms[0].enabled&&audioState.alarms[0].volume==60&&HostAlarmMaster==5&&HostPomodoroMaster==20);assert(!FocusService::act(Action::AlarmMasterVolume,101));
    click("07:30");click("下一步");click("铃声");click("试听");assert(HostToneSource==AudioBoard::Reminder::Alarm&&HostTonePriority==1&&HostToneVolume==60&&HostAlarmMaster==5);assert(FocusService::act(Action::AlarmMasterVolume,0));unsigned calls=HostToneCalls;click("试听");assert(HostToneCalls==calls&&HostAlarmMaster==0);assert(ToneSynth::applyMaster(ToneSynth::pcm(0,100,HostToneVolume),HostAlarmMaster)==0);assert(FocusService::act(Action::AlarmMasterVolume,5));
    show(App::Pomodoro);click("设置");save("pomo-18-master-entry");click("总音量");assert(label("番茄总音量")&&label("20%"));save("pomo-16-master");slideTo(0);save("pomo-17-master-muted");click("取消");assert(FocusService::snapshot().engine.state.audio.pomodoro==20);click("总音量");slideTo(15);click("保存");audioState=FocusService::snapshot().engine.state;assert(audioState.audio.alarm==5&&audioState.audio.pomodoro==15&&audioState.preferences.volume==20&&HostAlarmMaster==5&&HostPomodoroMaster==15);click("下一步");click("提示铃声");click("试听");assert(HostToneSource==AudioBoard::Reminder::Pomodoro&&HostTonePriority==1&&HostToneVolume==20&&HostPomodoroMaster==15);assert(FocusService::act(Action::PomodoroMasterVolume,0));calls=HostToneCalls;click("试听");assert(HostToneCalls==calls&&HostAlarmMaster==5&&HostPomodoroMaster==0);assert(FocusService::act(Action::PomodoroMasterVolume,15));
    e=sample();e.state.audio={5,15};set(e);HostEpoch=DeskLogic::beijingEpoch(2026,11,2,7,30);FocusApps::poll();assert(HostTonePriority==3&&HostToneSource==AudioBoard::Reminder::Alarm&&HostAlarmMaster==5);e=Engine{};e.state.audio={5,15};set(e);assert(FocusService::act(Action::Start));advance(25*60000);assert(HostTonePriority==2&&HostToneSource==AudioBoard::Reminder::Pomodoro&&HostPomodoroMaster==15);
    // Both launcher sheets use the production layout; they are part of evidence.
    e=Engine{};set(e);GiftUI::setAppsPage(0);show(App::None);save("launcher-1",false);assert(GiftUI::swipeApps(-80));GiftUI::setAppsPage(1);show(App::None);save("launcher-2",false);
    for(const char *name:{"闹钟","番茄时钟","音乐","游戏","倒计时","倒数日"})assert(label(name));
    assert(!label("游戏厅")&&!label("小黑游戏厅"));
    std::cout<<"PASS: production widget save/cancel/delete/preview/volume/overlay, pause/resume/end/cancel/settings lock/background timing and both launcher sheets\n";
}
void setTimerWheels(unsigned h,unsigned m,unsigned sec){
    std::vector<lv_obj_t*> rollers;
    auto visit=[&](auto &self,lv_obj_t *p)->void{if(lv_obj_check_type(p,&lv_roller_class))rollers.push_back(p);for(unsigned i=0;i<lv_obj_get_child_cnt(p);++i)self(self,lv_obj_get_child(p,i));};
    visit(visit,root);assert(rollers.size()==3);unsigned values[]={h,m,sec};
    for(unsigned i=0;i<3;++i)lv_roller_set_selected(rollers[i],values[i],LV_ANIM_OFF);
    lv_event_send(rollers[2],LV_EVENT_VALUE_CHANGED,nullptr);TimerPage::refresh(true);
}
void timerTests(){
    using TA=TimerService::Action;using TS=TimerEngine::Stage;
    TimerEngine::Engine t;t.settings.seconds=0;assert(!t.start(0));t.settings.seconds=86400;assert(!t.start(0));
    t.settings.seconds=TimerEngine::MaxSeconds;uint64_t origin=(1ULL<<32)-500;
    assert(t.start(origin));assert(t.remaining(origin+1000)==86398000);assert(!t.start(origin));
    assert(t.pause(origin+1250));auto frozen=t.left;assert(t.remaining(origin+999999)==frozen);
    assert(t.start(origin+999999));assert(t.remaining(origin+999999)==frozen);
    auto end=t.deadline;t.tick(end-1);assert(t.stage==TS::Running);t.tick(end);assert(t.stage==TS::Finished&&t.left==0);
    auto revision=t.revision;t.tick(end+100000);assert(t.revision==revision);t.reset();assert(t.stage==TS::Ready);
    // An expired pause is a completion, never a resumable zero-second timer.
    t.settings.seconds=1;assert(t.start(0));assert(!t.pause(1000)&&t.stage==TS::Finished);
    ::Preferences nvs;assert(nvs.begin("timer-journal-test"));TimerStorage::Journal journal(nvs);TimerEngine::Settings st;
    assert(journal.load(st)&&journal.save(st));st.seconds=17;assert(journal.save(st));st.seconds=99;
    ::Preferences::partialNextWriteBytes=9;assert(!journal.save(st));TimerStorage::Journal recovered(nvs);
    assert(recovered.load(st)&&st.seconds==17);st.volume=101;assert(!journal.save(st));
    Engine e=sample();set(e);show(App::Alarm);auto count=e.count();click("删除");assert(label("确定删除这条闹钟？"));
    save("alarm-direct-delete");click("取消");assert(label("07:30")&&FocusService::snapshot().engine.count()==count);
    click("删除");::Preferences::failNextWrites=1;click("确认删除");assert(label("确认删除")&&FocusService::snapshot().engine.count()==count);
    click("确认删除");assert(FocusService::snapshot().engine.count()==count-1);
    // Deleting a snoozed/ringing alarm cancels all of its future runtime events.
    HostEpoch=DeskLogic::beijingEpoch(2026,11,2,7,29);e=sample();set(e);HostEpoch=DeskLogic::beijingEpoch(2026,11,2,7,30);FocusApps::poll();
    assert(FocusService::act(Action::SnoozeAlarm));show(App::Alarm);click("删除");click("确认删除");advance(600000);
    assert(!FocusService::snapshot().engine.runtime.ringing&&!FocusService::snapshot().engine.state.alarms[0].used);
    e=Engine{};set(e);show(App::Timer);save("timer-01-home");assert(label("常用计时器")&&label("刷牙")&&label("蒸蛋")&&label("面膜")&&label("午睡")&&!lv_obj_check_type(label("设置倒计时"),&lv_roller_class));
    click("设置倒计时");save("timer-02-duration");setTimerWheels(0,0,0);click("开始倒计时");assert(TimerService::snapshot().engine.stage==TS::Ready);
    setTimerWheels(0,0,42);click("存为常用");click("取消");assert(label("开始倒计时"));
    assert(TimerPage::back());TimerPage::refresh(true);assert(TimerService::snapshot().engine.settings.seconds==900&&label("面膜"));
    click("+ 添加");assert(label("开始倒计时"));
    setTimerWheels(23,59,59);click("开始倒计时");assert(TimerService::snapshot().engine.total==86399000);click("重置");click("确认");
    click("设置倒计时");setTimerWheels(0,0,3);click("存为常用");assert(label("添加常用计时器"));click("学习");save("timer-03-add-preset",false);click("保存常用");
    auto saved=TimerService::snapshot().engine.settings;assert(saved.presets[4].seconds==3&&!strcmp(saved.presets[4].name,"学习"));
    auto *extraCard=lv_obj_get_parent(label("学习"));lv_obj_scroll_to_view(extraCard,LV_ANIM_OFF);lv_obj_update_layout(root);lv_area_t extraArea;lv_obj_get_coords(extraCard,&extraArea);assert(extraArea.y1>=134&&extraArea.y2<239);
    click("学习");assert(TimerService::snapshot().engine.stage==TS::Running&&TimerService::snapshot().engine.total==3000&&!strcmp(TimerService::snapshot().engine.sessionName,"学习"));
    show(App::Settings);show(App::Timer);assert(label("学习")&&label("暂停"));
    click("重置");click("确认");
    auto *preset=lv_obj_get_parent(label("学习"));lv_event_send(preset,LV_EVENT_PRESSED,nullptr);lv_event_send(preset,LV_EVENT_LONG_PRESSED,nullptr);lv_event_send(preset,LV_EVENT_CLICKED,nullptr);TimerPage::refresh(true);assert(label("编辑常用计时器")&&TimerService::snapshot().engine.stage==TS::Ready);
    click("删除");click("取消");assert(TimerService::snapshot().engine.settings.presets[4].seconds==3);click("删除");click("确认");assert(!TimerService::snapshot().engine.settings.presets[4].seconds);
    click("提示音");save("timer-04-sounds");
    for(unsigned i=0;i<3;++i){click("试听",i);assert(HostTone==i&&HostToneSource==AudioBoard::Reminder::Timer&&HostTonePriority==1);}
    slideTo(35);click("渐快提醒");click("取消");assert(!HostTonePriority&&TimerService::snapshot().engine.settings.volume==20);
    click("提示音");slideTo(25);click("渐快提醒");click("保存");
    saved=TimerService::snapshot().engine.settings;assert(saved.tone==2&&saved.volume==25);
    click("设置倒计时");setTimerWheels(0,0,3);click("开始倒计时");assert(TimerService::snapshot().engine.total==3000);
    advance(1200);assert(label("00:00:02"));save("timer-05-running");click("暂停");
    auto left=TimerService::snapshot().engine.left;assert(left==1800);save("timer-06-paused");advance(5000);assert(TimerService::snapshot().engine.remaining(FocusService::monotonic())==left);
    click("继续倒计时");show(App::Settings);advance(1799);assert(TimerService::snapshot().engine.stage==TS::Running);advance(1);
    assert(TimerService::snapshot().ringing&&HostTonePriority==2&&HostToneSource==AudioBoard::Reminder::Timer);save("timer-06-background-finished",false);
    click("停止提醒",0,true);FocusApps::poll();assert(!TimerService::snapshot().ringing&&!HostTonePriority);
    show(App::Timer);assert(label("设置倒计时")&&TimerService::snapshot().engine.stage==TS::Ready);
    click("设置倒计时");setTimerWheels(0,0,3);click("开始倒计时");advance(3000);
    assert(TimerService::act(TA::Dismiss));FocusApps::poll();save("timer-07-finished");click("再来一次");show(App::Settings);
    advance(3000);assert(TimerService::snapshot().ringing);
    auto otherSettings=TimerService::snapshot().engine.settings;otherSettings.seconds=7;
    assert(TimerService::act(TA::Save,&otherSettings));click("再来一次",0,true);
    auto replayed=TimerService::snapshot().engine;assert(replayed.stage==TS::Running&&replayed.total==3000&&!strcmp(replayed.sessionName,"自定义"));
    FocusApps::poll();show(App::Timer);
    click("重置");save("timer-08-reset-confirm");click("取消");assert(TimerService::snapshot().engine.stage==TS::Running);click("重置");click("确认");assert(TimerService::snapshot().engine.stage==TS::Ready);
    // Failed/partial saves never replace current settings or start a countdown.
    saved=TimerService::snapshot().engine.settings;auto changed=saved;changed.seconds=7;
    ::Preferences::failNextWrites=1;assert(!TimerService::act(TA::Start,&changed));assert(TimerService::snapshot().engine.stage==TS::Ready&&TimerService::snapshot().engine.settings.seconds==saved.seconds);
    ::Preferences::partialNextWriteBytes=11;assert(!TimerService::act(TA::Save,&changed));assert(TimerService::snapshot().engine.settings.seconds==saved.seconds);
    changed.seconds=1;assert(TimerService::act(TA::Start,&changed));advance(1000);assert(HostToneSource==AudioBoard::Reminder::Timer);
    // Alarm wins; after stopping it the pending timer reminder is audible again.
    HostEpoch=DeskLogic::beijingEpoch(2026,11,2,7,29);e=sample();set(e);HostEpoch=DeskLogic::beijingEpoch(2026,11,2,7,30);FocusApps::poll();assert(HostTonePriority==3);
    assert(FocusService::act(Action::StopAlarm));FocusApps::poll();assert(HostTonePriority==2&&HostToneSource==AudioBoard::Reminder::Timer);
    assert(FocusService::act(Action::End));assert(HostTonePriority==2);assert(TimerService::act(TA::Dismiss));FocusApps::poll();
    AudioBoard::ringtone(1,20,3000,2,0,AudioBoard::Reminder::Pomodoro);assert(TimerService::act(TA::Reset));assert(HostTonePriority==2&&HostToneSource==AudioBoard::Reminder::Pomodoro);stopAudio();
    changed.volume=0;assert(TimerService::act(TA::Start,&changed));auto calls=HostToneCalls;advance(1000);assert(TimerService::snapshot().ringing&&HostToneCalls==calls);save("timer-09-muted",false);advance(60000);assert(!TimerService::snapshot().ringing);assert(TimerService::act(TA::Reset));
    GiftUI::setAppsPage(1);show(App::None);save("launcher-2",false);click("倒计时");assert(app==App::Timer);show(App::Timer);assert(GiftUI::back()&&app==App::None&&page==6);
    std::cout<<"PASS: timer 64-bit/deadline/pause/resume, settings/preset persistence and partial recovery, real wheels/presets/sounds/volume/reset/background/mute, alarm priority and direct delete\n";
}
int persistent(const std::string &mode){
    if(mode.rfind("timer-",0)==0){
        TimerService::begin();auto s=TimerService::snapshot();assert(s.storageReady);
        if(mode=="timer-seed"){auto p=s.engine.settings;p.seconds=3723;p.tone=2;p.volume=35;strcpy(p.presets[4].name,"学习");p.presets[4].seconds=42;assert(TimerService::act(TimerService::Action::Start,&p));}
        else if(mode=="timer-restore"){assert(s.engine.stage==TimerEngine::Stage::Ready&&s.engine.settings.seconds==3723&&s.engine.settings.tone==2&&s.engine.settings.volume==35&&s.engine.settings.presets[4].seconds==42);}
        else if(mode=="timer-delete"){auto p=s.engine.settings;p.presets[4]=TimerEngine::Preset{};assert(TimerService::act(TimerService::Action::Save,&p));}
        else if(mode=="timer-deleted"){assert(!s.engine.settings.presets[4].seconds);}
        else if(mode=="timer-failed"||mode=="timer-partial"){auto p=s.engine.settings;p.seconds=17;if(mode=="timer-failed")::Preferences::failNextWrites=1;else ::Preferences::partialNextWriteBytes=9;assert(!TimerService::act(TimerService::Action::Save,&p));assert(TimerService::snapshot().engine.settings.seconds==3723);}
        else return 2;std::cout<<"PASS process "<<mode<<"\n";return 0;
    }
    if(mode=="v2-seed"){std::ifstream f("hardware/tests/fixtures/focus-v2.bin",std::ios::binary);std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),{});assert(raw.size()>20);::Preferences old;assert(old.begin("focus"));assert(old.putBytes("focus0",raw.data(),raw.size())==raw.size());return 0;}
    if(mode=="legacy-seed"){::Preferences old;assert(old.begin("focus"));assert(old.putUShort("alarmMin",465));assert(old.putBool("alarmOn",true));assert(old.putUChar("alarmDays",62));assert(old.putUChar("focusMin",35));return 0;}
    if(mode=="corrupt-seed"){::Preferences old;assert(old.begin("focus"));unsigned char raw[]={1,2,3};assert(old.putBytes("focus0",raw,3)==3&&old.putBytes("focus1",raw,3)==3);return 0;}
    HostEpoch=DeskLogic::beijingEpoch(2026,11,2,7,29);FocusService::begin();auto s=FocusService::snapshot();if(mode=="corrupt-read"){assert(!s.storageReady);FocusService::poll();assert(!FocusService::snapshot().engine.runtime.ringing);return 0;}assert(s.storageReady);
    if(mode=="app-volume-seed"){assert(FocusService::act(Action::AlarmMasterVolume,5));assert(FocusService::act(Action::PomodoroMasterVolume,0));}
    else if(mode=="app-volume-restore"){assert(s.engine.state.audio.alarm==5&&s.engine.state.audio.pomodoro==0&&HostAlarmMaster==5&&HostPomodoroMaster==0);}
    else if(mode=="app-volume-failed"){::Preferences::failNextWrites=1;assert(!FocusService::act(Action::PomodoroMasterVolume,30));assert(FocusService::snapshot().engine.state.audio.pomodoro==0&&HostPomodoroMaster==0);}
    else if(mode=="app-volume-partial"){::Preferences::partialNextWriteBytes=9;assert(!FocusService::act(Action::AlarmMasterVolume,10));assert(FocusService::snapshot().engine.state.audio.alarm==5&&HostAlarmMaster==5);}
    else if(mode=="v2-read"){assert(s.engine.state.alarms[0].used&&s.engine.state.alarms[0].minute==450&&s.engine.state.alarms[0].volume==80&&s.engine.state.preferences.volume==15&&s.engine.state.audio.alarm==20&&s.engine.state.audio.pomodoro==20);}
    else if(mode=="seed"){Alarm a;a.enabled=true;a.once=false;a.days=62;a.tone=2;a.volume=80;assert(FocusService::act(Action::SaveAlarm,0,&a));auto prefs=s.engine.state.preferences;prefs.volume=15;assert(FocusService::act(Action::Configure,0,nullptr,&prefs));assert(FocusService::act(Action::Start));}
    else if(mode=="restore-fire"){assert(s.engine.state.alarms[0].enabled&&s.engine.state.alarms[0].tone==2&&s.engine.state.alarms[0].volume==80);assert(s.engine.state.session.stage==Stage::Interrupted&&s.engine.state.preferences.volume==15);HostEpoch+=60;FocusService::poll();assert(FocusService::snapshot().engine.runtime.ringing);assert(FocusService::act(Action::SnoozeAlarm));}
    else if(mode=="restore-snooze"){HostEpoch+=660;FocusService::poll();assert(FocusService::snapshot().engine.runtime.ringing);assert(FocusService::act(Action::StopAlarm));}
    else if(mode=="deduplicate"){HostEpoch+=660;FocusService::poll();assert(!FocusService::snapshot().engine.runtime.ringing);assert(FocusService::act(Action::ToggleAlarm,0));}
    else if(mode=="disabled"){assert(!s.engine.state.alarms[0].enabled);HostEpoch+=86460;FocusService::poll();assert(!FocusService::snapshot().engine.runtime.ringing);}
    else if(mode=="delete-alarm"){assert(FocusService::act(Action::DeleteAlarm,0));}
    else if(mode=="verify-deleted"){assert(!s.engine.state.alarms[0].used);HostEpoch+=86460;FocusService::poll();assert(!FocusService::snapshot().engine.runtime.ringing);}
    else if(mode=="legacy-read"){assert(s.engine.state.alarms[0].enabled&&s.engine.state.alarms[0].minute==465&&s.engine.state.alarms[0].days==62&&s.engine.state.preferences.focus==35);}
    else if(mode=="partial-edit"){auto a=s.engine.state.alarms[0];a.minute=500;::Preferences::partialNextWriteBytes=9;assert(!FocusService::act(Action::SaveAlarm,0,&a));assert(FocusService::snapshot().engine.state.alarms[0].minute==450);}
    else if(mode=="failed-edit"){auto a=s.engine.state.alarms[0];a.minute=500;::Preferences::failNextWrites=1;assert(!FocusService::act(Action::SaveAlarm,0,&a));assert(FocusService::snapshot().engine.state.alarms[0].minute==450);}
    else if(mode=="verify-old"){assert(s.engine.state.alarms[0].minute==450);}
    else return 2;std::cout<<"PASS process "<<mode<<"\n";return 0;
}
int main(int argc,char **argv){
    std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);
#ifdef _WIN32
    _set_error_mode(1); // Assertions must go to stderr, never a modal dialog.
#endif
    if(argc>2&&std::string(argv[1])=="--mode")return persistent(argv[2]);
    if(argc<2)return 2;out=argv[1];std::filesystem::create_directories(out);LittleFS.directory=out+"/store";
    lv_init();static lv_disp_draw_buf_t drawbuf;lv_disp_draw_buf_init(&drawbuf,buffer,nullptr,320*40);static lv_disp_drv_t driver;lv_disp_drv_init(&driver);driver.hor_res=320;driver.ver_res=240;driver.draw_buf=&drawbuf;driver.flush_cb=flush;lv_disp_drv_register(&driver);root=lv_scr_act();lv_obj_remove_style_all(root);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);Ui::setRoot(root);Ui::setActionHandler([](lv_event_t *e){if(uintptr_t(lv_event_get_user_data(e))==Ui::Back){if(!GiftUI::back()){app=App::None;page=6;renderPending=true;}}});assert(GiftData::begin());
    engineTests();uiTests();timerTests();std::cout<<"PASS: "<<frames<<" screenshots from firmware widgets, no serial port opened or firmware flashed\n";
}
