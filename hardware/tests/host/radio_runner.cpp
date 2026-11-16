// Deterministic host-only radio states drive the real firmware RadioPage.
// This executable never links the production decoder or touches a device.
#include "Arduino.h"
#include "hardware/arduino/yiyi_desk/src/pages/radio_page.h"
#include "hardware/arduino/yiyi_desk/src/radio_player.h"
#include "hardware/arduino/yiyi_desk/src/radio_station_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

uint32_t HostMillis=0;int HostBatteryMv=1850;
namespace Fixture {
RadioPlayer::State state;
bool favorites[20]{};
bool boundary=false;
bool customCatalog=false;
uint32_t catalogRevision=0;
std::vector<RadioPlayer::Station> catalog;
unsigned backCalls=0,playCalls=0;
const RadioPlayer::Station boundaryStations[]={
 {"long","中央人民广播电台经济之声","新闻资讯","http://host.invalid/long"},
 {"piano","Piano & Friends","钢琴音乐","http://host.invalid/piano"}
};
}
namespace RadioPlayer {
void begin(){Fixture::state.ready=true;Fixture::state.wifi=true;Fixture::state.station=stationCount()?0:-1;}
void open(){begin();}
void stop(){Fixture::state.playing=Fixture::state.connecting=Fixture::state.buffering=false;}
void play(unsigned i){assert(i<stationCount());Fixture::state.station=i;Fixture::state.playing=true;Fixture::state.connecting=Fixture::state.buffering=false;Fixture::state.error="";Fixture::state.title="";++Fixture::playCalls;}
void toggle(){if(Fixture::state.playing||Fixture::state.connecting||Fixture::state.buffering)stop();else if(stationCount())play(Fixture::state.station<0?0:Fixture::state.station);}
void next(int direction){if(stationCount())play((Fixture::state.station+stationCount()+(direction<0?-1:1))%stationCount());}
void setVolume(unsigned value){Fixture::state.volume=std::min(100u,value);}
void toggleFavorite(){if(Fixture::state.station>=0&&Fixture::state.station<20)Fixture::favorites[Fixture::state.station]=!Fixture::favorites[Fixture::state.station];}
State state(){State result=Fixture::state;result.favorite=result.station>=0&&result.station<20&&Fixture::favorites[result.station];return result;}
unsigned stationCount(){return Fixture::customCatalog?Fixture::catalog.size():sizeof(RadioStations)/sizeof(RadioStations[0])+(Fixture::boundary?2:0);}
Station station(unsigned i){assert(i<stationCount());if(Fixture::customCatalog)return Fixture::catalog[i];if(i<6){auto &entry=RadioStations[i];return {entry.id,entry.name,entry.tag,entry.url};}return Fixture::boundaryStations[i-6];}
uint32_t stationRevision(){return Fixture::catalogRevision;}
bool isFavorite(unsigned i){return i<20&&Fixture::favorites[i];}
void diagnostics(JsonObject){}
void tick(){}
}
namespace Ui {void dispatchAction(lv_event_t*){++Fixture::backCalls;}}

