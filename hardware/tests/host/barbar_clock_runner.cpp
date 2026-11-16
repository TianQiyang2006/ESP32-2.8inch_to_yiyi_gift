// Production Barbar clock, picker and atomic preferences through real LVGL
// drawing, pointer reads, events and timer dispatch. No clock widgets are mocked.
#include "Arduino.h"
#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/pages/assets/barbar_clock_weather16.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

extern int HostNoticeCount;
extern bool HostNoticeError,HostTouchSwiped;
int64_t HostEpoch=DeskLogic::beijingEpoch(2026,11,15,9,59)+59;
static lv_color_t framebuffer[320*240],drawbuffer[320*40];
static lv_indev_t *pointer;
static lv_point_t finger;
static bool fingerDown=false;
static std::string out;
static unsigned checks=0,frames=0;
static unsigned weatherScenes=0;
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
static lv_obj_t *findWeatherIcon(lv_obj_t *object){
 if(lv_obj_check_type(object,&lv_label_class)&&lv_obj_get_style_text_font(object,0)==&barbar_clock_weather16)return object;
 for(unsigned i=0;i<lv_obj_get_child_cnt(object);i++)if(auto *found=findWeatherIcon(lv_obj_get_child(object,i)))return found;
 return nullptr;
}
static unsigned timers(){unsigned n=0;for(auto *t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static void clean(){
 fingerDown=false;lv_indev_reset(pointer,nullptr);GiftUI::leaveView();Ui::cancelAnimations();Ui::clearStatusCluster();
 lv_obj_clean(root);SavedDesign::invalidate();CHECK(!BarbarClockPage::active());
}
static void rebuild(){
 clean();Ui::setRoot(root);Ui::setPage(page);CHECK(GiftUI::render(root));
 GiftUI::refresh(true);lv_obj_update_layout(root);renderPending=false;lv_refr_now(nullptr);
}
static void caption(const char *text){
 auto *target=label(root,text);CHECK(target);
 while(target&&!lv_obj_has_flag(target,LV_OBJ_FLAG_CLICKABLE))target=lv_obj_get_parent(target);
 CHECK(target);lv_area_t a;lv_obj_get_coords(target,&a);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);
 CHECK(renderPending);rebuild();
}
static void picker(){GiftUI::reset();app=App::Clock;page=6;rebuild();}
static void open(){
 picker();CHECK(!label(root,"Barbar 彩条钟"));caption(">");CHECK(label(root,"Barbar 彩条钟"));
 caption("Barbar 彩条钟");CHECK(BarbarClockPage::active());advance(700);
}
static void verifySeconds(bool shown){
 CHECK(BarbarClockPage::showSeconds()==shown);
 CHECK(bool(GiftData::get("clockMode")["barbarSeconds"]|true)==shown);
 CHECK(GiftData::get("clockMode")["preset"]=="barbar");
 CHECK(GiftData::get("clockMode")["flipDark"]==true);
 CHECK(GiftData::get("letter")["title"]=="保留标题");
}
static void verifyColumns(bool shown){
 lv_refr_now(nullptr);unsigned runs=0;bool inside=false;
 for(int x=0;x<320;x++){
  const bool cyan=framebuffer[8*320+x].full==lv_color_hex(0x00c2b7).full;
  if(cyan&&!inside)++runs;inside=cyan;
 }
 CHECK(runs==(shown?57u:39u));
}
static void save(const char *name){
 lv_refr_now(nullptr);std::ofstream file(out+"/"+name+".rgb565",std::ios::binary);
 file.write((const char*)framebuffer,sizeof(framebuffer));++frames;
}
static std::vector<lv_color_t> weatherPixels(){
 lv_refr_now(nullptr);std::vector<lv_color_t> crop;
 for(int y=206;y<232;y++)for(int x=10;x<37;x++)crop.push_back(framebuffer[y*320+x]);
 return crop;
}
static void verifyWeather(){
 struct Scene{const char *name,*condition,*glyph;int hour;unsigned codepoint;};
 const Scene cases[]={
  {"cloudy","多云","\xef\x80\x93",12,0xf013},
  {"foggy","雾","\xef\x81\x8a",12,0xf04a},
  {"rainy","小雨","\xef\x80\x99",12,0xf019},
  {"snowy","小雪","\xef\x80\x9b",12,0xf01b},
  {"clear-day","晴","\xef\x80\x8d",12,0xf00d},
  {"clear-night","晴","\xef\x80\xae",22,0xf02e},
  {"thunderstorm","雷阵雨","\xef\x80\x9e",12,0xf01e},
  {"windy","大风","\xef\x81\x90",12,0xf050}
 };
 const auto savedEpoch=HostEpoch;const auto savedCondition=weather.condition;const auto savedValid=weather.valid;
 std::vector<std::vector<lv_color_t>> crops;weather.valid=true;
 for(const auto &scene:cases){
  HostEpoch=DeskLogic::beijingEpoch(2026,11,15,scene.hour,30);
  weather.condition=scene.condition;BarbarClockPage::refresh();advance(700);
  auto *icon=findWeatherIcon(root);CHECK(icon);CHECK(!lv_obj_has_flag(icon,LV_OBJ_FLAG_HIDDEN));
  CHECK(!strcmp(lv_label_get_text(icon),scene.glyph));
  lv_font_glyph_dsc_t glyph;CHECK(lv_font_get_glyph_dsc(&barbar_clock_weather16,&glyph,scene.codepoint,0));
  CHECK(glyph.box_w>0&&glyph.box_h>0);
  auto crop=weatherPixels();unsigned ink=0;
  for(const auto &pixel:crop)if(pixel.full==lv_color_hex(0x00c2b7).full)++ink;
  CHECK(ink>5);
  for(const auto &previous:crops)CHECK(!same(previous,crop));
  crops.push_back(crop);std::string filename=std::string("barbar-weather-")+scene.name;
  save(filename.c_str());++weatherScenes;
 }
 // Upstream intended night begins at 22:00 and ends after hour 5. Compare
 // actual selected source glyphs at both boundaries, including the last minute.
 const int hours[]={5,6,21,22};
 for(int hour:hours){
  HostEpoch=DeskLogic::beijingEpoch(2026,11,15,hour,hour==5?59:0);weather.condition="晴";
  BarbarClockPage::refresh();advance(700);auto *icon=findWeatherIcon(root);CHECK(icon);
  CHECK(!strcmp(lv_label_get_text(icon),hour==5||hour==22?"\xef\x80\xae":"\xef\x80\x8d"));
 }
 weather.valid=false;BarbarClockPage::refresh();advance(700);auto *icon=findWeatherIcon(root);CHECK(icon);
 CHECK(lv_obj_has_flag(icon,LV_OBJ_FLAG_HIDDEN));CHECK(!strcmp(lv_label_get_text(icon),""));
 CHECK(label(root,"--°C"));save("barbar-weather-no-data");++weatherScenes;
 weather.valid=savedValid;weather.condition=savedCondition;HostEpoch=savedEpoch;BarbarClockPage::refresh();advance(700);
}
static void writeReport(const char *mode){
 std::ofstream file(out+"/barbar-"+mode+".json");
 file<<"{\"passed\":true,\"checks\":"<<checks<<",\"frames\":"<<frames<<",\"weather_scenes\":"<<weatherScenes<<",\"seconds\":"<<(BarbarClockPage::showSeconds()?"true":"false")<<",\"pointer_driver\":true}\n";
 std::cout<<"PASS: Barbar "<<mode<<", "<<checks<<" checks, "<<frames<<" frames.\n";
}
int main(int argc,char **argv){
 CHECK(argc>=2);out=argv[1];std::filesystem::create_directories(out+"/store");LittleFS.directory=out+"/store";
 const bool reloaded=argc>2&&!strcmp(argv[2],"reload");
 if(!reloaded){
  std::ofstream old(LittleFS.directory+"/gift.json");
  old<<R"({"clockMode":{"preset":"compact","seconds":false,"flipDark":true},"letter":{"title":"保留标题"}})";
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
  CHECK(GiftData::get("clockMode")["barbarSeconds"]==false);open();verifySeconds(false);save("barbar-hidden-after-restart");
  CHECK(GiftUI::back());rebuild();CHECK(!BarbarClockPage::active());CHECK(timers()==baseTimers);writeReport("reload");return 0;
 }
 CHECK(GiftData::get("clockMode")["preset"]=="compact");CHECK(GiftData::get("clockMode")["seconds"]==true);
 CHECK(GiftData::get("clockMode")["barbarSeconds"]==true);
 picker();save("clock-picker-first");caption(">");save("clock-picker-barbar");
 caption("<");CHECK(label(root,"白色翻页"));caption(">");
 // Failing the preset save must leave the picker and original selection intact.
 LittleFS.failWrites=true;caption("Barbar 彩条钟");CHECK(!BarbarClockPage::active());
 CHECK(GiftData::get("clockMode")["preset"]=="compact");LittleFS.resetFaults();
 caption("Barbar 彩条钟");CHECK(BarbarClockPage::active());advance(700);verifySeconds(true);verifyColumns(true);save("barbar-seconds");
 const auto initial=pixels();auto revision=GiftData::revision();
 tap();advance(400);CHECK(GiftData::revision()==revision);verifySeconds(true);CHECK(same(initial,pixels()));
 tap();advance(330);tap();advance(330);CHECK(GiftData::revision()==revision);verifySeconds(true);
 // Digits, upper/lower bars and blank background must all reach one handler.
 for(auto point:std::vector<lv_point_t>{{160,116},{160,202},{160,222},{160,20}}){
  const bool old=BarbarClockPage::showSeconds();revision=GiftData::revision();doubleTap(point.x,point.y);
  CHECK(GiftData::revision()==revision+1);verifySeconds(!old);advance(700);
 }
 verifySeconds(true);revision=GiftData::revision();doubleTap(8,116);advance(400);
 CHECK(GiftData::revision()==revision);verifySeconds(true);
 sample(60,116,true);sample(120,116,true);sample(200,116,true);HostTouchSwiped=true;
 sample(200,116,false);advance(40);HostTouchSwiped=false;tap();advance(400);
 CHECK(GiftData::revision()==revision);verifySeconds(true);
 doubleTap();verifySeconds(false);advance(700);verifyColumns(false);save("barbar-hidden");CHECK(!same(initial,pixels()));
 const auto hidden=pixels();open();verifySeconds(false);CHECK(same(hidden,pixels()));
 // Back restores the sheet that owns Barbar, rather than stranding its entry.
 CHECK(GiftUI::back());rebuild();CHECK(label(root,"Barbar 彩条钟"));CHECK(!label(root,"白色翻页"));
 caption("Barbar 彩条钟");advance(700);verifySeconds(false);
 JsonDocument invalid;invalid["clockMode"]["barbarSeconds"]="false";String error;revision=GiftData::revision();
 CHECK(!GiftData::patch(invalid.as<JsonVariantConst>(),error));CHECK(GiftData::revision()==revision);verifySeconds(false);
 for(int fault=0;fault<3;fault++){
  const auto before=pixels();const auto disk=readFile(LittleFS.directory+"/gift.json");
  const auto notices=HostNoticeCount;revision=GiftData::revision();
  if(fault==0)LittleFS.failWrites=true;
  if(fault==1)LittleFS.writeLimit=7;
  if(fault==2)LittleFS.failRenameFrom="/gift.tmp";
  doubleTap();CHECK(BarbarClockPage::storageFailed());CHECK(HostNoticeCount==notices+1&&HostNoticeError);
  CHECK(GiftData::revision()==revision);verifySeconds(false);CHECK(same(before,pixels()));
  CHECK(readFile(LittleFS.directory+"/gift.json")==disk);LittleFS.resetFaults();advance(400);
 }
 doubleTap();verifySeconds(true);CHECK(!BarbarClockPage::storageFailed());advance(700);verifyColumns(true);save("barbar-restored");
 CHECK(same(initial,pixels()));
 // Time rollover and invalid recovery end on the exact fresh-view rendering.
 HostEpoch=DeskLogic::beijingEpoch(2026,11,15,23,59)+59;BarbarClockPage::refresh();advance(700);save("barbar-before-midnight");
 HostEpoch++;BarbarClockPage::refresh();advance(120);save("barbar-midnight-moving");advance(700);save("barbar-midnight");
 auto settled=pixels();open();CHECK(same(settled,pixels()));
 HostEpoch=0;BarbarClockPage::refresh();advance(700);save("barbar-invalid-time");
 settled=pixels();open();CHECK(same(settled,pixels()));
 HostEpoch=DeskLogic::beijingEpoch(2026,11,15,9,59)+59;BarbarClockPage::refresh();advance(700);
 settled=pixels();open();CHECK(same(settled,pixels()));save("barbar-time-restored");
 // A hidden clock must not change pixels merely because seconds have advanced.
 doubleTap();advance(700);verifySeconds(false);settled=pixels();HostEpoch-=29;BarbarClockPage::refresh();advance(700);
 CHECK(same(settled,pixels()));save("barbar-hidden-static");
 doubleTap();advance(700);verifySeconds(true);settled=pixels();HostEpoch++;BarbarClockPage::refresh();advance(700);
 CHECK(!same(settled,pixels()));save("barbar-next-second");
 verifyWeather();
 for(int i=0;i<30;i++){
  open();doubleTap();HostEpoch++;advance(100);tap();clean();CHECK(timers()==baseTimers);
  advance(500);CHECK(!BarbarClockPage::active());
 }
 open();if(BarbarClockPage::showSeconds()){doubleTap();advance(700);}verifySeconds(false);
 save("barbar-hidden-final");writeReport("main");clean();CHECK(timers()==baseTimers);
}
