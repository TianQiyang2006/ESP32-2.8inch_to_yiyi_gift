// Split-flap clock for LVGL 8. The moving half is resampled vertically around
// the centre hinge: shrinking a clipping box alone leaves the glyph motionless.
#include "mini_apps.h"
#include "mini_apps_memory.h"
#include "../app_state.h"
#include "../clock_source.h"
#include "../gift_data.h"
#include "../touch.h"
#include "../saved_design/saved_resources.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include "../text_util.h"
#include "../../desk_logic.h"
#include <stdio.h>

namespace FlipClockPage {
namespace {
constexpr int W=46, H=116, Half=H/2, LabelY=30, Duration=420;
constexpr int DigitPixels=W*H, FlapPixels=W*Half;
constexpr unsigned DoubleTapMs=320;
struct Palette { uint32_t background,card,ink,hinge,muted; uint8_t shadow; };
constexpr Palette Light={0xfaf9f7,0xffffff,0x171717,0xdcdcd8,0x8a8a86,25};
constexpr Palette Dark={0x101216,0x252a32,0xf0f3f7,0x11151b,0xb0bac8,100};
struct Cell {
 lv_obj_t *object=nullptr,*card=nullptr;
 lv_img_dsc_t upper{},lower{},flap{};
 lv_color_t *pixels=nullptr;
 int shown=10,target=10,height=0,y=0;
 uint32_t started=0;
 bool moving=false;
};
Cell cells[6];
lv_obj_t *view=nullptr,*dateLabel=nullptr,*weatherLabel=nullptr;
lv_timer_t *timer=nullptr;
lv_color_t *raster=nullptr;
bool initialized=false,timeWasValid=false;
bool darkTheme=false,saveFailed=false,pendingTap=false;
uint32_t tappedAt=0;
int64_t lastEpoch=-1;
const Palette &palette(){return darkTheme?Dark:Light;}

lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color) {
 auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);
 lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,255,0);
 lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
