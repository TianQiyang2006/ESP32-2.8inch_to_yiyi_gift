// Production split-flap renderer and persistent theme, driven through LVGL's
// pointer driver. Only hardware/time/storage fault injection is substituted.
#include "Arduino.h"
#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

extern int HostNoticeCount;
extern bool HostNoticeError;
extern bool HostTouchSwiped;
extern bool HostFailFlipAllocation;
int64_t HostEpoch=DeskLogic::beijingEpoch(2026,11,13,9,59)+59;
static lv_color_t framebuffer[320*240],drawbuffer[320*40];
static lv_indev_t *pointer;
static lv_point_t finger;
static bool fingerDown=false;
static std::string out;
static unsigned checks=0,frames=0;
#define CHECK(value) do {++checks; if(!(value)){std::cerr<<"FAILED line "<<__LINE__<<": "<<#value<<"\n";std::abort();}} while(0)

static void flush(lv_disp_drv_t *drv,const lv_area_t *a,lv_color_t *pixels){
 for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){
  if(x>=0&&x<320&&y>=0&&y<240)framebuffer[y*320+x]=*pixels;++pixels;
 }
 lv_disp_flush_ready(drv);
}
static void readPointer(lv_indev_drv_t *,lv_indev_data_t *data){
 data->point=finger;data->state=fingerDown?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;
}
static void advance(unsigned ms){
 while(ms){unsigned step=std::min(ms,5u);ms-=step;HostMillis+=step;lv_tick_inc(step);lv_timer_handler();}
 GiftUI::refresh(true);lv_obj_update_layout(root);lv_refr_now(nullptr);
}
static void sample(int x,int y,bool down){
 finger.x=x;finger.y=y;fingerDown=down;
 lv_timer_ready(pointer->driver->read_timer);advance(5);
}
static void tap(int x=160,int y=116){sample(x,y,true);sample(x,y,false);advance(40);}
static void doubleTap(int x=160,int y=116){tap(x,y);advance(90);tap(x,y);}
static std::vector<lv_color_t> pixels(){lv_refr_now(nullptr);return {framebuffer,framebuffer+320*240};}
static bool same(const std::vector<lv_color_t> &a,const std::vector<lv_color_t> &b){
 if(a.size()!=b.size())return false;for(unsigned i=0;i<a.size();i++)if(a[i].full!=b[i].full)return false;return true;
}
static std::string readFile(const std::string &name){
 std::ifstream input(name,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};
}
static lv_obj_t *label(lv_obj_t *object,const char *text){
 if(lv_obj_check_type(object,&lv_label_class)&&!strcmp(lv_label_get_text(object),text))return object;
 for(unsigned i=0;i<lv_obj_get_child_cnt(object);i++)if(auto *found=label(lv_obj_get_child(object,i),text))return found;
 return nullptr;
}
static unsigned timers(){unsigned n=0;for(auto *t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static void clean(){
 fingerDown=false;lv_indev_reset(pointer,nullptr);GiftUI::leaveView();Ui::cancelAnimations();Ui::clearStatusCluster();
 lv_obj_clean(root);SavedDesign::invalidate();CHECK(!FlipClockPage::active());
}
static void rebuild(){
 clean();Ui::setRoot(root);Ui::setPage(page);CHECK(GiftUI::render(root));
 GiftUI::refresh(true);lv_obj_update_layout(root);renderPending=false;lv_refr_now(nullptr);
}
static void open(){
 GiftUI::reset();app=App::Clock;page=6;rebuild();
 auto *target=label(root,"白色翻页");CHECK(target);
 while(target&&!lv_obj_has_flag(target,LV_OBJ_FLAG_CLICKABLE))target=lv_obj_get_parent(target);
 CHECK(target);lv_area_t a;lv_obj_get_coords(target,&a);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);
 CHECK(renderPending);rebuild();CHECK(FlipClockPage::active());
}
static void verifyTheme(bool dark){
 CHECK(FlipClockPage::darkMode()==dark);
 CHECK(bool(GiftData::get("clockMode")["flipDark"]|false)==dark);
 lv_refr_now(nullptr);CHECK(framebuffer[20*320+160].full==lv_color_hex(dark?0x101216:0xfaf9f7).full);
 CHECK(framebuffer[64*320+20].full==lv_color_hex(dark?0x252a32:0xffffff).full);
 auto *date=label(root,"2026年 11月 13日 周五");
 if(date)CHECK(lv_obj_get_style_text_color(date,0).full==lv_color_hex(dark?0xf0f3f7:0x171717).full);
 auto *weatherLabel=label(root,"多云 18°C");CHECK(weatherLabel);
 CHECK(lv_obj_get_style_text_color(weatherLabel,0).full==lv_color_hex(dark?0xb0bac8:0x8a8a86).full);
}
static void save(const char *name){
 lv_refr_now(nullptr);std::ofstream file(out+"/"+name+".rgb565",std::ios::binary);
 file.write((const char*)framebuffer,sizeof(framebuffer));++frames;
}
static void writeReport(const char *mode){
 std::ofstream file(out+"/flip-theme-"+mode+".json");
 file<<"{\"checks\":"<<checks<<",\"frames\":"<<frames<<",\"dark\":"<<(FlipClockPage::darkMode()?"true":"false")<<"}\n";
 std::cout<<"PASS: split-flap "<<mode<<", "<<checks<<" checks, "<<frames<<" frames.\n";
}
int main(int argc,char **argv){
 CHECK(argc>=2);out=argv[1];std::filesystem::create_directories(out+"/store");LittleFS.directory=out+"/store";
 const bool reloaded=argc>2&&!strcmp(argv[2],"reload");
 if(!reloaded){
  std::ofstream old(LittleFS.directory+"/gift.json");
  old<<R"({"clockMode":{"preset":"compact","seconds":false},"letter":{"title":"保留标题"}})";
 }
 lv_init();static lv_disp_draw_buf_t buf;lv_disp_draw_buf_init(&buf,drawbuffer,nullptr,320*40);
 static lv_disp_drv_t drv;lv_disp_drv_init(&drv);drv.hor_res=320;drv.ver_res=240;drv.draw_buf=&buf;drv.flush_cb=flush;lv_disp_drv_register(&drv);
 static lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;input.read_cb=readPointer;pointer=lv_indev_drv_register(&input);
 root=lv_scr_act();lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);Ui::setRoot(root);
 Ui::setActionHandler([](lv_event_t *e){if((uintptr_t)lv_event_get_user_data(e)==Ui::Back)GiftUI::back();});
 CHECK(GiftData::begin());settings.city="昆明";weather.valid=true;weather.temperature=18;weather.condition="多云";
 CHECK(GiftData::get("letter")["title"]=="保留标题");
 const unsigned baseTimers=timers();
 if(reloaded){
  CHECK(GiftData::get("clockMode")["flipDark"]==true);open();verifyTheme(true);save("flip-dark-after-restart");
  CHECK(GiftUI::back());rebuild();CHECK(!FlipClockPage::active());CHECK(timers()==baseTimers);writeReport("reload");return 0;
 }
 CHECK(GiftData::get("clockMode")["preset"]=="compact");CHECK(GiftData::get("clockMode")["seconds"]==true);
 CHECK(GiftData::get("clockMode")["flipDark"]==false);open();verifyTheme(false);save("flip-light");
 const auto initial=pixels();auto revision=GiftData::revision();
 tap();advance(400);CHECK(GiftData::revision()==revision);verifyTheme(false);CHECK(same(initial,pixels()));
 tap();advance(330);tap();advance(330);CHECK(GiftData::revision()==revision);verifyTheme(false);
 // Actual presses on digits, date, weather and empty space all reach the screen.
 for(auto point:std::vector<lv_point_t>{{160,116},{160,202},{160,222},{160,20}}){
  const bool old=FlipClockPage::darkMode();revision=GiftData::revision();doubleTap(point.x,point.y);
  CHECK(GiftData::revision()==revision+1);verifyTheme(!old);advance(400);
 }
 CHECK(!FlipClockPage::darkMode());revision=GiftData::revision();doubleTap(8,116);advance(400);
 CHECK(GiftData::revision()==revision);verifyTheme(false);
 // The physical driver reports swipe travel on release, even though LVGL's
 // full-screen non-scrollable object can still generate CLICKED afterwards.
 sample(60,116,true);sample(120,116,true);sample(200,116,true);HostTouchSwiped=true;
 sample(200,116,false);advance(40);HostTouchSwiped=false;tap();advance(400);
 CHECK(GiftData::revision()==revision);verifyTheme(false);
 doubleTap();verifyTheme(true);save("flip-dark");CHECK(!same(initial,pixels()));advance(400);
 // Persisted theme survives view re-entry and keeps all other settings.
 const auto dark=pixels();open();verifyTheme(true);CHECK(same(dark,pixels()));CHECK(GiftData::get("letter")["title"]=="保留标题");
 JsonDocument invalid;invalid["clockMode"]["flipDark"]="true";String error;revision=GiftData::revision();
 CHECK(!GiftData::patch(invalid.as<JsonVariantConst>(),error));CHECK(GiftData::revision()==revision);verifyTheme(true);
 // Three independent atomic-save failure paths keep current pixels and disk.
 for(int fault=0;fault<3;fault++){
  const auto before=pixels();const auto disk=readFile(LittleFS.directory+"/gift.json");
  const auto notices=HostNoticeCount;revision=GiftData::revision();
  if(fault==0)LittleFS.failWrites=true;
  if(fault==1)LittleFS.writeLimit=7;
  if(fault==2)LittleFS.failRenameFrom="/gift.tmp";
  doubleTap();CHECK(FlipClockPage::storageFailed());CHECK(HostNoticeCount==notices+1&&HostNoticeError);
  CHECK(GiftData::revision()==revision);verifyTheme(true);CHECK(same(before,pixels()));
  CHECK(readFile(LittleFS.directory+"/gift.json")==disk);LittleFS.resetFaults();advance(400);
 }
 doubleTap();verifyTheme(false);CHECK(!FlipClockPage::storageFailed());advance(400);
 // Theme switching during all six flaps repaints their correct target once.
 HostEpoch++;FlipClockPage::refresh();CHECK(FlipClockPage::movingDigits()==6);advance(100);save("flip-light-folding");
 doubleTap();verifyTheme(true);CHECK(FlipClockPage::movingDigits()==0);save("flip-dark-mid-fold-switch");
 auto final=pixels();open();CHECK(same(final,pixels()));
 HostEpoch++;FlipClockPage::refresh();CHECK(FlipClockPage::movingDigits()==1);
 advance(100);save("flip-dark-folding");advance(100);save("flip-dark-hinge");advance(100);save("flip-dark-unfolding");advance(140);
 CHECK(FlipClockPage::movingDigits()==0);save("flip-dark-settled");final=pixels();open();CHECK(same(final,pixels()));
 HostEpoch=DeskLogic::beijingEpoch(2026,11,13,23,59)+59;FlipClockPage::refresh();advance(440);
 HostEpoch++;FlipClockPage::refresh();advance(440);CHECK(label(root,"2026年 11月 14日 周六"));save("flip-dark-midnight");
 HostEpoch=0;FlipClockPage::refresh();CHECK(FlipClockPage::movingDigits()==0);CHECK(label(root,"等待联网校时"));save("flip-dark-invalid-time");
 HostEpoch=DeskLogic::beijingEpoch(2026,11,13,9,59)+59;FlipClockPage::refresh();CHECK(FlipClockPage::movingDigits()==0);
 // Delete pending taps and active animations; no stale callback or timer leaks.
 for(int i=0;i<30;i++){
  open();doubleTap();HostEpoch++;advance(100);tap();clean();CHECK(timers()==baseTimers);
  advance(500);CHECK(!FlipClockPage::active()&&FlipClockPage::movingDigits()==0);
 }
 // Raster OOM keeps themed static text readable. A successful retry during a
 // later theme switch must also wire all flap pointers before animation resumes.
 HostFailFlipAllocation=true;open();verifyTheme(true);save("flip-dark-oom-fallback");
 HostEpoch++;FlipClockPage::refresh();CHECK(FlipClockPage::movingDigits()==0);
 doubleTap();verifyTheme(false);save("flip-light-oom-fallback");advance(400);
 HostFailFlipAllocation=false;doubleTap();verifyTheme(true);HostEpoch++;FlipClockPage::refresh();
 CHECK(FlipClockPage::movingDigits()>0);advance(100);save("flip-dark-oom-recovered-folding");advance(440);
 final=pixels();open();CHECK(same(final,pixels()));
 if(!FlipClockPage::darkMode()){doubleTap();advance(400);}verifyTheme(true);
 save("flip-dark-final");CHECK(GiftData::get("letter")["title"]=="保留标题");writeReport("main");
 clean();CHECK(timers()==baseTimers);
}
