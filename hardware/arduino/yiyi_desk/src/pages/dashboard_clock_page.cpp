// Native LVGL 8 adaptation of teojs/clock-dashboard. See assets/dashboard_clock_sources.md.
#include "dashboard_clock_page.h"
#include "dashboard_clock_config.h"
#include "mini_apps_memory.h"
#include "assets/dashboard_clock_masks.h"
#include "assets/dashboard_clock_calendar.h"
#include "../app_state.h"
#include "../clock_source.h"
#include "../gift_data.h"
#include "../gift_lunar.h"
#include "../gift_assets/spring_dates.h"
#include "../gift_assets/gift_fonts.h"
#include "../saved_design/saved_resources.h"
#include "../text_util.h"
#include "../touch.h"
#include "../ui_widgets.h"
#include "../../desk_logic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace DashboardClockPage {
namespace {
using DashboardClockConfig::Config;
constexpr unsigned RollMs=800,TiltMs=600,LayoutMs=500;
constexpr int RasterPad=12;
struct Geometry { int x=0,y=0,zoom=256,opa=255; };
struct Cell {
    lv_obj_t *image=nullptr;
    lv_img_dsc_t descriptor{};
    uint8_t *pixels=nullptr;
    int shown=10,target=10,offset=0,angle=0,angleFrom=0,angleTo=0;
    uint32_t started=0,tiltedAt=0;
    unsigned delay=0;
    bool moving=false,tilting=false;
    Geometry geometry,from,to;
};
Cell cells[6];
lv_obj_t *view=nullptr,*stage=nullptr,*effects=nullptr,*icon=nullptr;
lv_obj_t *dayLabel=nullptr,*dateLabel=nullptr,*monthLabel=nullptr,*lunarLabel=nullptr,*yearLabel=nullptr,*festivalLabel=nullptr;
lv_obj_t *conditionLabel=nullptr,*cityLabel=nullptr,*temperatureLabel=nullptr,*rangeLabel=nullptr,*metricsLabel=nullptr;
lv_obj_t *temperatureUnit=nullptr,*lowLabel=nullptr,*metricIcons=nullptr,*metricLabels[4]{};
lv_timer_t *timer=nullptr;
Config config;
const char *mode="dashboard";
uint8_t *memory=nullptr,*glyphs=nullptr;
int glyphW=76,glyphH=112,pixelCount=0,canvasW=100,canvasH=136,canvasPixels=0;
bool initialized=false,timeWasValid=false,showSeconds=false,showLeading=true,layoutMoving=false;
bool refreshRequested=false,revealSeconds=false,iconDay=true;
bool failRaster=false;
#ifdef GIFT_HOST_TEST
uint32_t paintCounts[6]{},iconInvalidations=0;
#endif
uint32_t layoutAt=0,lastIconAt=0,lastEffectAt=0,lastRevision=0;
int64_t lastEpoch=-1;
char targetTime[9]="--:--";
Geometry separators[2],separatorsFrom[2],separatorsTo[2];
uint32_t randomState=0x53c10c17u;
int weatherKind=0; // 0 unknown, 1 sun/moon, 2 cloud, 3 fog, 4 rain, 5 snow, 6 thunder
struct Particle { float x,y,speed,phase; } particles[20];
uint32_t nextFlash=0,flashAt=0;
bool flashing=false;

uint32_t randomNext(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
bool compact(){return !strcmp(mode,"compact");}
bool minimal(){return !strcmp(mode,"minimal");}
bool effectiveTilt(){return config.tilt&&!compact()&&memory;}
bool sameConfig(const Config &a,const Config &b){return a.color==b.color&&a.weight==b.weight&&a.opacity==b.opacity&&a.spacing==b.spacing&&a.seconds==b.seconds&&a.hour24==b.hour24&&a.tilt==b.tilt&&a.lunar==b.lunar&&a.festival==b.festival&&a.rain==b.rain&&a.snow==b.snow&&a.thunder==b.thunder;}
int clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
uint32_t brighten(uint32_t color,float factor){int r=clamp(int(((color>>16)&255)*factor),0,255),g=clamp(int(((color>>8)&255)*factor),0,255),b=clamp(int((color&255)*factor),0,255);return (r<<16)|(g<<8)|b;}
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,bool opaque=false){
 auto *object=lv_obj_create(parent);lv_obj_remove_style_all(object);lv_obj_set_pos(object,x,y);lv_obj_set_size(object,w,h);
 lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 if(opaque){lv_obj_set_style_bg_color(object,lv_color_black(),0);lv_obj_set_style_bg_opa(object,255,0);}return object;
}
lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const lv_font_t *font,uint32_t color=0xffffff){
 auto *object=Ui::label(parent,"",x,y,w,font,color);lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 lv_label_set_long_mode(object,LV_LABEL_LONG_DOT);return object;
}
void text(lv_obj_t *object,const String &value){if(object&&String(lv_label_get_text(object))!=value)lv_label_set_text(object,value.c_str());}
void compactText(lv_obj_t *object,const String &value,const lv_font_t *preferred,int height){
 if(!object)return;
 const lv_font_t *fonts[]={preferred,&fd_sans_bold_12,&fd_sans_bold_10};
 const lv_font_t *selected=fonts[2];
 for(const auto *font:fonts){
  lv_point_t size;lv_txt_get_size(&size,value.c_str(),font,0,0,lv_obj_get_width(object),LV_TEXT_FLAG_NONE);
  if(size.y<=height){selected=font;break;}
 }
 if(lv_obj_get_style_text_font(object,0)!=selected)lv_obj_set_style_text_font(object,selected,0);
 text(object,value);
}
void centered(lv_obj_t *object){lv_obj_set_style_text_align(object,LV_TEXT_ALIGN_CENTER,0);}
void hidden(lv_obj_t *object,bool value){if(!object)return;if(value)lv_obj_add_flag(object,LV_OBJ_FLAG_HIDDEN);else lv_obj_clear_flag(object,LV_OBJ_FLAG_HIDDEN);}
float bezier(float x){
 // Solve the CSS curve's x(t), then evaluate y(t); y deliberately overshoots 1.
 float low=0,high=1,t=x;
 for(int i=0;i<12;i++){t=(low+high)*.5f;float a=1-t,v=3*a*a*t*.18f+3*a*t*t*.43f+t*t*t;if(v<x)low=t;else high=t;}
 float a=1-t;return 3*a*a*t*.18f+3*a*t*t*1.34f+t*t*t;
}
float layoutEase(float x){float a=1-x;return 1-a*a*a;}
Geometry interpolate(const Geometry &a,const Geometry &b,float t){Geometry g;g.x=a.x+(int)lroundf((b.x-a.x)*t);g.y=a.y+(int)lroundf((b.y-a.y)*t);g.zoom=a.zoom+(int)lroundf((b.zoom-a.zoom)*t);g.opa=a.opa+(int)lroundf((b.opa-a.opa)*t);return g;}
void apply(Cell &cell,const Geometry &g){
 cell.geometry=g;if(!cell.image)return;
 lv_obj_set_pos(cell.image,g.x,g.y);lv_img_set_zoom(cell.image,(uint16_t)g.zoom);
    lv_obj_set_style_img_opa(cell.image,(lv_opa_t)(g.opa*config.opacity/100),0);
 hidden(cell.image,g.opa==0);
}
void layout(bool animate){
 int count=showSeconds?6:4,first=showLeading?0:1;
 float spacing=compact()?0.0f:float(config.spacing+18)/100.0f;
 float pitch=glyphW*(compact()?1.0f:.84f+spacing);
 // Each colon owns a gap between digit groups. Negative spacing only changes
 // the two digits inside a group; it must never consume the colon's space.
 // Include the rebound border in the worst-case horizontal tilt projection.
 float rotationExtra=effectiveTilt()?LV_MAX(0.0f,(glyphH+2*RasterPad)*.104529f-glyphW*.005478f):0.0f;
 float separator=compact()?18.0f:24.0f+rotationExtra;
 float digitX[6],separatorX[2],cursor=0,width=0;
 for(int group=0;group<3;group++){
  int begin=group?group*2:first,end=group*2+1;
  if(!group&&first)digitX[0]=cursor-pitch;
  for(int i=begin;i<=end;i++)digitX[i]=cursor+(i-begin)*pitch;
  cursor+=glyphW+(end-begin)*pitch;
  if(end==count-1)width=cursor;
  if(group<2){separatorX[group]=cursor+separator*.5f;cursor+=separator;}
 }
 // Keep the whole tilted silhouette within the screen, including the tall
 // clock-only digits. The transparent raster border is not part of spacing.
 float maxWidth=compact()?188.0f:304.0f;
 float scale=LV_MIN(1.0f,maxWidth/(width+rotationExtra));
 float start=(compact()?96.0f:160.0f)-width*scale*.5f;
 int centerY=compact()?120:minimal()?120:123;
 for(unsigned i=0;i<6;i++){
  auto &c=cells[i];Geometry next;
  float x=start+digitX[i]*scale;
  next.x=(int)lroundf(x);next.y=centerY-glyphH/2;next.zoom=(int)lroundf(scale*256);next.opa=(i<(unsigned)count&&(i||showLeading))?(i>=4?160:255):0;
  // LVGL zoom uses the glyph centre as pivot. Offset the image's unscaled box
  // so its visible top-left is exactly the layout's scaled position.
  next.x-=(glyphW-(glyphW*next.zoom/256))/2+RasterPad;
  next.y-=RasterPad;
  c.from=c.geometry;c.to=next;if(!animate)apply(c,next);
 }
 for(int j=0;j<2;j++){
  Geometry next;float x=start+separatorX[j]*scale;
  next.x=(int)lroundf(x);next.y=centerY;next.zoom=(int)lroundf(scale*256);next.opa=(j==0||showSeconds)?255:0;
  separatorsFrom[j]=separators[j];separatorsTo[j]=next;if(!animate)separators[j]=next;
 }
 layoutMoving=animate;layoutAt=lv_tick_get();if(stage)lv_obj_invalidate(stage);
}
void releaseRaster(){
 for(auto &cell:cells){if(cell.image){lv_img_cache_invalidate_src(&cell.descriptor);cell.descriptor.data=nullptr;}cell.pixels=nullptr;}
 free(memory);memory=glyphs=nullptr;
}
void loadMasks(){
 if(failRaster)return;
 const auto &font=dashboard_clock_mask_fonts[compact()?9:clamp(config.weight/100-1,0,8)];
 pixelCount=glyphW*glyphH;canvasW=glyphW+2*RasterPad;canvasH=glyphH+2*RasterPad;canvasPixels=canvasW*canvasH;
 size_t amount=(size_t)pixelCount*11+(size_t)canvasPixels*6*3;
 memory=(uint8_t*)miniAppAlloc(amount);if(!memory)return;glyphs=memory;
 auto *decoded=(uint8_t*)miniAppAlloc(font.width*font.height);
 if(!decoded){releaseRaster();return;}
 for(int digit=0;digit<11;digit++){
  const auto &entry=font.glyphs[digit];const auto *data=dashboard_clock_mask_data+entry.offset;
  unsigned n=0;for(unsigned j=0;j<entry.length;j++){unsigned run=(data[j]>>4)+1;uint8_t alpha=(data[j]&15)*17;while(run--&&n<(unsigned)(font.width*font.height))decoded[n++]=alpha;}
  for(int y=0;y<glyphH;y++)for(int x=0;x<glyphW;x++){
   // Bilinear resampling happens once per selected font, not once per frame.
   float sx=((x+.5f)*font.width/glyphW)-.5f,sy=((y+.5f)*font.height/glyphH)-.5f;
   int ix=clamp((int)floorf(sx),0,font.width-1),iy=clamp((int)floorf(sy),0,font.height-1),ix2=clamp(ix+1,0,font.width-1),iy2=clamp(iy+1,0,font.height-1);
   float fx=sx-floorf(sx),fy=sy-floorf(sy);
   float a=decoded[iy*font.width+ix]*(1-fx)+decoded[iy*font.width+ix2]*fx,b=decoded[iy2*font.width+ix]*(1-fx)+decoded[iy2*font.width+ix2]*fx;
   glyphs[digit*pixelCount+y*glyphW+x]=(uint8_t)lroundf(a*(1-fy)+b*fy);
  }
 }
 free(decoded);
 for(unsigned i=0;i<6;i++){
  auto &c=cells[i];c.pixels=memory+11*pixelCount+i*3*canvasPixels;
  auto ink=lv_color_hex(brighten(config.color,i%2?1.25f:1.0f));
  for(int p=0;p<canvasPixels;p++){c.pixels[p*2]=ink.full&255;c.pixels[p*2+1]=ink.full>>8;}
  memset(c.pixels+canvasPixels*2,0,canvasPixels);
  c.descriptor={};c.descriptor.header.cf=LV_IMG_CF_RGB565A8;c.descriptor.header.w=canvasW;c.descriptor.header.h=canvasH;c.descriptor.data=c.pixels;c.descriptor.data_size=3*canvasPixels;
 }
}
void paint(Cell &cell){
 if(!memory||!cell.image)return;
#ifdef GIFT_HOST_TEST
 ++paintCounts[&cell-cells];
#endif
 auto *alpha=cell.pixels+2*canvasPixels;
 memset(alpha,0,canvasPixels);
 for(int y=0;y<canvasH;y++){
  // Retain the normal rolling viewport. Only the target's rebound may enter
  // the upper border; ordinary outgoing rows must not spill into the header.
  if(y>=RasterPad+glyphH||(y<RasterPad&&(!cell.moving||cell.offset<=glyphH)))continue;
  int oldY=y-RasterPad+cell.offset,newY=oldY-glyphH;const uint8_t *source=nullptr;
  if(oldY>=0&&oldY<glyphH)source=glyphs+cell.shown*pixelCount+oldY*glyphW;
  else if(cell.moving&&cell.offset>0&&newY>=0&&newY<glyphH)source=glyphs+cell.target*pixelCount+newY*glyphW;
  if(source)memcpy(alpha+y*canvasW+RasterPad,source,glyphW);
 }
 lv_img_cache_invalidate_src(&cell.descriptor);lv_obj_invalidate(cell.image);
}
void settle(Cell &cell){cell.shown=cell.target;cell.offset=0;cell.moving=false;paint(cell);}
void rotateTo(Cell &cell,bool snap){
 int angle=effectiveTilt()?int(randomNext()%121)-60:0;
 cell.angleFrom=cell.angle;cell.angleTo=angle;cell.tiltedAt=lv_tick_get();cell.tilting=!snap&&cell.angle!=angle;
 if(snap){cell.angle=angle;if(cell.image)lv_img_set_angle(cell.image,angle);}
}
void rect(lv_draw_ctx_t *ctx,int x,int y,int w,int h,uint32_t color,int radius=0,int opacity=255){
 lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=(lv_opa_t)opacity;d.radius=radius;
 lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+h-1)};lv_draw_rect(ctx,&d,&a);
}
void line(lv_draw_ctx_t *ctx,int x,int y,int x2,int y2,uint32_t color,int width=1,int opacity=255){
 lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=lv_color_hex(color);d.width=width;d.opa=(lv_opa_t)opacity;d.round_start=d.round_end=1;
 lv_point_t a={(lv_coord_t)x,(lv_coord_t)y},b={(lv_coord_t)x2,(lv_coord_t)y2};lv_draw_line(ctx,&d,&a,&b);
}
void drawStage(lv_event_t *event){
 auto *ctx=lv_event_get_draw_ctx(event);
 if(!memory){
  lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=&lv_font_montserrat_48;d.color=lv_color_hex(config.color);d.opa=(lv_opa_t)(config.opacity*255/100);d.align=LV_TEXT_ALIGN_CENTER;
  lv_area_t a={(lv_coord_t)(compact()?4:8),(lv_coord_t)(compact()?91:minimal()?91:94),(lv_coord_t)(compact()?188:311),159};
  const char *value=!showLeading&&targetTime[0]=='0'?targetTime+1:targetTime;
  lv_draw_label(ctx,&d,&a,value,nullptr);return;
 }
 for(int j=0;j<2;j++){
  const auto &g=separators[j];if(!g.opa)continue;
  int radius=LV_MAX(2,(compact()?7:5)*g.zoom/256),gap=(compact()?35:25)*g.zoom/256;
  int opacity=g.opa*config.opacity/100;
  if(j)opacity=opacity*7/10;
  for(int n=-1;n<=1;n+=2)rect(ctx,g.x-radius,g.y+n*gap-radius,radius*2,radius*2,brighten(config.color,1.8f),LV_RADIUS_CIRCLE,opacity);
 }
}
int classify(){
 if(!weather.valid||weather.code<0)return 0;int code=weather.code;
 if(code==0)return 1;if(code<=3)return 2;if(code<=49)return 3;
 if((code>=71&&code<=77)||code==85||code==86)return 5;
 if(code>=95)return 6;return 4;
}
void drawIcon(lv_event_t *event){
 auto *ctx=lv_event_get_draw_ctx(event);lv_area_t a;lv_obj_get_coords(icon,&a);int w=lv_area_get_width(&a),h=lv_area_get_height(&a),cx=a.x1+w/2,cy=a.y1+h/2;
 int size=LV_MIN(w,h),r=LV_MAX(4,size/7),kind=weatherKind;
 if(kind==1||kind==2){
  int sunX=kind==2?cx-size/7:cx,sunY=kind==2?cy-size/7:cy;
  if(weather.day){
   int rotation=(int)((lv_tick_get()%45000)*360/45000);
   for(int n=0;n<8;n++){int angle=rotation+n*45;float co=lv_trigo_cos(angle)/32767.0f,si=lv_trigo_sin(angle)/32767.0f;
    line(ctx,sunX+(int)(co*(r+3)),sunY+(int)(si*(r+3)),sunX+(int)(co*(r+7)),sunY+(int)(si*(r+7)),0xffc83d,2);
   }rect(ctx,sunX-r,sunY-r,r*2,r*2,0xffc43d,LV_RADIUS_CIRCLE);
  }else{rect(ctx,sunX-r-2,sunY-r-2,r*2+4,r*2+4,0xc5d9ff,LV_RADIUS_CIRCLE);rect(ctx,sunX-r+4,sunY-r-4,r*2+1,r*2+1,0x000000,LV_RADIUS_CIRCLE);}
 }
 if(kind>=2&&kind!=3){
  int drift=(int)(lv_trigo_sin((lv_tick_get()/45)%360)*2/32767),y=cy+(kind==2?5:0),x=cx+drift;
  uint32_t cloud=weather.day?0xe5edf9:0x9baac7;
  rect(ctx,x-size/3,y-2,size*2/3,size/4,cloud,size/8);rect(ctx,x-size/4,y-size/6,size/3,size/3,cloud,LV_RADIUS_CIRCLE);rect(ctx,x,y-size/8,size/4,size/4,cloud,LV_RADIUS_CIRCLE);
  if(kind>=4){
   int travel=(lv_tick_get()/100)%8;
   for(int n=0;n<3;n++){int px=cx-9+n*9,py=cy+size/4+(travel+n*3)%8;
    if(kind==5){line(ctx,px-2,py,px+2,py,0xcce9ff,1);line(ctx,px,py-2,px,py+2,0xcce9ff,1);}else line(ctx,px+1,py,px-1,py+4,0x6caaf5,2);
   }
  }
  if(kind==6){line(ctx,cx+3,cy+4,cx-2,cy+11,0xffdd74,3);line(ctx,cx-2,cy+11,cx+4,cy+10,0xffdd74,3);line(ctx,cx+4,cy+10,cx,cy+16,0xffdd74,3);}
 }else if(kind==3){for(int n=0;n<3;n++)line(ctx,cx-size/3+n%2*3,cy-7+n*7,cx+size/3-n%2*3,cy-7+n*7,0xb9c9d8,3);}
 else if(kind==0){rect(ctx,cx-12,cy-7,24,14,0x6b778a,7);}
}
void drawMetricIcons(lv_event_t *event){
 auto *ctx=lv_event_get_draw_ctx(event);lv_area_t a;lv_obj_get_coords(metricIcons,&a);
 int x=a.x1,y=a.y1;
 // Small coloured symbols distinguish humidity, air quality, feels-like and UV.
 line(ctx,x+5,y+1,x+1,y+7,0x598fe0,2);line(ctx,x+5,y+1,x+9,y+7,0x598fe0,2);line(ctx,x+1,y+7,x+2,y+11,0x598fe0,2);line(ctx,x+2,y+11,x+8,y+11,0x598fe0,2);line(ctx,x+8,y+11,x+9,y+7,0x598fe0,2);
 if(compact())return;
 int leaf=x+44;line(ctx,leaf+1,y+10,leaf+8,y+2,0x77bc92,2);line(ctx,leaf+1,y+10,leaf+1,y+5,0x77bc92,2);line(ctx,leaf+1,y+5,leaf+8,y+1,0x77bc92,2);line(ctx,leaf+8,y+1,leaf+9,y+7,0x77bc92,2);line(ctx,leaf+9,y+7,leaf+1,y+10,0x77bc92,2);
 int bodyY=y+27;rect(ctx,x+4,bodyY,4,4,0xe9a363,LV_RADIUS_CIRCLE);line(ctx,x+6,bodyY+5,x+6,bodyY+10,0xe9a363,2);line(ctx,x+1,bodyY+5,x+6,bodyY+7,0xe9a363,2);line(ctx,x+6,bodyY+7,x+10,bodyY+5,0xe9a363,2);line(ctx,x+6,bodyY+10,x+3,bodyY+14,0xe9a363,2);line(ctx,x+6,bodyY+10,x+9,bodyY+14,0xe9a363,2);
 int sunX=leaf+5,sunY=bodyY+7;rect(ctx,sunX-2,sunY-2,5,5,0xb28be2,LV_RADIUS_CIRCLE);for(int angle=0;angle<360;angle+=60){float co=lv_trigo_cos(angle)/32767.0f,si=lv_trigo_sin(angle)/32767.0f;line(ctx,sunX+(int)(co*5),sunY+(int)(si*5),sunX+(int)(co*7),sunY+(int)(si*7),0xb28be2,1);}
}
bool particlesActive(){return !minimal()&&((weatherKind==4&&config.rain)||(weatherKind==5&&config.snow)||(weatherKind==6&&config.rain));}
void drawEffects(lv_event_t *event){
 if(minimal())return;auto *ctx=lv_event_get_draw_ctx(event);
 if(particlesActive())for(const auto &p:particles){
  int x=(int)p.x,y=(int)p.y;
  if(weatherKind==5)rect(ctx,x,y,2+(int)(p.phase)%2,2+(int)(p.phase)%2,0xdceaff,LV_RADIUS_CIRCLE,65);
  else line(ctx,x,y,x-3,y+9,0x6ba2df,1,65);
 }
 if(flashing){
  rect(ctx,0,0,320,240,0xc8dcff,0,20);
  const int points[][2]={{238,0},{221,29},{234,28},{213,59},{225,55},{207,83}};
  for(unsigned n=1;n<sizeof(points)/sizeof(points[0]);n++){
   line(ctx,points[n-1][0],points[n-1][1],points[n][0],points[n][1],0x9ec4ff,5,50);
   line(ctx,points[n-1][0],points[n-1][1],points[n][0],points[n][1],0xeaf4ff,1,190);
  }
 }
}
const char *festival(const DeskLogic::Civil &d,const GiftLunar::Date &lunar){
 (void)lunar;
 if(d.year<1900||d.year>2099)return "";
 unsigned first=dashboard_clock_year_offsets[d.year-1900],last=dashboard_clock_year_offsets[d.year-1899];int key=d.month*100+d.day;
 for(unsigned i=first;i<last;i++){const auto &event=dashboard_clock_calendar_events[i];if(event.date==key)return dashboard_clock_event_names[event.label];if(event.date>key)break;}return "";
}
void updateDate(const DeskLogic::Civil &d,bool valid){
 if(minimal())return;
 if(!valid){text(dayLabel,"--");text(dateLabel,compact()?"待校时":"等待联网校时");text(monthLabel,"");text(lunarLabel,"");text(yearLabel,"");text(festivalLabel,"");return;}
 static const char *week[]={"日","一","二","三","四","五","六"};
 int weekday=(int)(((DeskLogic::civilDays(d.year,d.month,d.day)+4)%7+7)%7);char buf[64];snprintf(buf,sizeof(buf),"%d",d.day);text(dayLabel,buf);
 snprintf(buf,sizeof(buf),"%s%s",compact()?"周":"星期",week[weekday]);text(dateLabel,buf);
 snprintf(buf,sizeof(buf),compact()?"%d月":"%04d年%d月",compact()?d.month:d.year,d.month);text(monthLabel,buf);
 auto lunar=GiftLunar::fromSolar(d.year,d.month,d.day);char date[48];GiftLunar::format(lunar,date,sizeof(date));
 const char *event=config.festival?festival(d,lunar):"";
 const char *monthEnd=strstr(date,"月");const char *day=monthEnd?monthEnd+strlen("月"):date;
 char lunarMonth[24]="";if(monthEnd){size_t count=LV_MIN((size_t)(monthEnd-date+strlen("月")),sizeof(lunarMonth)-1);memcpy(lunarMonth,date,count);lunarMonth[count]='\0';}
 String line=config.lunar?String(compact()?date:day):String("");
 if(!compact()&&event[0]){if(line.length())line+="·";line+=event;}
 if(compact()){
  compactText(lunarLabel,line,&fd_sans_bold_14_ui,18);
  compactText(festivalLabel,event,&fd_sans_bold_12,30);
 }else{text(lunarLabel,line);text(festivalLabel,"");}
 if(config.lunar&&lunar.valid){
  static const char *stems[]={"甲","乙","丙","丁","戊","己","庚","辛","壬","癸"},*branches[]={"子","丑","寅","卯","辰","巳","午","未","申","酉","戌","亥"},*animals[]={"鼠","牛","虎","兔","龙","蛇","马","羊","猴","鸡","狗","猪"};
  int year=d.year;if(d.year>=1900&&d.year<=2099&&d.month*100+d.day<GiftSpringDates[d.year-1900])--year;
  int ganzhiYear=d.year;if(d.year>=1900&&d.year<=2099&&d.month*100+d.day<dashboard_clock_lichun[d.year-1900])--ganzhiYear;
  int stem=((ganzhiYear-4)%10+10)%10,branch=((ganzhiYear-4)%12+12)%12,animal=((year-4)%12+12)%12;
  snprintf(buf,sizeof(buf),"%s%s(%s)年",stems[stem],branches[branch],animals[animal]);
  text(yearLabel,compact()?String(buf):String(buf)+lunarMonth);
 }else text(yearLabel,"");
}
void updateWeather(){
 if(minimal())return;int nextKind=classify();bool kindChanged=nextKind!=weatherKind;
 if(kindChanged||iconDay!=weather.day){weatherKind=nextKind;iconDay=weather.day;if(icon)lv_obj_invalidate(icon);if(kindChanged&&effects)lv_obj_invalidate(effects);}
 if(compact()){
  compactText(conditionLabel,weather.valid?weather.condition:String("待更新"),&fd_sans_bold_14_ui,30);
  compactText(cityLabel,settings.city.length()?settings.city:String("未设置地区"),&fd_sans_bold_12,26);
 }else{text(conditionLabel,weather.valid?weather.condition:String("天气待更新"));text(cityLabel,settings.city.length()?settings.city:String("未设置地区"));}
 String temperature=TextUtil::number(weather.valid?weather.temperature:NAN,compact()?"°":"");
 if(!compact()&&temperatureLabel&&String(lv_label_get_text(temperatureLabel))!=temperature){
  lv_point_t size;lv_txt_get_size(&size,temperature.c_str(),&fd_sans_bold_39,0,0,320,LV_TEXT_FLAG_NONE);
  const auto *font=size.x>53?&fd_sans_bold_26:&fd_sans_bold_39;
  if(lv_obj_get_style_text_font(temperatureLabel,0)!=font)lv_obj_set_style_text_font(temperatureLabel,font,0);
 }
 text(temperatureLabel,compact()&&!weather.valid?String(""):temperature);
 text(temperatureUnit,compact()?"":"°C");
 if(compact()){
  text(rangeLabel,weather.valid?"":"等待天气数据");
  text(metricsLabel,"");text(metricLabels[0],weather.valid?TextUtil::number(weather.humidity,"%"):String(""));
 }else{
  text(rangeLabel,TextUtil::number(weather.valid?weather.high:NAN,"°"));text(lowLabel,TextUtil::number(weather.valid?weather.low:NAN,"°"));
  text(metricLabels[0],TextUtil::number(weather.valid?weather.humidity:NAN,"%"));text(metricLabels[1],TextUtil::number(weather.valid?weather.aqi:NAN));
  text(metricLabels[2],TextUtil::number(weather.valid?weather.feels:NAN,"°"));text(metricLabels[3],TextUtil::number(weather.valid?weather.uv:NAN));
 }
}
void clicked(lv_event_t *){
 if(compact()||Touch::swiped())return;lv_indev_t *input=lv_indev_get_act();lv_point_t point={160,120};if(input)lv_indev_get_point(input,&point);
 if(point.x<20)return;
 Config next=config;next.seconds=!next.seconds;String error;
 if(!DashboardClockConfig::save(next,error)){notifyUser(error,true);return;}
 config=next;showSeconds=config.seconds;revealSeconds=showSeconds;layout(true);refreshRequested=true;refresh();lastInputAt=millis();
}
void deleted(lv_event_t *){
 cancel();releaseRaster();view=stage=effects=icon=nullptr;dayLabel=dateLabel=monthLabel=lunarLabel=yearLabel=festivalLabel=nullptr;conditionLabel=cityLabel=temperatureLabel=rangeLabel=metricsLabel=nullptr;
 temperatureUnit=lowLabel=metricIcons=nullptr;for(auto &label:metricLabels)label=nullptr;
 for(auto &c:cells)c=Cell{};initialized=false;timeWasValid=false;
}
void tick(lv_timer_t *){
 refresh();uint32_t now=lv_tick_get();
 if(layoutMoving){float t=float(now-layoutAt)/LayoutMs;if(t>=1){t=1;layoutMoving=false;}t=layoutEase(t);
  for(auto &c:cells)apply(c,interpolate(c.from,c.to,t));for(int j=0;j<2;j++)separators[j]=interpolate(separatorsFrom[j],separatorsTo[j],t);lv_obj_invalidate(stage);
 }
 for(auto &c:cells){
  if(c.moving){uint32_t elapsed=now-c.started;if(elapsed>=c.delay+RollMs)settle(c);else if(elapsed>=c.delay){int offset=(int)lroundf(glyphH*bezier(float(elapsed-c.delay)/RollMs));if(offset!=c.offset){c.offset=offset;paint(c);}}}
  if(c.tilting){uint32_t elapsed=now-c.tiltedAt;int angle=elapsed>=TiltMs?c.angleTo:c.angleFrom+(int)((c.angleTo-c.angleFrom)*(int)elapsed/(int)TiltMs);if(angle!=c.angle){c.angle=angle;if(c.image)lv_img_set_angle(c.image,angle);}if(elapsed>=TiltMs)c.tilting=false;}
 }
 bool animatedIcon=weatherKind==2||weatherKind>=4||(weatherKind==1&&weather.day);
 if(icon&&animatedIcon&&now-lastIconAt>=100){lastIconAt=now;lv_obj_invalidate(icon);
#ifdef GIFT_HOST_TEST
  ++iconInvalidations;
#endif
 }
 if(effects&&now-lastEffectAt>=50){
  float elapsed=LV_MIN(now-lastEffectAt,100u)/1000.f;lastEffectAt=now;
  if(particlesActive()){for(auto &p:particles){p.y+=p.speed*elapsed*(weatherKind==5?.25f:1.0f);if(weatherKind==5)p.x+=sinf(now*.001f+p.phase)*elapsed*5;else p.x-=elapsed*20;if(p.y>240){p.y=-12;p.x=(float)(randomNext()%320);}if(p.x<0)p.x+=320;if(p.x>=320)p.x-=320;}lv_obj_invalidate(effects);}
  bool wasFlashing=flashing;
  if(weatherKind==6&&config.thunder&&!minimal()){
   if(!flashing&&(int32_t)(now-nextFlash)>=0){flashing=true;flashAt=now;nextFlash=now+3000+randomNext()%7001;}
   if(flashing&&now-flashAt>130)flashing=false;
  }else flashing=false;
  if(wasFlashing||flashing)lv_obj_invalidate(effects);
 }
}
}
bool active(){return view!=nullptr;}
void cancel(){if(timer){lv_timer_del(timer);timer=nullptr;}layoutMoving=false;for(auto &c:cells)c.moving=c.tilting=false;}
void build(lv_obj_t *parent,const char *preset){
 mode=!strcmp(preset,"minimal")?"minimal":!strcmp(preset,"compact")?"compact":"dashboard";config=DashboardClockConfig::load();showSeconds=config.seconds&&!compact();
 glyphW=compact()?44:76;glyphH=compact()?220:minimal()?124:112;
 for(auto &c:cells)c=Cell{};for(auto &s:separators)s=Geometry{};
 view=box(parent,0,0,320,240,true);lv_obj_add_event_cb(view,deleted,LV_EVENT_DELETE,nullptr);
 effects=box(view,0,0,320,240);lv_obj_add_event_cb(effects,drawEffects,LV_EVENT_DRAW_MAIN,nullptr);
 stage=box(view,0,0,compact()?192:320,240);lv_obj_add_event_cb(stage,drawStage,LV_EVENT_DRAW_POST,nullptr);
 loadMasks();
 if(memory)for(auto &c:cells){c.image=lv_img_create(stage);lv_obj_clear_flag(c.image,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_img_set_src(c.image,&c.descriptor);lv_img_set_pivot(c.image,canvasW/2,canvasH/2);lv_img_set_antialias(c.image,true);}
 if(!compact()){
  auto *hit=box(view,20,minimal()?10:59,300,minimal()?220:119);lv_obj_add_flag(hit,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(hit,clicked,LV_EVENT_CLICKED,nullptr);
 }
 if(!minimal()){
  if(compact()){
   // The right 120px column has the same 8px inset on both sides. Calendar,
   // lunar notes, weather and measurements each own a separate vertical band.
   dayLabel=label(view,200,8,56,&fd_sans_bold_39);
   monthLabel=label(view,264,15,48,&fd_sans_bold_14_ui);dateLabel=label(view,264,37,48,&fd_sans_bold_14_ui);centered(monthLabel);centered(dateLabel);
   lunarLabel=label(view,200,64,112,&fd_sans_bold_14_ui,0xe1e5ef);centered(lunarLabel);lv_obj_set_height(lunarLabel,18);
   yearLabel=label(view,200,86,112,&fd_sans_bold_12,0xb8c4d6);centered(yearLabel);
   festivalLabel=label(view,200,105,112,&fd_sans_bold_12,0xd7e4f8);centered(festivalLabel);lv_label_set_long_mode(festivalLabel,LV_LABEL_LONG_WRAP);lv_obj_set_height(festivalLabel,30);
   icon=box(view,200,139,28,28);conditionLabel=label(view,234,137,78,&fd_sans_bold_14_ui);lv_label_set_long_mode(conditionLabel,LV_LABEL_LONG_WRAP);lv_obj_set_height(conditionLabel,30);
   cityLabel=label(view,200,170,112,&fd_sans_bold_12,0xb8c4d6);centered(cityLabel);lv_label_set_long_mode(cityLabel,LV_LABEL_LONG_WRAP);lv_obj_set_height(cityLabel,26);
   temperatureLabel=label(view,200,199,66,&fd_sans_bold_26);
   metricLabels[0]=label(view,269,207,43,&fd_sans_bold_16);lv_obj_set_style_text_align(metricLabels[0],LV_TEXT_ALIGN_RIGHT,0);
   rangeLabel=label(view,200,207,112,&fd_sans_bold_12,0x8c9bad);centered(rangeLabel);
  }else{
   dayLabel=label(view,26,10,50,&fd_sans_bold_39);dateLabel=label(view,82,13,108,&fd_sans_bold_16);monthLabel=label(view,82,35,108,&fd_sans_bold_12,0xd1d7e2);
   lunarLabel=label(view,202,13,110,&fd_sans_bold_16,0xe1e4eb);lv_label_set_long_mode(lunarLabel,LV_LABEL_LONG_SCROLL_CIRCULAR);
   yearLabel=label(view,202,36,110,&fd_sans_bold_12,0xd1d7e2);lv_label_set_long_mode(yearLabel,LV_LABEL_LONG_SCROLL_CIRCULAR);
   icon=box(view,6,193,32,32);conditionLabel=label(view,43,190,72,&fd_sans_bold_16);lv_label_set_long_mode(conditionLabel,LV_LABEL_LONG_SCROLL_CIRCULAR);cityLabel=label(view,43,214,72,&fd_sans_bold_12,0xd1d7e2);lv_label_set_long_mode(cityLabel,LV_LABEL_LONG_SCROLL_CIRCULAR);
   temperatureLabel=label(view,118,185,53,&fd_sans_bold_39);temperatureUnit=label(view,170,193,20,&fd_sans_bold_12,0xe7e8ed);
   rangeLabel=label(view,194,193,31,&fd_sans_bold_12,0xf2bdc4);lowLabel=label(view,194,217,31,&fd_sans_bold_12,0xaecdf0);
   metricIcons=box(view,227,193,57,43);lv_obj_add_event_cb(metricIcons,drawMetricIcons,LV_EVENT_DRAW_MAIN,nullptr);
   for(int i=0;i<4;i++)metricLabels[i]=label(view,239+(i%2)*44,193+(i/2)*27,35,&fd_sans_bold_12,0xf0f1f5);
  }
  lv_obj_add_event_cb(icon,drawIcon,LV_EVENT_DRAW_MAIN,nullptr);
 }
 weatherKind=classify();iconDay=weather.day;for(auto &p:particles){p.x=randomNext()%320;p.y=randomNext()%240;p.speed=65+randomNext()%60;p.phase=randomNext()%628*.01f;}
#ifdef GIFT_HOST_TEST
 memset(paintCounts,0,sizeof(paintCounts));iconInvalidations=0;
#endif
 lastEpoch=-1;initialized=false;timeWasValid=false;showLeading=true;refreshRequested=false;revealSeconds=false;layout(false);layoutMoving=false;lastIconAt=lastEffectAt=lv_tick_get();nextFlash=lv_tick_get()+3000;flashing=false;lastRevision=GiftData::revision();refresh();timer=lv_timer_create(tick,20,nullptr);
}
void refresh(){
 if(!view)return;
 int64_t epoch=ClockSource::now();bool valid=DeskLogic::timeValid(epoch);auto date=DeskLogic::beijingDate(epoch);
 if(initialized&&epoch==lastEpoch&&GiftData::revision()==lastRevision&&!refreshRequested)return;
 if(initialized&&GiftData::revision()!=lastRevision&&!sameConfig(config,DashboardClockConfig::load())){
  // Settings edited elsewhere rebuild once; time-click changes config locally
  // before refresh(), preserving its animated 500ms geometry transition.
  renderPending=true;lastRevision=GiftData::revision();return;
 }
 bool jumped=initialized&&lastEpoch>=0&&(epoch<lastEpoch||epoch-lastEpoch>1);
 bool snap=!initialized||!valid||!timeWasValid||jumped;
 int hour=date.hour;if(!config.hour24){hour%=12;if(!hour)hour=12;}
 int values[]={hour/10,hour%10,date.minute/10,date.minute%10,date.second/10,date.second%10};
 bool leading=config.hour24||!valid||values[0]!=0,revealLeading=leading&&!showLeading;if(leading!=showLeading){showLeading=leading;layout(!snap);}
 if(valid)snprintf(targetTime,sizeof(targetTime),showSeconds?"%02d:%02d:%02d":"%02d:%02d",hour,date.minute,date.second);else snprintf(targetTime,sizeof(targetTime),showSeconds?"--:--:--":"--:--");
 for(unsigned i=0;i<6;i++){
  auto &c=cells[i];int target=valid?values[i]:10;
  bool visible=(i<4||showSeconds)&&(i||showLeading);
  if(!visible){c.shown=c.target=target;c.offset=0;c.moving=c.tilting=false;continue;}
  if(snap||!memory||(i>=4&&revealSeconds)||(i==0&&revealLeading)){c.target=target;settle(c);rotateTo(c,true);}
  else if(target!=c.target){if(c.moving||c.offset)settle(c);c.target=target;c.started=lv_tick_get();c.delay=showSeconds?(5-i)*100:(7-i)*100;c.moving=true;rotateTo(c,false);}
  // Every visible ordinary/minimal digit changes its tilt once per second,
  // including HH:MM. Hidden seconds and upright compact digits stay untouched.
  else if(epoch!=lastEpoch&&effectiveTilt())rotateTo(c,false);
 }
 if(!initialized||valid!=timeWasValid||(epoch+28800)/86400!=(lastEpoch+28800)/86400)updateDate(date,valid);
 updateWeather();if(!memory&&(snap||epoch/60!=lastEpoch/60||showSeconds||refreshRequested))lv_obj_invalidate(stage);
 lastEpoch=epoch;initialized=true;timeWasValid=valid;lastRevision=GiftData::revision();refreshRequested=false;revealSeconds=false;
}
Status status(){Status result{};result.preset=mode;snprintf(result.time,sizeof(result.time),"%s",targetTime);result.seconds=showSeconds;result.tilt=effectiveTilt();result.rasterReady=memory!=nullptr;for(const auto &c:cells)if(c.moving)++result.movingDigits;return result;}
#ifdef GIFT_HOST_TEST
int digitShown(unsigned digit){return digit<6?cells[digit].shown:-1;}
int digitTarget(unsigned digit){return digit<6?cells[digit].target:-1;}
int digitAngle(unsigned digit){return digit<6?cells[digit].angle:0;}
int digitOffset(unsigned digit){return digit<6?cells[digit].offset:0;}
void debugFailRaster(bool fail){failRaster=fail;}
uint32_t debugPaintCount(unsigned digit){return digit<6?paintCounts[digit]:0;}
uint32_t debugIconInvalidations(){return iconInvalidations;}
bool debugLayoutMoving(){return layoutMoving;}
#endif
}