namespace {
lv_color_t pixels[320*240],drawBuffer[320*40];
lv_indev_t *pointer=nullptr;lv_point_t point{};bool down=false;
std::string output;
unsigned checks=0,frames=0;
void check(bool condition,const char *description){++checks;if(!condition){std::cerr<<"FAIL: "<<description<<"\n";std::abort();}}
void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *data){
 for(int y=area->y1;y<=area->y2;++y)for(int x=area->x1;x<=area->x2;++x){if(x>=0&&x<320&&y>=0&&y<240)pixels[y*320+x]=*data;++data;}lv_disp_flush_ready(driver);
}
lv_obj_t *find(lv_obj_t *object,const char *caption){
 if(lv_obj_check_type(object,&lv_label_class)&&!strcmp(lv_label_get_text(object),caption))return object;
 for(unsigned i=0;i<lv_obj_get_child_cnt(object);++i)if(auto *found=find(lv_obj_get_child(object,i),caption))return found;return nullptr;
}
void build(){RadioPage::cancel();lv_obj_clean(root);RadioPage::build(root);lv_obj_update_layout(root);renderPending=false;}
void pump(unsigned duration=100){
 for(unsigned t=0;t<duration;t+=5){HostMillis+=5;lv_tick_inc(5);RadioPage::refresh(false);lv_timer_handler();if(renderPending&&!RadioPage::inputActive()&&!down)build();}
 lv_obj_update_layout(root);
}
void tap(int x,int y){point={(lv_coord_t)x,(lv_coord_t)y};down=true;pump(100);down=false;pump(180);}
void drag(int x,int from,int to){point={(lv_coord_t)x,(lv_coord_t)from};down=true;pump(80);for(int i=1;i<=12;++i){point.y=from+(to-from)*i/12;pump(25);}down=false;pump(220);}
void captionTap(const char *caption){auto *obj=find(root,caption);check(obj,"caption exists");lv_area_t coords;lv_obj_get_coords(obj,&coords);tap((coords.x1+coords.x2)/2,(coords.y1+coords.y2)/2);}
uint32_t codepoint(const unsigned char *&p){uint32_t c=*p++;if(c<128)return c;unsigned n=c<224?1:c<240?2:3;c&=n==1?31:n==2?15:7;while(n--&&*p)c=(c<<6)|(*p++&63);return c;}
void validate(lv_obj_t *obj,bool clipped=false){
 if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return;
 clipped=clipped||lv_obj_has_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
 if(lv_obj_check_type(obj,&lv_label_class)){
  const char *value=lv_label_get_text(obj);const auto *font=lv_obj_get_style_text_font(obj,0);
  for(const unsigned char *p=(const unsigned char*)value;*p;){uint32_t c=codepoint(p);if(c=='\n')continue;lv_font_glyph_dsc_t glyph{};bool available=lv_font_get_glyph_dsc(font,&glyph,c,0);if(!available||glyph.is_placeholder)std::cerr<<"Missing glyph "<<c<<" in "<<value<<"\n";check(available&&!glyph.is_placeholder,"all visible glyphs available");}
  auto mode=lv_label_get_long_mode(obj);lv_point_t size{};lv_txt_get_size(&size,value,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
  if(mode==LV_LABEL_LONG_DOT){if(size.x>lv_obj_get_width(obj))std::cerr<<"Clipped: "<<value<<" "<<size.x<<"/"<<lv_obj_get_width(obj)<<"\n";check(size.x<=lv_obj_get_width(obj),"fixed caption fits without ellipsis");}
  check(lv_obj_get_height(obj)>=font->line_height,"font line height fits label");
  if(!clipped){lv_area_t area;lv_obj_get_coords(obj,&area);check(area.x1>=0&&area.y1>=0&&area.x2<320&&area.y2<240,"caption stays on screen");}
 }
 for(unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)validate(lv_obj_get_child(obj,i),clipped);
}
void save(const char *name){lv_obj_update_layout(root);validate(root);lv_refr_now(nullptr);std::ofstream file(output+"/"+name+".rgb565",std::ios::binary);file.write((char*)pixels,sizeof(pixels));++frames;std::cout<<name<<"\n";}
void dynamicCatalog(){
 Fixture::boundary=false;Fixture::customCatalog=true;Fixture::catalog.clear();
 for(unsigned i=0;i<20;++i){char number[4];snprintf(number,sizeof(number),"%02u",i+1);Fixture::catalog.push_back({String("host-")+number,String("Station ")+number,"Music",String("https://host.invalid/live/")+number+".mp3"});}
 ++Fixture::catalogRevision;RadioPlayer::stop();Fixture::state.title="";RadioPage::cancel();lv_obj_clean(root);RadioPage::enter();build();pump();
 check(RadioPlayer::stationCount()==20,"dynamic catalog contains maximum twenty stations");check(find(root,"01 / 20"),"player shows dynamic catalog count");save("radio-19-twenty-player");
 tap(276,18);check(find(root,"20 个声音"),"picker shows twenty station count");check(find(root,"Station 20"),"last catalog row exists");save("radio-20-twenty-list");
 auto *last=find(root,"Station 20"),*scroll=lv_obj_get_parent(lv_obj_get_parent(last));
 check(lv_obj_has_flag(scroll,LV_OBJ_FLAG_SCROLLABLE),"dynamic rows belong to scroll viewport");
 bool visible=false;unsigned plays=Fixture::playCalls;
 for(unsigned i=0;i<20;++i){lv_area_t itemArea,viewport;lv_obj_get_coords(last,&itemArea);lv_obj_get_coords(scroll,&viewport);visible=itemArea.y1>=viewport.y1&&itemArea.y2<=viewport.y2;if(visible)break;drag(135,182,92);check(Fixture::playCalls==plays,"twenty-row pointer scrolling never selects content");}
 check(visible,"pointer scrolling reaches twentieth station");check(lv_obj_get_scroll_y(scroll)>0,"twenty-row list moved through LVGL scrolling");save("radio-21-twenty-list-bottom");
 captionTap("Station 20");check(Fixture::playCalls==plays+1&&RadioPlayer::state().station==19,"pointer selects twentieth catalog entry");check(!find(root,"全部电台")&&find(root,"Station 20"),"last selection returns to matching player");save("radio-22-twenty-last-selected");
 tap(276,18);tap(20,18);check(!find(root,"全部电台"),"twenty station picker header returns normally");
 Fixture::catalog[19].name="Night Radio";Fixture::catalog[19].tag="Jazz";++Fixture::catalogRevision;pump();
 check(RadioPlayer::stationCount()==20,"catalog rename keeps station count");check(find(root,"Night Radio")&&!find(root,"Station 20"),"same-count revision refreshes player name");check(find(root,"Jazz"),"same-count revision refreshes player tag");save("radio-23-renamed-player");
 tap(276,18);check(find(root,"Night Radio")&&!find(root,"Station 20"),"updated selected entry appears in picker");
 Fixture::catalog[0].name="Morning Radio";Fixture::catalog[0].tag="Talk";++Fixture::catalogRevision;pump();
 check(find(root,"Morning Radio")&&!find(root,"Station 01"),"same-count revision rebuilds an already open picker");check(find(root,"Talk"),"picker refreshes edited tag");save("radio-24-renamed-list");
 plays=Fixture::playCalls;captionTap("Morning Radio");check(Fixture::playCalls==plays+1&&RadioPlayer::state().station==0,"updated first row remains selectable");check(find(root,"Morning Radio"),"selection opens edited station player");
 RadioPlayer::stop();tap(276,18);auto *pressedCaption=find(root,"Morning Radio");check(pressedCaption,"station to hold exists in current picker");
 auto *pressedRow=lv_obj_get_parent(pressedCaption);lv_area_t pressedArea;lv_obj_get_coords(pressedRow,&pressedArea);
 unsigned staleRowClicks=0;lv_obj_add_event_cb(pressedRow,[](lv_event_t *event){++*static_cast<unsigned*>(lv_event_get_user_data(event));},LV_EVENT_CLICKED,&staleRowClicks);
 point={(lv_coord_t)((pressedArea.x1+pressedArea.x2)/2),(lv_coord_t)((pressedArea.y1+pressedArea.y2)/2)};down=true;pump(100);
 check(lv_obj_has_state(pressedRow,LV_STATE_PRESSED),"real pointer holds station row before catalog change");plays=Fixture::playCalls;
 std::swap(Fixture::catalog[0],Fixture::catalog[1]);Fixture::state.station=1;++Fixture::catalogRevision;
 check(RadioPlayer::station(0).id=="host-02"&&RadioPlayer::station(1).id=="host-01","phone reorder changes old row index while retaining stable selected station");
 // Consume the release through LVGL before the host's rebuild loop. This keeps
 // the original pressed row alive until its real CLICKED reaches onAction().
 down=false;HostMillis+=5;lv_tick_inc(5);lv_indev_read_timer_cb(pointer->driver->read_timer);
 check(staleRowClicks==1,"LVGL delivers the held row release as a real click");
 check(Fixture::playCalls==plays&&!RadioPlayer::state().playing,"stale catalog row click never starts a shifted station index");
 check(renderPending&&find(root,"全部电台"),"stale click requests picker rebuild without opening player");pump(180);
 check(find(root,"Station 02")&&find(root,"Morning Radio"),"picker rebuild shows the reordered station catalog");save("radio-27-reordered-after-held-touch");
 captionTap("Station 02");check(Fixture::playCalls==plays+1&&RadioPlayer::state().station==0&&RadioPlayer::station(0).id=="host-02","fresh pointer selection after catalog reorder plays the intended stable station");check(!find(root,"全部电台"),"fresh selection after stale touch returns to player");
 RadioPlayer::stop();Fixture::catalog.clear();Fixture::state.station=-1;Fixture::state.title="";++Fixture::catalogRevision;pump();
 check(find(root,"选择一个电台")&&find(root,"00 / 00"),"empty catalog player has safe selection and count");check(find(root,"连接手机，添加一个电台"),"empty player explains phone setup");save("radio-25-empty-player");
 plays=Fixture::playCalls;tap(160,214);check(Fixture::playCalls==plays&&!RadioPlayer::state().playing,"empty catalog does not attempt invalid playback");
 tap(276,18);check(find(root,"0 个声音"),"empty picker shows zero stations");check(find(root,"还没有电台")&&find(root,"连接手机后，添加喜欢的电台"),"empty picker explains how to add content");save("radio-26-empty-list");
 unsigned backs=Fixture::backCalls;tap(20,18);check(find(root,"在线 FM")&&!find(root,"全部电台"),"empty picker header returns to player");check(Fixture::backCalls==backs,"empty picker back stays inside app");
 Fixture::customCatalog=false;Fixture::state.station=0;++Fixture::catalogRevision;RadioPage::enter();build();pump();
 std::cout<<"PASS dynamic catalog: twenty-row pointer scrolling/selection/back, empty player/list/back, same-count revision name/tag refresh, stale held-row release rejects shifted index\n";
}
void init(){
 lv_init();static lv_disp_draw_buf_t db;lv_disp_draw_buf_init(&db,drawBuffer,nullptr,320*40);
 static lv_disp_drv_t driver;lv_disp_drv_init(&driver);driver.hor_res=320;driver.ver_res=240;driver.draw_buf=&db;driver.flush_cb=flush;lv_disp_drv_register(&driver);
 static lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;input.read_cb=[](lv_indev_drv_t*,lv_indev_data_t *data){data->point=point;data->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;};pointer=lv_indev_drv_register(&input);
 root=lv_scr_act();lv_obj_remove_style_all(root);lv_obj_set_size(root,320,240);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);RadioPage::enter();build();pump();
}
}
int main(int argc,char **argv){
 output=argc>1?argv[1]:"build/radio-host";std::filesystem::create_directories(output);init();
 check(find(root,"在线 FM"),"main title");check(!RadioPlayer::state().playing,"opening app does not auto-play");save("radio-01-ready");
 tap(160,214);check(RadioPlayer::state().playing,"pointer play starts stream");Fixture::state.title="听见好时光 · 音乐正在发生";RadioPage::refresh(true);save("radio-02-playing");
 tap(284,59);check(RadioPlayer::isFavorite(0),"main favorite action");save("radio-03-favorite");
 tap(224,214);check(RadioPlayer::state().station==1,"next station");tap(94,214);check(RadioPlayer::state().station==0,"previous station");
 tap(33,215);check(RadioPage::canBack(),"volume sheet can back");save("radio-04-volume");
 captionTap("轻声 20");check(RadioPlayer::state().volume==20,"quiet preset");tap(287,165);check(RadioPlayer::state().volume==25,"volume plus");tap(32,165);check(RadioPlayer::state().volume==20,"volume minus");
 captionTap("静音");check(RadioPlayer::state().volume==0,"mute preset");captionTap("适中 40");check(RadioPlayer::state().volume==40,"normal preset");
 for(int i=0;i<12;++i)tap(286,165);check(RadioPlayer::state().volume==100,"volume caps at 100");save("radio-05-volume-max");
 check(RadioPage::back(),"back closes volume");pump();check(!RadioPage::canBack(),"main has no local back");
 tap(276,18);check(find(root,"全部电台"),"station picker");save("radio-06-stations");
 unsigned plays=Fixture::playCalls;drag(135,175,108);check(Fixture::playCalls==plays,"scroll gesture does not select station");save("radio-07-stations-scrolled");
 captionTap("我的收藏");check(find(root,"北京音乐广播"),"favorite station visible");save("radio-08-favorites");
 tap(120,100);check(!find(root,"全部电台"),"select station returns to player");check(RadioPlayer::state().station==0,"correct favorite selected");
 tap(284,59);check(!RadioPlayer::isFavorite(0),"favorite removal");tap(286,214);check(find(root,"把喜欢的声音留在这里"),"empty favorite explanation");save("radio-09-empty-favorites");
 check(RadioPage::back(),"back exits picker");pump();
 Fixture::state.wifi=false;RadioPlayer::stop();RadioPage::refresh(true);check(find(root,"还没联网"),"offline status");save("radio-10-offline");
 plays=Fixture::playCalls;tap(160,180);check(Fixture::playCalls==plays,"offline hint does not start network job");
 Fixture::state.wifi=true;Fixture::state.connecting=true;RadioPage::refresh(true);save("radio-11-connecting");
 tap(160,214);check(!RadioPlayer::state().connecting,"stop cancels connection");
 Fixture::state.error="服务器暂时不可用";RadioPage::refresh(true);save("radio-12-error");tap(160,180);check(RadioPlayer::state().playing,"error hint retries station");
 Fixture::state.buffering=true;RadioPage::refresh(true);save("radio-13-buffering");
 Fixture::state.buffering=false;Fixture::state.saveFailed=true;RadioPage::refresh(true);save("radio-14-save-failed");Fixture::state.saveFailed=false;
 Fixture::boundary=true;RadioPlayer::play(6);Fixture::state.title="这是一条非常长的直播节目信息，用于验证滚动文字不会覆盖控制按钮";RadioPage::refresh(true);save("radio-15-long-title");pump(2400);save("radio-16-long-title-scroll");
 RadioPlayer::play(7);RadioPage::refresh(true);save("radio-17-latin-title");
 Fixture::boundary=false;dynamicCatalog();
 for(unsigned i=0;i<40;++i){RadioPage::cancel();lv_obj_clean(root);RadioPage::enter();build();pump(20);tap(275,18);check(RadioPage::canBack(),"picker lifecycle");check(RadioPage::back(),"picker closes through local back");pump(20);}
 tap(20,18);check(Fixture::backCalls==1,"main back forwards to desktop navigation");
 RadioPage::cancel();lv_obj_clean(root);RadioPage::refresh(true);check(!RadioPage::inputActive(),"cancel releases widget references");
 RadioPage::createIcon(root,132,92);save("radio-18-launcher-icon");
 std::ofstream report(output+"/runner-verification.json");report<<"{\"passed\":true,\"checks\":"<<checks<<",\"frames\":"<<frames<<",\"lifecycle_rounds\":40,\"pointer_input\":true,\"production_page\":true,\"dynamic_catalog\":true,\"maximum_stations\":20,\"empty_catalog\":true,\"same_count_revision_refresh\":true,\"stale_pressed_row_revision_guard\":true,\"backend\":\"host fixtures only\",\"device_opened\":false}";
 std::cout<<"PASS "<<checks<<" checks, "<<frames<<" frames\n";
}
