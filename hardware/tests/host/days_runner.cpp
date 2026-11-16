// Render and exercise the production countdown-days widgets with LVGL input.
#include "Arduino.h"
#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/days_page.h"
#include "hardware/arduino/yiyi_desk/src/pages/days_store.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int64_t HostEpoch=DeskLogic::beijingEpoch(2026,11,14,12,0);
static lv_color_t pixels[320*240],buffer[320*40];
static lv_point_t finger{};
static bool down=false;
static lv_indev_t *pointer=nullptr;
static std::string output;
static unsigned screenshots=0,checks=0;
static void check(bool value){++checks;assert(value);}
static void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *data){
 for(int y=area->y1;y<=area->y2;++y)for(int x=area->x1;x<=area->x2;++x){if(x>=0&&x<320&&y>=0&&y<240)pixels[y*320+x]=*data;++data;}lv_disp_flush_ready(drv);
}
static void input(lv_indev_drv_t*,lv_indev_data_t *data){data->point=finger;data->state=down?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;}
static void advance(){HostMillis+=50;lv_tick_inc(50);lv_timer_handler();}
static void tap(int x,int y){finger={(lv_coord_t)x,(lv_coord_t)y};down=true;lv_timer_ready(pointer->driver->read_timer);advance();down=false;lv_timer_ready(pointer->driver->read_timer);advance();DaysPage::refresh(true);lv_obj_update_layout(root);}
static lv_obj_t *find(lv_obj_t *obj,const char *value){
 if(obj==root)lv_obj_update_layout(root);
 if(lv_obj_check_type(obj,&lv_label_class)&&!strcmp(lv_label_get_text(obj),value))return obj;
 for(unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)if(auto *result=find(lv_obj_get_child(obj,i),value))return result;return nullptr;
}
static void press(const char *value){
 lv_obj_update_layout(root);auto *obj=find(root,value);if(!obj)std::cerr<<"missing "<<value<<"\n";check(obj);
 while(obj&&!lv_obj_has_flag(obj,LV_OBJ_FLAG_CLICKABLE))obj=lv_obj_get_parent(obj);check(obj);
 lv_area_t area;lv_obj_get_coords(obj,&area);tap((area.x1+area.x2)/2,(area.y1+area.y2)/2);
}
static void openCard(const char *value){
 lv_obj_update_layout(root);
 auto enabled=[](const char *caption){auto *obj=find(root,caption);if(!obj)return false;while(obj&&!lv_obj_has_flag(obj,LV_OBJ_FLAG_CLICKABLE))obj=lv_obj_get_parent(obj);return obj&&!lv_obj_has_state(obj,LV_STATE_DISABLED);};
 for(unsigned i=0;i<7&&enabled("上一页");++i)press("上一页");
 for(unsigned i=0;!find(root,value)&&i<7;++i){if(enabled("下一页"))press("下一页");else break;}
 press(value);
}
static void verify(lv_obj_t *obj){
 if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return;
 if(lv_obj_check_type(obj,&lv_label_class)){
  const auto *font=lv_obj_get_style_text_font(obj,0);const char *s=lv_label_get_text(obj);
  for(uint32_t i=0;s[i];){uint32_t cp=_lv_txt_encoded_next(s,&i);if(cp=='\n'||cp=='\r')continue;lv_font_glyph_dsc_t dsc;check(lv_font_get_glyph_dsc(font,&dsc,cp,0));}
  lv_point_t size;lv_txt_get_size(&size,s,font,lv_obj_get_style_text_letter_space(obj,0),lv_obj_get_style_text_line_space(obj,0),lv_obj_get_width(obj),LV_TEXT_FLAG_NONE);
  if(size.y>lv_obj_get_height(obj)&&lv_label_get_long_mode(obj)!=LV_LABEL_LONG_DOT){std::cerr<<"label height "<<s<<" "<<size.y<<"/"<<lv_obj_get_height(obj)<<"\n";check(false);}
 }
 for(unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)verify(lv_obj_get_child(obj,i));
}
static void save(const char *name){lv_obj_update_layout(root);verify(root);lv_refr_now(nullptr);std::ofstream file(output+"/"+name+".rgb565",std::ios::binary);file.write((char*)pixels,sizeof(pixels));++screenshots;std::cout<<"frame "<<name<<"\n";}
static void show(App target){
 GiftUI::leaveView();Ui::cancelAnimations();Ui::clearStatusCluster();lv_obj_clean(root);SavedDesign::invalidate();
 lv_indev_reset(pointer,nullptr);app=target;page=6;renderPending=false;check(GiftUI::render(root));GiftUI::refresh(true);lv_obj_update_layout(root);
}
static void patch(const char *json){JsonDocument value;check(!deserializeJson(value,json));String error;if(!GiftData::patch(value.as<JsonVariantConst>(),error))std::cerr<<error.c_str()<<"\n";else return;check(false);}
static void upsert(int id,const char *name,const char *date,bool yearly,int color,const char *mode=nullptr,const char *category=nullptr,const char *style=nullptr,const char *textColor=nullptr){
 JsonDocument request;request["action"]="upsert";auto item=request["item"].to<JsonObject>();item["id"]=id;item["name"]=name;item["date"]=date;item["yearly"]=yearly;item["color"]=color;if(mode)item["mode"]=mode;if(category)item["category"]=category;if(style)item["style"]=style;if(textColor)item["textColor"]=textColor;String error;check(DaysStore::mutate(request.as<JsonVariantConst>(),error));
}
int main(int argc,char **argv){
 std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);
 if(argc<2)return 2;output=argv[1];std::filesystem::create_directories(output);LittleFS.directory=output+"/store";
 std::filesystem::remove_all(LittleFS.directory);check(GiftData::begin());lv_init();
 static lv_disp_draw_buf_t draw;lv_disp_draw_buf_init(&draw,buffer,nullptr,320*40);
 static lv_disp_drv_t display;lv_disp_drv_init(&display);display.hor_res=320;display.ver_res=240;display.draw_buf=&draw;display.flush_cb=flush;lv_disp_drv_register(&display);
 static lv_indev_drv_t driver;lv_indev_drv_init(&driver);driver.type=LV_INDEV_TYPE_POINTER;driver.read_cb=input;pointer=lv_indev_drv_register(&driver);
 root=lv_scr_act();lv_obj_remove_style_all(root);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);Ui::setRoot(root);
 Ui::setActionHandler([](lv_event_t *e){if(uintptr_t(lv_event_get_user_data(e))==Ui::Back&&!GiftUI::back()){app=App::None;page=6;renderPending=true;}});
 GiftUI::setAppsPage(1);show(App::None);save("days-00-launcher");press("倒数日");check(app==App::Days);show(App::Days);save("days-01-empty");
 upsert(0,"依依的生日","2026-12-20",true,1);upsert(0,"一起跨年","2027-01-01",false,2);upsert(0,"相遇的日子","2026-01-01",false,0);upsert(0,"今天也要开心","2026-11-14",false,3);
 DaysPage::refresh(true);save("days-02-list");
 press("依依的生日");save("days-03-birthday");check(DaysPage::canBack());check(DaysPage::back());DaysPage::refresh(true);
 press("一起跨年");save("days-04-future");check(DaysPage::back());DaysPage::refresh(true);
 openCard("相遇的日子");save("days-05-elapsed");check(DaysPage::back());DaysPage::refresh(true);press("上一页");
 // Refresh while a detail is open must follow phone edits and deletion safely.
 press("依依的生日");upsert(1,"生日快乐","2026-11-14",true,3);DaysPage::refresh(true);save("days-06-today");
 JsonDocument remove;remove["action"]="remove";remove["id"]=1;String error;check(DaysStore::mutate(remove.as<JsonVariantConst>(),error));DaysPage::refresh(true);save("days-07-deleted-open-detail");
 HostEpoch=0;DaysPage::refresh(true);save("days-08-no-time");HostEpoch=DeskLogic::beijingEpoch(2026,11,14,23,59)+59;DaysPage::refresh(true);
 upsert(0,"新的期待","2026-11-15",false,0);DaysPage::refresh(true);save("days-09-before-midnight");HostEpoch++;DaysPage::refresh(true);save("days-10-after-midnight");
 // New countup days coexist with old5-field countdown items, with startday0.
 upsert(0,"在一起的日子","2026-01-01",false,1,"countup");
 const int elapsedId=GiftData::get("countdownDays")["items"].as<JsonArrayConst>()[4]["id"].as<int>();
 upsert(0,"今天开始记录","2026-11-15",false,0,"countup");
 upsert(0,"新的旅程","2026-11-20",false,2,"countup");
 DaysPage::refresh(true);save("countup-01-mixed-list");
 openCard("在一起的日子");check(find(root,"318"));save("countup-02-elapsed");check(DaysPage::back());DaysPage::refresh(true);
 openCard("今天开始记录");check(find(root,"0"));save("countup-03-start-day");
 HostEpoch+=86400;DaysPage::refresh(true);check(find(root,"1"));save("countup-04-next-day");HostEpoch-=86400;DaysPage::refresh(true);check(DaysPage::back());DaysPage::refresh(true);
 openCard("新的旅程");check(find(root,"5"));save("countup-05-future-start");check(DaysPage::back());DaysPage::refresh(true);
 openCard("在一起的日子");HostEpoch=0;DaysPage::refresh(true);check(find(root,"--"));save("countup-06-no-time");HostEpoch=DeskLogic::beijingEpoch(2026,11,15,12,0);DaysPage::refresh(true);
 // Phone mode changes must redraw an already open detail, including captions.
 upsert(elapsedId,"在一起的日子","2027-01-01",false,1,"countdown");DaysPage::refresh(true);check(find(root,"47"));save("countup-07-mode-edit-countdown");
 upsert(elapsedId,"在一起的日子","2026-01-01",false,1,"countup");DaysPage::refresh(true);check(find(root,"318"));save("countup-08-mode-edit-countup");check(DaysPage::back());DaysPage::refresh(true);
 // Category controls the dedicated visual, independently of the custom name.
 upsert(0,"依依的生日","2026-12-20",true,2,"countdown","birthday");
 auto items=GiftData::get("countdownDays")["items"].as<JsonArrayConst>();const int birthdayId=items[items.size()-1]["id"].as<int>();
 upsert(0,"我们相恋了","2026-01-01",false,0,"countup","love");
 items=GiftData::get("countdownDays")["items"].as<JsonArrayConst>();const int loveId=items[items.size()-1]["id"].as<int>();
 DaysPage::refresh(true);save("special-00-classified-list");openCard("依依的生日");check(find(root,"35"));save("special-01-birthday");check(DaysPage::back());DaysPage::refresh(true);
 openCard("我们相恋了");check(find(root,"318"));save("special-02-love");
 auto updateLove=[&](const char *name,const char *date,const char *mode,const char *category){upsert(loveId,name,date,false,0,mode,category);DaysPage::refresh(true);lv_obj_update_layout(root);};
 auto updateBirthday=[&](const char *name,const char *date,bool yearly,const char *mode){upsert(birthdayId,name,date,yearly,2,mode,"birthday");DaysPage::refresh(true);lv_obj_update_layout(root);};
 updateLove("属于我们的第一个夏天","2026-01-01","countup","love");check(find(root,"318"));save("special-03-love-custom-name");
 updateLove("属于我们的第一个夏天","2026-01-01","countup","general");check(find(root,"318"));save("special-04-love-tag-removed");
 updateLove("一二三四五六七八九十一二三四五六七八九十一二三四","1900-01-01","countup","love");check(find(root,"46339"));save("special-05-love-long-title-max-days");
 updateLove("我们相恋了","2026-11-15","countup","love");check(find(root,"0"));save("special-06-love-start-day");
 HostEpoch+=86400;DaysPage::refresh(true);save("special-07-love-next-day");HostEpoch-=86400;
 updateLove("我们相恋了","2026-11-20","countup","love");check(find(root,"5"));save("special-08-love-future");
 HostEpoch=0;DaysPage::refresh(true);save("special-09-love-no-time");HostEpoch=DeskLogic::beijingEpoch(2026,11,15,12,0);
 updateLove("下一次相见","2027-01-01","countdown","love");check(find(root,"47"));save("special-10-love-countdown");check(DaysPage::back());DaysPage::refresh(true);
 openCard("依依的生日");updateBirthday("依依的生日","2026-11-15",true,"countdown");check(find(root,"0"));save("special-11-birthday-today");
 updateBirthday("一二三四五六七八九十一二三四五六七八九十一二三四","1900-01-01",false,"countup");check(find(root,"46339"));save("special-12-birthday-long-title-max-days");
 HostEpoch=0;DaysPage::refresh(true);save("special-13-birthday-no-time");HostEpoch=DeskLogic::beijingEpoch(2026,11,15,12,0);
 updateBirthday("从出生开始记录","2026-11-20",false,"countup");check(find(root,"5"));save("special-14-birthday-future-countup");
 updateBirthday("记得那天的生日","1900-01-01",false,"countdown");check(find(root,"46339"));save("special-15-birthday-past");
 // Exact firmware styles and palettes, including live changes of an open item.
 upsert(loveId,"我们的周年纪念","2027-02-01",false,0,"countdown","love","classic");DaysPage::refresh(true);check(DaysPage::back());DaysPage::refresh(true);openCard("我们的周年纪念");check(find(root,"78"));
 const char *styles[]={"classic","laurel","ink","heart","dots"};
 const char *palettes[]={"blue","mint","cream","dark"};
 for(int style=0;style<5;++style)for(int color=0;color<4;++color){
  if(style==4&&color!=0&&color!=3)continue;
  upsert(loveId,"我们的周年纪念","2027-02-01",false,color,"countdown","love",styles[style]);DaysPage::refresh(true);check(find(root,"78"));
  std::string shot="style-"+std::to_string(style+1)+"-"+styles[style]+"-"+std::to_string(color)+"-"+(style==3?(color==0?"rose":color==1?"orange":color==2?"violet":"peach"):palettes[color]);save(shot.c_str());
 }
 upsert(loveId,"一二三四五六七八九十一二三四五六七八九十一二三四","1900-01-01",false,0,"countup","love","laurel");DaysPage::refresh(true);check(find(root,"46339"));save("style-6-laurel-max-digits-long-title");
 upsert(loveId,"一二三四五六七八九十一二三四五六七八九十一二三四","1900-01-01",false,0,"countup","love","heart");DaysPage::refresh(true);check(find(root,"46339"));save("style-7-heart-max-digits-long-title");
 upsert(loveId,"我们的周年纪念","2027-02-01",false,0,"countdown","love","classic");DaysPage::refresh(true);HostEpoch=0;DaysPage::refresh(true);check(find(root,"--"));save("style-8-classic-no-time");HostEpoch=DeskLogic::beijingEpoch(2026,11,15,12,0);
 for(const char *design:styles){
  upsert(loveId,"我们的周年纪念","2027-02-01",false,0,"countdown","love",design,"#6B2760");DaysPage::refresh(true);auto *number=find(root,"78");check(number);check(lv_obj_get_style_text_color(number,0).full==lv_color_hex(0x6B2760).full);
  const std::string name=std::string("text-color-")+design;save(name.c_str());
 }
 upsert(loveId,"我们的周年纪念","2027-02-01",false,0,"countdown","love","heart");DaysPage::refresh(true);auto *autoNumber=find(root,"78");check(autoNumber);check(lv_obj_get_style_text_color(autoNumber,0).full==lv_color_hex(0xFFFFFF).full);save("text-color-auto-restored");
 for(int id:{birthdayId,loveId}){JsonDocument request;request["action"]="remove";request["id"]=id;check(DaysStore::mutate(request.as<JsonVariantConst>(),error));}DaysPage::refresh(true);
 // Max-length dynamic titles and the full20-item limit exercise layout/paging.
 for(int i=(int)GiftData::get("countdownDays")["items"].size();i<20;++i)upsert(0,"一二三四五六七八九十一二三四五六七八九十一二三四","2099-12-31",false,i%4);
 DaysPage::refresh(true);save("days-11-full-list");
 for(unsigned i=0;i<7&&find(root,"上一页");++i)press("上一页");tap(100,183);check(find(root,"一二三四五六七八九十一二三四五六七八九十一二三四"));save("days-12-long-title");check(DaysPage::back());DaysPage::refresh(true);
 upsert(2,"最早的纪念","1900-01-01",false,2);show(App::Days);
 openCard("最早的纪念");save("days-13-largest-elapsed");check(DaysPage::back());DaysPage::refresh(true);
 upsert(elapsedId,"最久的计时日","1900-01-01",false,3,"countup");DaysPage::refresh(true);openCard("最久的计时日");check(find(root,"46339"));save("countup-09-largest-elapsed");check(DaysPage::back());DaysPage::refresh(true);
 for(int i=0;i<18;++i){show(App::None);show(App::Days);}save("days-14-reentered");
 press("手机设置");check(app==App::Phone&&renderPending);
 std::cout<<"PASS: "<<checks<<" UI/font assertions, "<<screenshots<<" production LVGL screenshots, 36 lifecycle switches\n";
}
