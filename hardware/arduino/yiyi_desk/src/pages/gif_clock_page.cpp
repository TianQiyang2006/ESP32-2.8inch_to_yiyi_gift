// Fixed-position live clock with touch-triggered, one-shot GIF playback.
#include "mini_apps.h"
#include "../app_state.h"
#include "../clock_source.h"
#include "../touch.h"
#include "../ui_widgets.h"
#include "../saved_design/saved_resources.h"
#include "../../desk_logic.h"
#include "assets/clock_gifs/clock_gif_assets.h"
extern "C" {
#include "assets/clock_codec/clock_gifdec.h"
}
#include <stdio.h>

namespace GifClockPage {
namespace {
constexpr unsigned DoubleTapMs=320;
constexpr int StageY=87, StageHeight=153;
// Keep the original first, second and fifth clips in that order.
const uint8_t *const sources[]={clock_gif_0,clock_gif_1,clock_gif_4};
constexpr unsigned Count=sizeof(sources)/sizeof(sources[0]);
lv_obj_t *view=nullptr,*art=nullptr,*timeLabel=nullptr,*dateLabel=nullptr,*errorLabel=nullptr;
lv_timer_t *timer=nullptr;
clock_gd_GIF *decoder=nullptr;
lv_img_dsc_t descriptor{};
unsigned selected=0,frame=0;
bool running=false,pendingTap=false;
uint32_t tappedAt=0,frameAt=0,frameDelay=70;
int64_t lastEpoch=-1;

void text(lv_obj_t *object,const char *value) {
 if(strcmp(lv_label_get_text(object),value))lv_label_set_text(object,value);
}
void releaseDecoder() {
 lv_img_cache_invalidate_src(&descriptor);
 if(decoder){clock_gd_close_gif(decoder);decoder=nullptr;}
 descriptor.data=nullptr;
}
bool nextFrame() {
 // Ignore the source's infinite-loop extension: each click plays one cycle.
 decoder->loop_count=1;
 if(clock_gd_get_frame(decoder)!=1)return false;
 clock_gd_render_frame(decoder,decoder->canvas);
 frameDelay=LV_MAX(20u,(uint32_t)decoder->gce.delay*10u);
 frameAt=lv_tick_get();
 lv_img_cache_invalidate_src(&descriptor);lv_obj_invalidate(art);
 return true;
}
bool preview() {
 running=false;frame=0;
 if(art)lv_obj_add_flag(art,LV_OBJ_FLAG_HIDDEN);
 releaseDecoder();
 decoder=clock_gd_open_gif_data(sources[selected]);
 if(decoder){
  descriptor={};descriptor.header.cf=LV_IMG_CF_TRUE_COLOR_ALPHA;
  descriptor.header.w=decoder->width;descriptor.header.h=decoder->height;
  descriptor.data=decoder->canvas;descriptor.data_size=decoder->width*decoder->height*LV_IMG_PX_SIZE_ALPHA_BYTE;
  lv_img_set_src(art,&descriptor);
  lv_obj_set_pos(art,(320-decoder->width)/2,StageY+(StageHeight-decoder->height)/2);
  if(nextFrame()){
   lv_obj_clear_flag(art,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(errorLabel,LV_OBJ_FLAG_HIDDEN);return true;
  }
  releaseDecoder();
 }
 lv_obj_clear_flag(errorLabel,LV_OBJ_FLAG_HIDDEN);return false;
}
void clicked(lv_event_t *) {
 if(Touch::swiped())return;
 lastInputAt=millis();uint32_t now=lv_tick_get();
 if(pendingTap&&now-tappedAt<DoubleTapMs){
  pendingTap=false;selected=(selected+1)%Count;preview();
 }else{
  pendingTap=true;tappedAt=now;
 }
}
void deleted(lv_event_t *) {
 cancel();releaseDecoder();view=art=timeLabel=dateLabel=errorLabel=nullptr;lastEpoch=-1;
}
void tick(lv_timer_t *) {
 uint32_t now=lv_tick_get();
 if(pendingTap&&now-tappedAt>=DoubleTapMs){
  pendingTap=false;running=preview();
 }
 if(running&&now-frameAt>=frameDelay){
  if(nextFrame())++frame;
  else preview(); // Restore the still first frame, ready for another click.
 }
 refresh();
}
}
bool active(){return view!=nullptr;}
bool playing(){return running;}
unsigned selection(){return selected;}
unsigned frameIndex(){return frame;}
void cancel(){
 if(timer){lv_timer_del(timer);timer=nullptr;}
 pendingTap=false;running=false;
}
void build(lv_obj_t *parent) {
 view=lv_obj_create(parent);lv_obj_remove_style_all(view);
 lv_obj_set_pos(view,0,0);lv_obj_set_size(view,320,240);
 lv_obj_set_style_bg_color(view,lv_color_black(),0);lv_obj_set_style_bg_opa(view,LV_OPA_COVER,0);
 lv_obj_clear_flag(view,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(view,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(view,clicked,LV_EVENT_CLICKED,nullptr);
 lv_obj_add_event_cb(view,deleted,LV_EVENT_DELETE,nullptr);
 art=lv_img_create(view);lv_obj_clear_flag(art,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 timeLabel=Ui::label(view,"--:--:--",4,0,312,saved_gift_font_bold_56_0,0xffffff);
 dateLabel=Ui::label(view,"",4,65,312,saved_gift_font_bold_14_0,0xbac0c8);
 errorLabel=Ui::label(view,"GIF unavailable",4,153,312,&lv_font_montserrat_12,0x959da9);
 for(auto *label:{timeLabel,dateLabel,errorLabel}){
  lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_clear_flag(label,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 }
 lastEpoch=-1;pendingTap=false;preview();refresh();timer=lv_timer_create(tick,20,nullptr);
}
void refresh() {
 if(!view)return;
 int64_t epoch=ClockSource::now();if(epoch==lastEpoch)return;lastEpoch=epoch;
 bool valid=DeskLogic::timeValid(epoch);auto d=DeskLogic::beijingDate(epoch);char value[64];
 if(valid)snprintf(value,sizeof(value),"%02d:%02d:%02d",d.hour,d.minute,d.second);
 else snprintf(value,sizeof(value),"--:--:--");
 text(timeLabel,value);
 if(valid){
  const char *days[]={"日","一","二","三","四","五","六"};
  int day=(int)((DeskLogic::civilDays(d.year,d.month,d.day)+4)%7+7)%7;
  snprintf(value,sizeof(value),"%04d年 %02d月 %02d日 周%s",d.year,d.month,d.day,days[day]);
 }else snprintf(value,sizeof(value),"等待联网校时");
 text(dateLabel,value);
}
}