void descriptor(lv_img_dsc_t &d,lv_color_t *data,int height) {
 d={};d.header.cf=LV_IMG_CF_TRUE_COLOR;d.header.w=W;d.header.h=height;
 d.data_size=W*height*sizeof(lv_color_t);d.data=(const uint8_t*)data;
}
void draw(lv_event_t *e) {
 auto &c=*(Cell*)lv_event_get_user_data(e);
 auto *ctx=lv_event_get_draw_ctx(e);lv_area_t area;lv_obj_get_coords(c.object,&area);
 if(raster) {
  descriptor(c.upper,raster+(c.moving?c.target:c.shown)*DigitPixels,Half);
  descriptor(c.lower,raster+c.shown*DigitPixels+FlapPixels,Half);
  lv_draw_img_dsc_t d;lv_draw_img_dsc_init(&d);
  lv_area_t half={area.x1,area.y1,area.x2,(lv_coord_t)(area.y1+Half-1)};
  lv_draw_img(ctx,&d,&half,&c.upper);
  half.y1+=Half;half.y2+=Half;lv_draw_img(ctx,&d,&half,&c.lower);
  if(c.moving&&c.height>0) {
   descriptor(c.flap,c.pixels,c.height);
   half.y1=area.y1+c.y;half.y2=half.y1+c.height-1;
   lv_draw_img(ctx,&d,&half,&c.flap);
  }
 } else {
  // An allocation failure keeps a readable static clock.
  lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);
  d.font=saved_gift_font_bold_56_0;d.color=lv_color_hex(palette().ink);d.align=LV_TEXT_ALIGN_CENTER;
  char text[2]={(char)(c.shown==10?'-':'0'+c.shown),0};
  area.y1+=LabelY;lv_draw_label(ctx,&d,&area,text,nullptr);
  lv_obj_get_coords(c.object,&area);
 }
 lv_draw_line_dsc_t line;lv_draw_line_dsc_init(&line);line.color=lv_color_hex(palette().hinge);line.width=2;
 lv_point_t a={area.x1,(lv_coord_t)(area.y1+Half)},b={area.x2,a.y};
 lv_draw_line(ctx,&line,&a,&b);
}
void settle(Cell &c) {c.shown=c.target;c.moving=false;c.height=0;}
void deform(Cell &c,uint32_t elapsed) {
 if(elapsed>=Duration){settle(c);return;}
 // Cosine models the projected height of a rotating half-card. In both phases
 // the edge at the middle stays fixed; the glyph and paper deform together.
 bool lower=elapsed>=Duration/2;
 int angle=(int)(elapsed*180/Duration); // lv_trigo_cos uses degrees, scale 32767
 int height=(Half*abs((int)lv_trigo_cos(angle))+16383)/32767;
 c.height=height;c.y=lower?Half:Half-height;
 if(!height)return;
 auto *src=raster+(lower?c.target:c.shown)*DigitPixels+(lower?FlapPixels:0);
 uint8_t shade=(uint8_t)(72*(Half-height)/Half);
 for(int y=0;y<height;y++) {
  int sy=(y*Half+height/2)/height;if(sy>=Half)sy=Half-1;
  for(int x=0;x<W;x++)c.pixels[y*W+x]=lv_color_mix(lv_color_black(),src[sy*W+x],shade);
 }
}
void deleted(lv_event_t *) {
 cancel();view=nullptr;dateLabel=weatherLabel=nullptr;
 free(raster);raster=nullptr;for(auto &c:cells)c=Cell{};initialized=false;
}
void tick(lv_timer_t *) {
 refresh();uint32_t now=lv_tick_get();
 if(pendingTap&&now-tappedAt>=DoubleTapMs)pendingTap=false;
 for(auto &c:cells)if(c.moving){deform(c,now-c.started);lv_obj_invalidate(c.object);}
}
void makeRaster() {
 if(!raster)raster=(lv_color_t*)miniAppAlloc((11*DigitPixels+6*FlapPixels)*sizeof(lv_color_t));
 if(!raster)return;
 auto *canvas=lv_canvas_create(view);
 lv_obj_add_flag(canvas,LV_OBJ_FLAG_HIDDEN);
 lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);
 d.font=saved_gift_font_bold_56_0;d.color=lv_color_hex(palette().ink);d.align=LV_TEXT_ALIGN_CENTER;
 for(int i=0;i<11;i++) {
  lv_canvas_set_buffer(canvas,raster+i*DigitPixels,W,H,LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(canvas,lv_color_hex(palette().card),LV_OPA_COVER);
  char text[2]={(char)(i==10?'-':'0'+i),0};lv_canvas_draw_text(canvas,0,LabelY,W,&d,text);
 }
 lv_obj_del(canvas);
}
void applyTheme(){
 const auto &colors=palette();
 lv_obj_set_style_bg_color(view,lv_color_hex(colors.background),0);
 for(auto &c:cells){
  lv_img_cache_invalidate_src(&c.upper);lv_img_cache_invalidate_src(&c.lower);lv_img_cache_invalidate_src(&c.flap);
  settle(c);
  lv_obj_set_style_bg_color(c.card,lv_color_hex(colors.card),0);
  lv_obj_set_style_shadow_opa(c.card,colors.shadow,0);
  lv_obj_set_style_bg_color(c.object,lv_color_hex(colors.card),0);
 }
 makeRaster();
 for(int i=0;i<6;i++)cells[i].pixels=raster?raster+11*DigitPixels+i*FlapPixels:nullptr;
 lv_obj_set_style_text_color(dateLabel,lv_color_hex(colors.ink),0);
 lv_obj_set_style_text_color(weatherLabel,lv_color_hex(colors.muted),0);
 lv_obj_invalidate(view);
}
void clicked(lv_event_t *){
 if(Touch::swiped()){pendingTap=false;return;}
 if(auto *input=lv_indev_get_act()){
  lv_point_t point;lv_indev_get_point(input,&point);
  if(point.x<20){pendingTap=false;return;}
 }
 lastInputAt=millis();const uint32_t now=lv_tick_get();
 if(!pendingTap||now-tappedAt>=DoubleTapMs){pendingTap=true;tappedAt=now;return;}
 pendingTap=false;
 JsonDocument patch;patch["clockMode"]["flipDark"]=!darkTheme;String error;
 if(!GiftData::patch(patch.as<JsonVariantConst>(),error)){
  saveFailed=true;notifyUser(error,true);return;
 }
 saveFailed=false;darkTheme=!darkTheme;applyTheme();
}
}
bool active(){return view!=nullptr;}
bool darkMode(){return view?darkTheme:bool(GiftData::get("clockMode")["flipDark"]|false);}
bool storageFailed(){return saveFailed;}
int movingDigits(){int n=0;for(auto &c:cells)if(c.moving)n++;return n;}
void cancel(){if(timer){lv_timer_del(timer);timer=nullptr;}pendingTap=false;}
void build(lv_obj_t *parent) {
 darkTheme=GiftData::get("clockMode")["flipDark"]|false;saveFailed=false;pendingTap=false;
 const auto &colors=palette();
 view=box(parent,0,0,320,240,colors.background);lv_obj_add_event_cb(view,deleted,LV_EVENT_DELETE,nullptr);
 lv_obj_add_flag(view,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(view,clicked,LV_EVENT_CLICKED,nullptr);
 makeRaster();
 for(int i=0;i<6;i++) {
  auto *card=box(view,8+i*50+(i/2)*4,58,W,H,colors.card);
  lv_obj_set_style_radius(card,4,0);lv_obj_set_style_clip_corner(card,true,0);
  lv_obj_set_style_shadow_width(card,5,0);lv_obj_set_style_shadow_opa(card,colors.shadow,0);
  auto *o=box(card,0,0,W,H,colors.card);
  cells[i]=Cell{};cells[i].object=o;cells[i].card=card;
  if(raster)cells[i].pixels=raster+11*DigitPixels+i*FlapPixels;
  lv_obj_add_event_cb(o,draw,LV_EVENT_DRAW_MAIN,&cells[i]);
 }
 dateLabel=Ui::label(view,"",6,194,308,saved_gift_font_bold_14_0,colors.ink);
 weatherLabel=Ui::label(view,"",6,216,308,&fd_sans_bold_12,colors.muted);
 for(auto *label:{dateLabel,weatherLabel}){
  lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_clear_flag(label,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 }
 lv_label_set_long_mode(weatherLabel,LV_LABEL_LONG_DOT);
 initialized=false;timeWasValid=false;lastEpoch=-1;refresh();timer=lv_timer_create(tick,20,nullptr);
}
void refresh() {
 if(!view)return;
 int64_t epoch=ClockSource::now();auto d=DeskLogic::beijingDate(epoch);bool valid=DeskLogic::timeValid(epoch);
 int values[]={d.hour/10,d.hour%10,d.minute/10,d.minute%10,d.second/10,d.second%10};
 bool snap=!initialized||!valid||!timeWasValid;
 if(epoch!=lastEpoch||!initialized) {
  for(int i=0;i<6;i++) {
   auto &c=cells[i];int value=valid?values[i]:10;
   if(snap||!raster){c.target=value;settle(c);lv_obj_invalidate(c.object);}
   else if(value!=c.target) {
    settle(c);c.target=value;c.started=lv_tick_get();c.moving=true;
    deform(c,0);lv_obj_invalidate(c.object);
   }
  }
  static const char *week[]={"日","一","二","三","四","五","六"};
  char date[70];int weekday=(int)((DeskLogic::civilDays(d.year,d.month,d.day)+4)%7+7)%7;
  if(valid)snprintf(date,sizeof(date),"%04d年 %d月 %d日 周%s",d.year,d.month,d.day,week[weekday]);
  else snprintf(date,sizeof(date),"等待联网校时");lv_label_set_text(dateLabel,date);
  lastEpoch=epoch;initialized=true;timeWasValid=valid;
  String line=weather.valid?weather.condition+" "+TextUtil::number(weather.temperature,"°C"):String("天气等待更新");
  if(String(lv_label_get_text(weatherLabel))!=line)lv_label_set_text(weatherLabel,line.c_str());
 }
}
}
