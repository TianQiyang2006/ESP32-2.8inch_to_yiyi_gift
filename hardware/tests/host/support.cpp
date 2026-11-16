#include "Arduino.h"
#include "hardware/arduino/yiyi_desk/src/pages/music_page.h"
#include "hardware/arduino/yiyi_desk/src/pages/radio_page.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/display.h"
#include "hardware/arduino/yiyi_desk/src/audio_board.h"
#include "hardware/arduino/yiyi_desk/src/xiaozhi_voice.h"
#include "hardware/arduino/yiyi_desk/src/touch.h"
#include "hardware/arduino/yiyi_desk/src/pages/page_api.h"
#include "hardware/arduino/yiyi_desk/src/nav_logic.h"
// Host builds do not compile desk.cpp, so the navigation entry points that desk.cpp
// implements are stubs for the offscreen runners. App entries and returns use
// the production origin-table helpers so nested launcher navigation is exercised.
namespace { NavOrigin hostOrigins[NavAppCount]; }
namespace GiftPages {
void fire(int) {}
void openApp(int targetApp) {
    if (targetApp < 0 || targetApp >= NavAppCount) return;
    navRecord(hostOrigins, targetApp, app == App::None ? -1 : (int)app, page);
    app = (App)targetApp;
    lastInputAt = millis();
    if (app == App::Voice) XiaozhiVoice::launch();
#ifndef GIFT_LOGIC_TEST
    // Logic-only suites do not link the LVGL music page. UI suites enter it.
    if (app == App::Music) MusicPage::enter();
    if (app == App::Radio) RadioPage::enter();
#endif
    renderPending = true;
}
void openPage(int targetPage) {
    if (targetPage < 0 || targetPage > 6) return;
    app = App::None;
    page = (uint8_t)targetPage;
    lastInputAt = millis();
    renderPending = true;
}
bool canGoBack() { return false; }
}  // namespace GiftPages
namespace GiftUI {
bool hostAppBack() {
    if (app == App::None) return false;
    if (app == App::Voice) XiaozhiVoice::close();
    int originApp = -1, originPage = 6;
    navResolve(hostOrigins, (int)app, &originApp, &originPage);
    app = originApp < 0 ? App::None : (App)originApp;
    page = (uint8_t)originPage;
    renderPending = true;
    return true;
}
}  // namespace GiftUI
uint32_t HostMillis=10000;int HostBatteryMv=1850;int HostKnocks=0,HostBrightness=255;
extern int64_t HostEpoch;
int HostNoticeCount=0;
String HostNotice;
bool HostNoticeError=false;
void notifyUser(const String &message,bool error){++HostNoticeCount;HostNotice=message;HostNoticeError=error;}
namespace ClockSource {int64_t now(){return HostEpoch;}}
namespace Display {void setBrightness(uint8_t v){HostBrightness=v;}}
namespace Touch {bool swiped(){return false;}}
unsigned HostTone=0,HostToneVolume=0,HostTonePriority=0,HostToneDuration=0,HostToneCalls=0;
unsigned HostAlarmMaster=20,HostPomodoroMaster=20;
AudioBoard::Reminder HostToneSource=AudioBoard::Reminder::Alarm;
bool HostAudioReady=true;
namespace AudioBoard {
void knock(){++HostKnocks;}void setVolume(int){}bool playing(){return HostTonePriority!=0;}
bool ready(){return HostAudioReady;}String error(){return HostAudioReady?String(""):String("音频故障");}
bool ringtone(unsigned tone,unsigned percent,uint32_t duration,unsigned priority,unsigned,Reminder source){if(!HostAudioReady||priority<HostTonePriority)return false;HostTone=tone;HostToneVolume=percent;HostToneDuration=duration;HostTonePriority=priority;HostToneSource=source;++HostToneCalls;return true;}
void setReminderVolume(Reminder source,unsigned percent){if(source==Reminder::Timer)return;if(source==Reminder::Pomodoro)HostPomodoroMaster=percent;else HostAlarmMaster=percent;}
void stopRingtone(unsigned priority){if(priority==HostTonePriority)HostTonePriority=0;}
void stopRingtone(unsigned priority,Reminder source){if(priority==HostTonePriority&&source==HostToneSource)HostTonePriority=0;}
void protect(bool,unsigned){}
}
namespace XiaozhiVoice {Snapshot current;int launches=0;void launch(){++launches;current.stage=Stage::Connecting;current.message="正在打开官方小智…";}void close(){current.stage=Stage::Idle;}Snapshot get(){return current;}}
