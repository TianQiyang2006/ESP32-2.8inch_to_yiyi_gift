// Real LVGL 8 widget/timer/draw pipeline. No hardware mocks of either clock.
#include "Arduino.h"
#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/pages/assets/clock_gifs/clock_gif_assets.h"
extern "C" {
#include "hardware/arduino/yiyi_desk/src/pages/assets/clock_codec/clock_gifdec.h"
}
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int64_t HostEpoch=DeskLogic::beijingEpoch(2026,10,28,9,59)+59;
static lv_color_t framebuffer[320*240],drawbuffer[320*40];
static std::string out;
static void flush(lv_disp_drv_t *drv,const lv_area_t *a,lv_color_t *pixels) {
 for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++) {
  if(x>=0&&x<320&&y>=0&&y<240)framebuffer[y*320+x]=*pixels;++pixels;
 }
 lv_disp_flush_ready(drv);
}
static void advance(unsigned ms) {
 while(ms){unsigned step=std::min(ms,20u);ms-=step;HostMillis+=step;lv_tick_inc(step);lv_timer_handler();}
 GiftUI::refresh(true);lv_refr_now(nullptr);
}
static void save(const std::string &name) {
 lv_refr_now(nullptr);std::ofstream file(out+"/"+name+".rgb565",std::ios::binary);
 file.write((const char*)framebuffer,sizeof(framebuffer));
}
static std::vector<lv_color_t> pixels(){lv_refr_now(nullptr);return {framebuffer,framebuffer+320*240};}
static bool equalPixels(const std::vector<lv_color_t> &a,const std::vector<lv_color_t> &b){
 if(a.size()!=b.size())return false;
 for(unsigned i=0;i<a.size();i++)if(a[i].full!=b[i].full)return false;
 return true;
}
static lv_obj_t *label(lv_obj_t *o,const char *value) {
 if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),value))return o;
 for(unsigned i=0;i<lv_obj_get_child_cnt(o);i++)if(auto *found=label(lv_obj_get_child(o,i),value))return found;
 return nullptr;
}
static unsigned timers(){unsigned n=0;for(auto *t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))n++;return n;}
static void clean() {
 Ui::clearStatusCluster();GiftUI::leaveView();lv_obj_clean(root);SavedDesign::invalidate();
 assert(!GifClockPage::active()&&!PixelClockPage::active()&&!FlipClockPage::active());
}
static void rebuild() {
 clean();Ui::setRoot(root);Ui::setPage(page);assert(GiftUI::render(root));
 GiftUI::refresh(true);lv_obj_update_layout(root);renderPending=false;lv_refr_now(nullptr);
}
static void caption(const char *value) {
 auto *l=label(root,value);assert(l);lv_event_send(lv_obj_get_parent(l),LV_EVENT_CLICKED,nullptr);
 if(renderPending)rebuild();
}
static void clock(const char *value) {
 GiftUI::reset();app=App::Clock;page=6;rebuild();caption(value);
}
static void same(const std::vector<lv_color_t> &expected) {
 lv_refr_now(nullptr);for(unsigned i=0;i<expected.size();i++)assert(expected[i].full==framebuffer[i].full);
}
static void verifyGifDecoding() {
 const uint8_t *sources[]={clock_gif_0,clock_gif_1,clock_gif_4};
 std::ofstream hashes(out+"/gif-decoder-crc32.txt");
 for(unsigned clip=0;clip<3;clip++){
  auto *gif=clock_gd_open_gif_data(sources[clip]);assert(gif);unsigned frame=0;
  gif->loop_count=1;int result;
  while((result=clock_gd_get_frame(gif))==1){
   clock_gd_render_frame(gif,gif->canvas);uint32_t crc=~0u;
   for(unsigned i=0;i<gif->width*gif->height*3u;i++){
    crc^=gif->canvas[i];for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));
   }
   hashes<<clip<<' '<<frame++<<' '<<gif->gce.delay*10<<' '<<(~crc)<<'\n';
  }
  assert(result==0&&frame>1);clock_gd_close_gif(gif);
 }
}
int main(int argc,char **argv) {
 out=argc>1?argv[1]:"build/clock-port/host";std::filesystem::create_directories(out+"/store");
 LittleFS.directory=out+"/store";
 lv_init();static lv_disp_draw_buf_t buf;lv_disp_draw_buf_init(&buf,drawbuffer,nullptr,320*40);
 static lv_disp_drv_t drv;lv_disp_drv_init(&drv);drv.hor_res=320;drv.ver_res=240;drv.draw_buf=&buf;drv.flush_cb=flush;lv_disp_drv_register(&drv);
 root=lv_scr_act();lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);Ui::setRoot(root);
 Ui::setActionHandler([](lv_event_t *e){if((uintptr_t)lv_event_get_user_data(e)==Ui::Back)GiftUI::back();});
 assert(GiftData::begin());settings.city="昆明";weather.valid=true;weather.temperature=18;weather.condition="多云";
 const unsigned baseTimers=timers();
 verifyGifDecoding();

 // Picker keeps the persisted matrix ID and handles a failed settings commit.
 GiftUI::reset();app=App::Clock;page=6;rebuild();save("clock-picker");
 LittleFS.writeLimit=0;caption("Pixel Clock");LittleFS.resetFaults();
 assert(!PixelClockPage::active()&&GiftUI::viewKey().indexOf("clock-face")<0);
 caption("Pixel Clock");assert(PixelClockPage::active());
 assert(!label(root,"Pixel Clock")&&!label(root,LV_SYMBOL_LEFT));
 assert(GiftData::get("clockMode")["preset"]=="matrix");
 save("pixel-clock");auto old=pixels();HostEpoch++;advance(1000);save("pixel-clock-next-second");
 // The four digits stay fixed at 10:00:00 -> 10:00:01; only colon/footer change.
 // First set an even/odd pair within one minute (the previous transition was 09:59).
 HostEpoch=DeskLogic::beijingEpoch(2026,10,28,10,0);advance(1000);old=pixels();
 HostEpoch++;advance(1000);bool colonChanged=false;
 for(int y=47;y<191;y++)for(int x=8;x<312;x++) {
  if(x>=152&&x<166)colonChanged|=old[y*320+x].full!=framebuffer[y*320+x].full;
  else assert(old[y*320+x].full==framebuffer[y*320+x].full);
 }
 assert(colonChanged);
 PixelClockPage::startSnake();assert(PixelClockPage::snakeActive());save("pixel-clock-snake");
 advance(16000);assert(!PixelClockPage::snakeActive());
 // Click storms create at most six short-lived tiles, including during a snake.
 auto *view=lv_obj_get_child(root,0);unsigned children=lv_obj_get_child_cnt(view);
 auto *tile=lv_obj_get_child(view,2);
 for(int i=0;i<30;i++)lv_event_send(tile,LV_EVENT_CLICKED,nullptr);
 assert(lv_obj_get_child_cnt(view)==children+6);advance(720);
 assert(lv_obj_get_child_cnt(view)==children);
 assert(GiftUI::back());rebuild();assert(!PixelClockPage::active());

 HostEpoch=DeskLogic::beijingEpoch(2026,10,28,9,59)+59;
 caption("白色翻页");assert(FlipClockPage::active()&&FlipClockPage::movingDigits()==0);
 assert(!label(root,LV_SYMBOL_LEFT));
 assert(!label(root,"HOUR")&&!label(root,"MIN")&&!label(root,"SEC"));
 save("flip-before");HostEpoch++;FlipClockPage::refresh();assert(FlipClockPage::movingDigits()==6);
 advance(100);save("flip-folding");auto folding=pixels();
 // Frequent refreshes of the same target must not cancel the in-flight fold.
 for(int i=0;i<20;i++)FlipClockPage::refresh();assert(FlipClockPage::movingDigits()==6);same(folding);
 advance(100);save("flip-hinge");advance(100);save("flip-unfolding");advance(140);
 assert(FlipClockPage::movingDigits()==0);save("flip-after");auto final=pixels();
 clock("白色翻页");same(final); // animated result equals a fresh render of 10:00:00

 // Updates during a fold, midnight and invalid time all settle to exact snapshots.
 HostEpoch++;FlipClockPage::refresh();advance(100);HostEpoch+=17;FlipClockPage::refresh();advance(440);
 final=pixels();clock("白色翻页");same(final);
 HostEpoch=DeskLogic::beijingEpoch(2026,10,28,23,59)+59;FlipClockPage::refresh();advance(440);
 HostEpoch++;FlipClockPage::refresh();advance(440);
 assert(label(root,"2026年 10月 29日 周四"));save("flip-midnight");
 HostEpoch=0;FlipClockPage::refresh();assert(FlipClockPage::movingDigits()==0);
 assert(label(root,"等待联网校时"));save("flip-no-time");
 HostEpoch=DeskLogic::beijingEpoch(2026,10,29,8,28);FlipClockPage::refresh();
 assert(FlipClockPage::movingDigits()==0);save("flip-time-restored");

 // Leave during motion; neither clock may retain a timer after deleting its view.
 for(int i=0;i<30;i++) {
  clock(i%2?"Pixel Clock":"白色翻页");HostEpoch++;advance(100);
  clean();assert(timers()==baseTimers&&FlipClockPage::movingDigits()==0);advance(500);
 }
 clock("Pixel Clock");HostEpoch=0;advance(1000);save("pixel-clock-no-time");
 clean();assert(timers()==baseTimers);HostEpoch=DeskLogic::beijingEpoch(2026,10,28,10,0);
 clock("时光动画");assert(GifClockPage::active()&&label(root,"10:00:00"));
 assert(!label(root,LV_SYMBOL_LEFT)&&!label(root,"时光动画"));
 assert(!GifClockPage::playing()&&GifClockPage::frameIndex()==0&&GifClockPage::selection()==0);
 save("gif-clock-0-still");auto still=pixels();advance(5000);same(still);
 auto tap=[](){lv_event_send(lv_obj_get_child(root,0),LV_EVENT_CLICKED,nullptr);};
 auto clockPixels=[](){
  auto all=pixels();return std::vector<lv_color_t>(all.begin(),all.begin()+87*320);
 };
 auto header=clockPixels();
 tap();advance(280);assert(!GifClockPage::playing());
 advance(60);assert(GifClockPage::playing());advance(600);
 assert(GifClockPage::frameIndex()>0);save("gif-clock-0-playing");
 assert(!equalPixels(pixels(),still));assert(equalPixels(clockPixels(),header));
 advance(10000);assert(!GifClockPage::playing()&&GifClockPage::frameIndex()==0);same(still);
 save("gif-clock-0-finished");
 // Each double tap selects the next clip without playing it, including wrap.
 for(unsigned clip=1;clip<=3;clip++){
  tap();advance(140);tap();advance(600);
  assert(GifClockPage::selection()==clip%3&&!GifClockPage::playing());
  assert(GifClockPage::frameIndex()==0);assert(equalPixels(clockPixels(),header));
  save("gif-clock-"+std::to_string(clip%3)+"-still");
  auto first=pixels();advance(1000);same(first);
  tap();advance(340);assert(GifClockPage::playing());advance(700);
  save("gif-clock-"+std::to_string(clip%3)+"-playing");
  assert(GifClockPage::frameIndex()>0&&!equalPixels(pixels(),first));assert(equalPixels(clockPixels(),header));
  advance(16000);assert(!GifClockPage::playing()&&GifClockPage::frameIndex()==0);same(first);
 }
 // Clock keeps running while the artwork rests, including invalid-time recovery.
 HostEpoch++;advance(1000);assert(label(root,"10:00:01"));
 HostEpoch=0;advance(1000);assert(label(root,"--:--:--")&&label(root,"等待联网校时"));
 HostEpoch=DeskLogic::beijingEpoch(2026,10,30,23,59)+59;advance(1000);
 HostEpoch++;advance(1000);assert(label(root,"00:00:00")&&label(root,"2026年 10月 31日 周六"));
 // A double tap during motion cancels that clip and previews the next one.
 tap();advance(360);assert(GifClockPage::playing());tap();advance(120);tap();advance(600);
 assert(GifClockPage::selection()==1&&!GifClockPage::playing());
 // Pending single taps and in-flight frames cannot survive a view transition.
 for(int i=0;i<30;i++){
  clock("时光动画");tap();advance(i%2?140:380);clean();
  assert(timers()==baseTimers&&!GifClockPage::playing());advance(800);
 }
 std::cout<<"PASS: title/back removal, Pixel and flip regressions, all three retained GIF still/play/finish states, single/double tap, fixed live clock, clip wrap, interrupted playback, invalid time/midnight and 30 GIF lifecycle cycles.\n";
}
