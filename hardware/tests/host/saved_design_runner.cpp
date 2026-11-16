#include "Arduino.h"
#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/gift_assets/gift_fonts.h"
#include "hardware/arduino/yiyi_desk/src/power_status.h"
#include "hardware/arduino/yiyi_desk/src/xiaozhi_voice.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_resources.h"
#include "hardware/arduino/yiyi_desk/src/gift_assets/gift_glyph_coverage.h"
#include "hardware/arduino/yiyi_desk/src/gift_assets/gift_assets.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <vector>
#include <cstdlib>
int64_t HostEpoch=DeskLogic::beijingEpoch(2026,10,11,8,28);
extern int HostKnocks,HostBrightness;
extern int HostNoticeCount;
extern String HostNotice;
extern bool HostNoticeError;
namespace XiaozhiVoice {extern Snapshot current;extern int launches;}
lv_color_t framebuffer[320*240],drawbuffer[320*40];
unsigned flushCount=0;
static void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *pixels){++flushCount;for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++){if(x>=0&&x<320&&y>=0&&y<240)framebuffer[y*320+x]=*pixels;pixels++;}lv_disp_flush_ready(drv);}
void tick(){HostMillis+=200;lv_tick_inc(200);GiftUI::refresh();lv_timer_handler();}
void save(const std::string &name){lv_refr_now(nullptr);std::ofstream file("build/host-lvgl/"+name+".rgb565",std::ios::binary);file.write((char*)framebuffer,sizeof(framebuffer));}
void rebuild(){SavedDesign::invalidate();lv_obj_clean(root);Ui::setPage(page);bool owned=GiftUI::render(root);assert(owned||app==App::Phone||app==App::Calendar||app==App::Countdown);renderPending=false;tick();}
lv_obj_t *named(const char *name){return SavedDesign::find(root,name);}
lv_obj_t *label(lv_obj_t *parent,const std::string &value){if(lv_obj_check_type(parent,&lv_label_class)&&std::string(lv_label_get_text(parent))==value)return parent;for(uint32_t i=0;i<lv_obj_get_child_cnt(parent);i++)if(auto *found=label(lv_obj_get_child(parent,i),value))return found;return nullptr;}
void click(const char *name){auto *obj=named(name);assert(obj);lv_event_send(obj,LV_EVENT_CLICKED,nullptr);if(renderPending)rebuild();tick();}
void caption(const char *text){auto *obj=label(root,text);assert(obj);lv_event_send(lv_obj_get_parent(obj),LV_EVENT_CLICKED,nullptr);if(renderPending)rebuild();tick();}
void expect(const char *name,const char *text){auto *obj=named(name);if(!obj){std::cerr<<"EXPECT missing "<<name<<"\n";assert(obj);}std::string got(lv_label_get_text(obj));if(got!=text){std::cerr<<"EXPECT "<<name<<" got ["<<got<<"] want ["<<text<<"]\n";}assert(got==text);}
void patch(const char *text){JsonDocument json;assert(!deserializeJson(json,text));String error;assert(GiftData::patch(json.as<JsonVariantConst>(),error));tick();HostMillis+=1000;tick();}
int main(int argc,char **argv){
 const bool clocksOnly=argc>1&&std::string(argv[1])=="--clock-only";
 std::filesystem::create_directories("build/host-lvgl");LittleFS.directory="build/host-lvgl/store";
 lv_init();static lv_disp_draw_buf_t buf;lv_disp_draw_buf_init(&buf,drawbuffer,nullptr,320*40);static lv_disp_drv_t drv;lv_disp_drv_init(&drv);drv.hor_res=320;drv.ver_res=240;drv.draw_buf=&buf;drv.flush_cb=flush;lv_disp_drv_register(&drv);
 root=lv_scr_act();lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);Ui::setRoot(root);unsigned lastAction=0;static unsigned *actionOutput=&lastAction;Ui::setActionHandler([](lv_event_t *e){*actionOutput=(uintptr_t)lv_event_get_user_data(e);});assert(GiftData::begin());PowerStatus::begin();assert(DeskLogic::selfTest());
 // F-d replaced the runtime glyph scaler with real open-source fonts, so the old
 // "scaled fallback produces a >512B glyph" probe no longer exists. Check the two
 // replacements instead: a real glyph from the full GB2312 body font, and the
 // coverage table used by the missing-glyph filter.
 assert(gift_coverage_has(0x4F60));assert(gift_coverage_has(0x4E00));assert(!gift_coverage_has(0x1F600));
 lv_font_glyph_dsc_t largeGlyph;assert(lv_font_get_glyph_dsc(&fd_sans_16,&largeGlyph,0x4F60,0));assert(largeGlyph.box_h>=12);assert(lv_font_get_glyph_bitmap(&fd_sans_16,0x4F60));
 if(!clocksOnly){
 // The unmodified saved page content is rendered before real data is bound.
 for(unsigned i=0;i<7;i++){lv_obj_clean(root);SavedDesign::create(root,i);lv_obj_update_layout(root);save("saved-page-"+std::to_string(i));assert(named("nav_0"));}
 settings.city="昆明";weather.valid=true;weather.temperature=22;weather.condition="多云";weather.code=2;weather.feels=21;weather.low=17;weather.high=24;weather.humidity=65;weather.wind=8;weather.pressure=1008;weather.visibility=20;weather.uv=4;weather.aqi=35;weather.aqiCategory="优";weather.sunrise="06:48";weather.sunset="18:55";weather.fetchedAt=HostEpoch;
 for(int i=0;i<7;i++){weather.forecast[i].date="2026-10-"+String(11+i);weather.forecast[i].high=23+i;weather.forecast[i].low=16+i;weather.forecast[i].code=i%3==0?0:i%3==1?2:61;}
 for(page=0;page<7;page++){app=App::None;GiftUI::reset();rebuild();save("page-"+std::to_string(page));tick();unsigned before=flushCount;tick();assert(before==flushCount);}
 page=0;rebuild();expect("home_city","昆明");expect("clock_hour","08");expect("calendar_title","10月11日");assert(!lv_obj_has_flag(named("home_city"),LV_OBJ_FLAG_HIDDEN));assert(lv_obj_has_flag(named("home_city_template"),LV_OBJ_FLAG_HIDDEN));
 assert(named("home_weather_header")&&named("home_temperature_row")&&named("home_temperature_unit")&&named("home_temperature_unit_text"));
 auto *temperature=named("home_temperature"),*unit=named("home_temperature_unit"),*city=named("home_city"),*condition=named("home_condition");
 assert(lv_label_get_long_mode(city)==LV_LABEL_LONG_DOT&&lv_label_get_long_mode(condition)==LV_LABEL_LONG_DOT);
 for(const char *edge:{"-999","-99","-40","0","99","100","1000"}){
  weather.temperature=atof(edge);GiftUI::refresh(true);lv_obj_update_layout(root);assert(std::string(lv_label_get_text(temperature))==edge);
  const auto *numberFont=lv_obj_get_style_text_font(temperature,0),*unitFont=lv_obj_get_style_text_font(named("home_temperature_unit_text"),0);
  lv_point_t measured;lv_txt_get_size(&measured,edge,numberFont,lv_obj_get_style_text_letter_space(temperature,0),0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  assert(measured.x<=lv_obj_get_width(temperature));assert(lv_obj_get_style_x(unit,0)==measured.x+1);
  assert(lv_obj_get_y(unit)==numberFont->line_height-numberFont->base_line-unitFont->line_height+unitFont->base_line);
 }
 settings.city=String("中华人民共和国北京市朝阳区望京街道天气测试区域");weather.condition=String("局部多云转阵雨，夜间有持续强风");
 GiftUI::refresh(true);save("page-0-weather-long");
 // LV_LABEL_LONG_DOT truncation is font-metric dependent, so assert the visible
 // contract (prefix kept, text shortened, fixed width) instead of one exact slice.
 const std::string longCity("中华人民共和国北京市朝阳区望京街道天气测试区域");
 const std::string cityText(lv_label_get_text(city));
 assert(cityText.rfind("中华人民共和国",0)==0&&cityText.size()<longCity.size());
 const std::string conditionText(lv_label_get_text(condition));
 assert(conditionText.rfind("局部多云",0)==0&&conditionText.size()<std::string("局部多云转阵雨，夜间有持续强风").size());
 assert(lv_obj_get_width(city)==72&&lv_obj_get_width(condition)==72);
 weather.valid=false;GiftUI::refresh(true);expect("home_temperature","--");expect("home_condition","暂无");
 weather.valid=true;weather.temperature=22;weather.condition="多云";settings.city="昆明";GiftUI::refresh(true);
 // Runtime navigation uses touch swipes; the designer's dots are hidden.
 for(int i=0;i<7;i++){assert(lv_obj_has_flag(named(("nav_"+std::to_string(i)).c_str()),LV_OBJ_FLAG_HIDDEN));page=DeskLogic::nextPage(page,40);GiftUI::reset();rebuild();assert(page==(i+1)%7&&app==App::None);}
 page=1;rebuild();click("weather_refresh");assert(lastAction==Ui::RefreshWeather);assert(lv_img_get_src(named("icon_78"))==saved_gift_forecast_sun);click("element_2");assert(app==App::Phone);app=App::None;page=3;GiftUI::reset();rebuild();click("element_5");assert(lastAction==Ui::RefreshQuote);click("element_15");assert(lastAction==Ui::RefreshPoem);click("element_6");assert(GiftUI::viewKey().indexOf("reading-quote")>=0);assert(GiftUI::back());rebuild();
 page=4;rebuild();click("water_200");expect("water_value","200");click("water_500");expect("water_value","700");click("wood_hit");assert(HostKnocks==1);expect("wood_count","1");click("mood_select");caption("开心");assert(GiftData::record(GiftData::today())["mood"]=="happy");assert(!lv_obj_has_flag(named("today_mood"),LV_OBJ_FLAG_HIDDEN));click("element_7");assert(label(root,"选择心情 · 2026-10-05"));caption("平静");assert(GiftData::record("2026-10-05")["mood"]=="calm");
 // Page 5 is the current timetable, replacing the older letter template.
 page=5;rebuild();assert(label(root,"一")&&label(root,"五")&&label(root,"‹")&&label(root,"›"));
 patch(R"({"letter":{"title":"手机编辑标题","text":"你好，依依！这是从手机更新的中文留言。","signature":"来自手机"}})");assert(GiftData::get("letter")["title"]=="手机编辑标题");
 app=App::None;page=6;GiftUI::reset();rebuild();click("app_settings");assert(app==App::Settings);
 // The two serial-only pages now have Settings entries; verify both the labels and
 // the action wiring (About / Colors&Touch still render their pages in firmware).
 assert(label(root,"关于")&&label(root,"颜色与触摸")&&label(root,"固件 · 版本信息")&&label(root,"红蓝 · 反色 · 触摸方向"));
 caption("关于");assert(lastAction==Ui::OpenAbout);
 caption("颜色与触摸");assert(lastAction==Ui::OpenDiagnostics);
 save("settings");
 }
 app=App::None;page=4;GiftUI::reset();rebuild();click("element_43");assert(GiftUI::viewKey().indexOf("wood")>=0);
 LittleFS.writeLimit=0;int woodNotices=HostNoticeCount;caption("声音：开");LittleFS.resetFaults();
 assert(HostNoticeCount==woodNotices+1&&HostNoticeError);
 assert(GiftData::get("daily")["sound"]==true&&GiftUI::viewKey().indexOf("wood")>=0);
 caption("声音：开");assert(GiftData::get("daily")["sound"]==false);
 app=App::None;page=6;GiftUI::reset();rebuild();click("app_clock");assert(app==App::Clock);
 LittleFS.writeLimit=0;int notices=HostNoticeCount;caption("Pixel Clock");LittleFS.resetFaults();
 assert(HostNoticeCount==notices+1&&HostNoticeError&&!HostNotice.isEmpty());
 assert(GiftUI::viewKey().indexOf("clock-face")<0&&GiftData::get("clockMode")["preset"]=="landscape");
 caption("Pixel Clock");assert(GiftUI::viewKey().indexOf("clock-face")>=0);
 HostEpoch=DeskLogic::beijingEpoch(2026,10,11,20,23)+42;HostMillis+=1000;tick();save("clock-matrix");
 auto before=std::vector<lv_color_t>(framebuffer,framebuffer+320*240);
 HostEpoch++;HostMillis+=1000;tick();save("clock-matrix-next-second");
 bool secondsChanged=false;for(int y=47;y<191;y++)for(int x=8;x<312;x++){
  if(x<152||x>=166)assert(before[y*320+x].full==framebuffer[y*320+x].full);
  else secondsChanged|=before[y*320+x].full!=framebuffer[y*320+x].full;
 }assert(secondsChanged);
 assert(GiftUI::back());rebuild();caption("时光山水");assert(label(root,"20:23:43"));
 const int periodHours[]={0,6,11,16,18,21};
 const lv_img_dsc_t *periodImages[]={&gift_clock_predawn,&gift_clock_morning,&gift_clock_day,&gift_clock_sunset,&gift_clock_dusk,&gift_clock_night};
 const char *periodNames[]={"predawn","morning","day","sunset","dusk","night"};
 for(int i=0;i<6;i++){
  HostEpoch=DeskLogic::beijingEpoch(2026,10,22,periodHours[i],28)+58;HostMillis+=1000;tick();
  lv_obj_t *background=nullptr;
  for(uint32_t child=0;child<lv_obj_get_child_cnt(root);child++){
   auto *object=lv_obj_get_child(root,child);
   if(lv_obj_check_type(object,&lv_img_class))background=object;
  }
  assert(background&&lv_img_get_src(background)==periodImages[i]);
  assert(label(root,"2026年 10月 22日 周四"));save(std::string("clock-landscape-")+periodNames[i]);
  if(i==2){
   auto prior=std::vector<lv_color_t>(framebuffer,framebuffer+320*240);
   HostEpoch++;HostMillis+=1000;tick();save("clock-landscape-next-second");
   assert(label(root,"11:28:59"));bool changed=false;
   for(int y=16;y<78;y++)for(int x=0;x<320;x++){
    if(x<210)assert(prior[y*320+x].full==framebuffer[y*320+x].full);
    else changed|=prior[y*320+x].full!=framebuffer[y*320+x].full;
   }assert(changed);
  }
  // Background changes at the precise next period boundary, without reopening.
  if(i<5){
   HostEpoch=DeskLogic::beijingEpoch(2026,10,22,periodHours[i+1],0)-1;HostMillis+=1000;tick();
   assert(lv_img_get_src(background)==periodImages[i]);
   HostEpoch++;HostMillis+=1000;tick();assert(lv_img_get_src(background)==periodImages[i+1]);
  }
 }
 // The widest repeated digits must fit; equal advances prevent second-to-second drift.
 lv_font_glyph_dsc_t zero;assert(lv_font_get_glyph_dsc(&gift_font_clock,&zero,'0',0));
 for(char digit='0';digit<='9';digit++){
  lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(&gift_font_clock,&glyph,digit,0));assert(glyph.adv_w==zero.adv_w);
  std::string value(8,digit);value[2]=value[5]=':';lv_point_t size;
  lv_txt_get_size(&size,value.c_str(),&gift_font_clock,0,0,308,LV_TEXT_FLAG_NONE);assert(size.x<=308&&size.y==gift_font_clock.line_height);
 }
 HostEpoch=DeskLogic::beijingEpoch(2026,10,22,23,59)+59;HostMillis+=1000;tick();
 assert(label(root,"23:59:59"));HostEpoch++;HostMillis+=1000;tick();
 assert(label(root,"00:00:00")&&label(root,"2026年 10月 23日 周五"));save("clock-landscape-midnight");
 assert(GiftUI::back());rebuild();caption("白色翻页");save("clock-flip");
 if(clocksOnly){std::cout<<"PASS: all clock modes, seconds, six landscape periods and exact boundaries, tabular font fit, midnight date rollover.\n";return 0;}
 app=App::None;page=6;GiftUI::reset();rebuild();int launches=XiaozhiVoice::launches;click("app_voice");assert(XiaozhiVoice::launches==launches+1&&app==App::Voice);assert(GiftUI::back());rebuild();assert(named("app_voice"));
 std::cout<<"PASS: seven current pages, weather text/temperature boundary layouts, semantic home IDs, swipe page logic, forecast/refresh events, timetable, Chinese settings, mood/water/wood, large fallback glyphs, all clock modes/period boundaries/midnight and Xiaozhi entry. Source "<<SAVED_DESIGN_HASH<<"\n";
}
