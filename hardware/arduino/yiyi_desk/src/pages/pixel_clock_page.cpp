// Adapted from Eric Nam's Pixel Clock (MIT): 19x9 tiles, 3x5 digits,
// falling tiles and an idle snake. The board's existing clock is authoritative.
#include "mini_apps.h"
#include "../app_state.h"
#include "../clock_source.h"
#include "../ui_widgets.h"
#include "../saved_design/saved_resources.h"
#include "../gift_assets/gift_fonts.h"
#include "../touch.h"
#include "../../desk_logic.h"
#include <stdlib.h>
#include <stdio.h>

namespace PixelClockPage {
namespace {
constexpr int Rows=9, Cols=19, Step=16, GridX=8, GridY=47;
constexpr uint32_t Off=0x262930, On=0xf5f5f2;
const char *digits[]={"111101101101111","010110010010111","111001111100111",
 "111001111001111","101101111001001","111100111001111",
 "111100111101111","111001010010010","111101111101111","111101111001111"};
lv_obj_t *view=nullptr,*cells[Rows][Cols]={},*dateLabel=nullptr,*weatherLabel=nullptr;
lv_timer_t *timer=nullptr;
bool mask[Rows][Cols]={};
struct Point {int r,c;};
Point snake[3],target;
bool playing=false;
unsigned moves=0;
uint32_t lastMove=0,nextSnake=0;
int lastSecond=-1;
struct Tile {lv_obj_t *object=nullptr; int y=0; uint32_t start=0;};
Tile falling[6];
lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color) {
 auto *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,255,0);
 lv_obj_set_style_radius(o,1,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
void paint() {
 for(int r=0;r<Rows;r++)for(int c=0;c<Cols;c++) {
  uint32_t color=mask[r][c]?On:Off;
  if(playing) {
   if(r==target.r&&c==target.c)color=0xff565b;
   for(auto p:snake)if(p.r==r&&p.c==c)color=0x64ff64;
  }
  if(lv_obj_get_style_bg_color(cells[r][c],0).full!=lv_color_hex(color).full)
   lv_obj_set_style_bg_color(cells[r][c],lv_color_hex(color),0);
 }
}
void glyph(int value,int col) {
 for(int r=0;r<5;r++)for(int c=0;c<3;c++)
  mask[r+2][col+c]=value<0?r==2:digits[value][r*3+c]=='1';
}
void tileClick(lv_event_t *event) {
 if(Touch::swiped())return;
 lastInputAt=millis();
 auto *cell=lv_event_get_target(event);
 for(auto &tile:falling)if(!tile.object) {
  tile.y=lv_obj_get_y(cell);tile.start=lv_tick_get();
  tile.object=box(view,lv_obj_get_x(cell),tile.y,14,14,0x505050u+(unsigned)rand()%0xafafaf);
  break;
 }
}
void deleted(lv_event_t *) {
 cancel();view=nullptr;dateLabel=weatherLabel=nullptr;
 for(auto &row:cells)for(auto &cell:row)cell=nullptr;
 for(auto &tile:falling)tile=Tile{};
 playing=false;
}
bool occupied(Point p) {
 for(auto s:snake)if(p.r==s.r&&p.c==s.c)return true;
 return false;
}
void moveSnake() {
 Point head=snake[0],next=head;
 // A bounded search replaces the original unbounded/unchecked edge move.
 Point choices[]={{head.r,head.c+1},{head.r,head.c-1},{head.r+1,head.c},{head.r-1,head.c}};
 int best=1000;
 for(auto p:choices) {
  if(p.r<0||p.r>=Rows||p.c<0||p.c>=Cols||occupied(p))continue;
  int distance=abs(p.r-target.r)+abs(p.c-target.c);
  if(distance<best){best=distance;next=p;}
 }
 if(best==1000||++moves>Rows*Cols){playing=false;return;}
 snake[2]=snake[1];snake[1]=head;snake[0]=next;
 if(next.r==target.r&&next.c==target.c)playing=false;
}
void tick(lv_timer_t *) {
 uint32_t now=lv_tick_get();
 for(auto &tile:falling)if(tile.object) {
  uint32_t elapsed=now-tile.start;
  if(elapsed>=700){lv_obj_del(tile.object);tile=Tile{};}
  else lv_obj_set_y(tile.object,tile.y+(int)(240ull*elapsed*elapsed/(700*700)));
 }
 if(!playing&&(int32_t)(now-nextSnake)>=0)startSnake();
 if(playing&&now-lastMove>=180){lastMove=now;moveSnake();paint();}
 refresh();
}
}
bool active(){return view!=nullptr;}
bool snakeActive(){return playing;}
void startSnake() {
 if(!view||playing)return;
 snake[0]={rand()%Rows,2};snake[1]={snake[0].r,1};snake[2]={snake[0].r,0};
 // Put the apple at the opposite side, outside the four clock digits.
 target={rand()%Rows,Cols-1};playing=true;moves=0;lastMove=lv_tick_get();
 nextSnake=lv_tick_get()+18000+(unsigned)rand()%18000;paint();
}
void cancel(){if(timer){lv_timer_del(timer);timer=nullptr;}}
void build(lv_obj_t *parent) {
 view=box(parent,0,0,320,240,0x090a0d);
 lv_obj_add_event_cb(view,deleted,LV_EVENT_DELETE,nullptr);
 for(int r=0;r<Rows;r++)for(int c=0;c<Cols;c++) {
  auto *o=box(view,GridX+c*Step,GridY+r*Step,14,14,Off);cells[r][c]=o;
  lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(o,tileClick,LV_EVENT_CLICKED,nullptr);
 }
 dateLabel=Ui::label(view,"",8,201,304,&lv_font_montserrat_12,0xc3c7cf);
 lv_obj_set_style_text_align(dateLabel,LV_TEXT_ALIGN_CENTER,0);
 weatherLabel=Ui::label(view,"",8,221,304,&fd_sans_bold_12,0x8e96a3);
 lv_obj_set_style_text_align(weatherLabel,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(weatherLabel,LV_LABEL_LONG_DOT);
 lastSecond=-1;playing=false;nextSnake=lv_tick_get()+18000;
 for(auto &row:mask)for(auto &v:row)v=false;
 timer=lv_timer_create(tick,40,nullptr);refresh();
}
void refresh() {
 if(!view)return;
 int64_t epoch=ClockSource::now();auto d=DeskLogic::beijingDate(epoch);bool valid=DeskLogic::timeValid(epoch);
 int second=valid?d.second:-2;if(second==lastSecond)return;lastSecond=second;
 glyph(valid?d.hour/10:-1,1);glyph(valid?d.hour%10:-1,5);
 glyph(valid?d.minute/10:-1,11);glyph(valid?d.minute%10:-1,15);
 mask[3][9]=mask[5][9]=valid&&d.second%2==0;paint();
 char date[40];if(valid)snprintf(date,sizeof(date),"%04d-%02d-%02d     %02d:%02d:%02d",d.year,d.month,d.day,d.hour,d.minute,d.second);
 else snprintf(date,sizeof(date),"--:--  /  --:--:--");lv_label_set_text(dateLabel,date);
 String line=valid?String(""):String("等待联网校时");
 if(weather.valid){line=settings.city;if(!line.isEmpty())line+=" · ";line+=weather.condition;line+=" ";line+=String((double)weather.temperature,0);line+="°C";}
 else if(valid)line="点击方块 · 偶遇小蛇";
 lv_label_set_text(weatherLabel,line.c_str());
}
}
