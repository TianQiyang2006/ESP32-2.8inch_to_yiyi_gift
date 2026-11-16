// Review-only layouts rendered by the actual LVGL 8.4 software renderer.
// No production game code or firmware state is modified by this executable.
#include <lvgl.h>
#include "hardware/arduino/yiyi_desk/src/gift_assets/gift_fonts.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
extern "C" const lv_font_t fd_sans_bold_14;

namespace {
constexpr uint32_t Paper=0xF6F7F3, Ink=0x243C34, Muted=0x68776E, Green=0x397255,
  Light=0xE6EBE3, White=0xFFFFFF, Gold=0xD6AC58, Red=0xD66F5A;
const uint32_t colors[]={0xD66F5A,0xDCA84D,0x58966E,0x6297BB,0x9C7BBA,0xB87092};
lv_color_t pixels[320*240],buffer[320*40];
lv_obj_t *root;
lv_point_t pointer={0,0}; bool down=false;
unsigned labels=0,glyphs=0,controls=0,bounds=0,taps=0;
int lastCommand=0;
std::string output;
struct Control {lv_obj_t *o;int command;};
std::vector<Control> actionable;

void flush(lv_disp_drv_t *drv,const lv_area_t *a,lv_color_t *data){
  for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){
    if(x>=0&&x<320&&y>=0&&y<240)pixels[y*320+x]=*data; ++data;
  } lv_disp_flush_ready(drv);
}
void readPointer(lv_indev_drv_t*,lv_indev_data_t *data){data->point=pointer;data->state=down?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;}
void advance(unsigned ms){for(unsigned i=0;i<ms;i+=5){lv_tick_inc(5);lv_timer_handler();}lv_obj_update_layout(root);}
void clicked(lv_event_t *e){lastCommand=int(intptr_t(lv_event_get_user_data(e)));}

lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h,uint32_t c,int radius=0){
  auto *o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
  lv_obj_set_style_bg_color(o,lv_color_hex(c),0);lv_obj_set_style_bg_opa(o,255,0);
  lv_obj_set_style_radius(o,radius,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
  return o;
}
lv_obj_t *text(lv_obj_t *p,const char *s,int x,int y,int w,int h,const lv_font_t *font=&fd_sans_12,
               uint32_t c=Ink,lv_text_align_t align=LV_TEXT_ALIGN_LEFT){
  auto *o=lv_label_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
  lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(c),0);
  lv_obj_set_style_text_align(o,align,0);lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);lv_label_set_text(o,s);
  lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
void bind(lv_obj_t *o,int command){lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(o,clicked,LV_EVENT_CLICKED,(void*)intptr_t(command));actionable.push_back({o,command});}
lv_obj_t *button(lv_obj_t *p,const char *s,int x,int y,int w,int h,int command,bool primary=false){
  auto *o=box(p,x,y,w,h,primary?Green:Light,7);bind(o,command);
  text(o,s,2,(h-18)/2,w-4,18,&fd_sans_12,primary?White:Ink,LV_TEXT_ALIGN_CENTER);return o;
}
void border(lv_obj_t *o,uint32_t c,int width=1){lv_obj_set_style_border_color(o,lv_color_hex(c),0);lv_obj_set_style_border_width(o,width,0);}
void clear(){actionable.clear();lv_obj_clean(root);lv_obj_set_style_bg_color(root,lv_color_hex(Paper),0);lv_obj_set_style_bg_opa(root,255,0);}
void topbar(const char *title,const char *status=""){
  box(root,0,0,320,32,Paper);button(root,"返回",0,0,40,32,1);
  text(root,title,46,7,82,20,&fd_sans_12);
  text(root,status,132,7,142,20,&fd_sans_12,Muted,LV_TEXT_ALIGN_RIGHT);
  button(root,"暂停",280,0,40,32,2);
}

void rect(lv_draw_ctx_t *ctx,int x,int y,int w,int h,uint32_t color,int radius=0){
  lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=255;d.radius=radius;d.border_width=0;
  lv_area_t a={(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+h-1)};lv_draw_rect(ctx,&d,&a);
}
void line(lv_draw_ctx_t *ctx,int x1,int y1,int x2,int y2,uint32_t color,int width=2){
  lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=lv_color_hex(color);d.width=width;d.round_start=d.round_end=1;
  lv_point_t a={(lv_coord_t)x1,(lv_coord_t)y1},b={(lv_coord_t)x2,(lv_coord_t)y2};lv_draw_line(ctx,&d,&a,&b);
}
void triangle(lv_draw_ctx_t *ctx,int x,int y,int w,int h,uint32_t c){
  lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(c);d.bg_opa=255;
  lv_point_t a[3]={{(lv_coord_t)x,(lv_coord_t)(y+h)},{(lv_coord_t)(x+w/2),(lv_coord_t)y},{(lv_coord_t)(x+w),(lv_coord_t)(y+h)}};lv_draw_triangle(ctx,&d,a);
}
void cat(lv_draw_ctx_t *ctx,int x,int y,int scale=1){
  rect(ctx,x,y+8*scale,24*scale,20*scale,Ink,7*scale);
  triangle(ctx,x,y,10*scale,12*scale,Ink);triangle(ctx,x+14*scale,y,10*scale,12*scale,Ink);
  rect(ctx,x+5*scale,y+14*scale,4*scale,4*scale,0xB5D2A0,2*scale);
  rect(ctx,x+16*scale,y+14*scale,4*scale,4*scale,0xB5D2A0,2*scale);
  line(ctx,x+3*scale,y+28*scale,x,y+31*scale,Ink,3*scale);
  line(ctx,x+20*scale,y+28*scale,x+23*scale,y+31*scale,Ink,3*scale);
  line(ctx,x+24*scale,y+22*scale,x+29*scale,y+16*scale,Ink,3*scale);
}
void fruit(lv_draw_ctx_t *ctx,int x,int y,int radius,uint32_t c,bool bomb=false){
  rect(ctx,x-radius,y-radius,radius*2,radius*2,c,radius);
  if(bomb){line(ctx,x+3,y-radius,x+8,y-radius-8,Gold,3);line(ctx,x-5,y-5,x+5,y+5,White,2);line(ctx,x+5,y-5,x-5,y+5,White,2);}
  else {line(ctx,x,y-radius,x+3,y-radius-6,Green,3);rect(ctx,x+3,y-radius-7,8,4,Green,2);rect(ctx,x-radius+5,y-radius+5,4,7,0xFFF1DE,2);}
}
void drawDecor(lv_event_t *e){
  auto *ctx=lv_event_get_draw_ctx(e);lv_obj_t *o=lv_event_get_target(e);lv_area_t a;lv_obj_get_coords(o,&a);
  int k=int(intptr_t(lv_event_get_user_data(e))),x=a.x1,y=a.y1,w=lv_obj_get_width(o),h=lv_obj_get_height(o);
  if(k==1){
    rect(ctx,x,y,w,h,0xEEF2EB);rect(ctx,x,y+h-9,w,9,0xDDE5D4);
    cat(ctx,x+18,y+9);triangle(ctx,x+105,y+29,16,14,0x8DA88A);rect(ctx,x+84,y+17,7,7,Gold,4);
  }else if(k==2){
    for(int r=0;r<3;r++)for(int c=0;c<5;c++)rect(ctx,x+39+c*13,y+6+r*13,11,11,0xDDE5D4,2);
    rect(ctx,x+40,y+20,24,10,Gold,2);rect(ctx,x+67,y+6,24,24,0x9DB8AC,3);
    rect(ctx,x+94,y+7,10,35,0x99B4C8,3);line(ctx,x+109,y+25,x+122,y+25,Green,2);
    line(ctx,x+117,y+21,x+122,y+25,Green,2);line(ctx,x+117,y+29,x+122,y+25,Green,2);
  }else if(k==3){
    fruit(ctx,x+48,y+31,12,Red);fruit(ctx,x+89,y+22,13,0xE1AE50);line(ctx,x+25,y+46,x+114,y+7,Green,2);
  }else if(k==4){
    for(int i=0;i<4;i++){rect(ctx,x+32+i*23,y+16,18,18,colors[i],9);rect(ctx,x+37+i*23,y+42,7,7,i<2?Green:0xD8DED4,4);}
  }else if(k==5){
    // One static playfield draw object: no offscreen image or framebuffer.
    rect(ctx,x,y,w,h,0xF1F4ED);
    rect(ctx,x+210,y+38,36,9,White,5);rect(ctx,x+220,y+32,17,12,White,6);
    rect(ctx,x+28,y+28,29,8,White,4);triangle(ctx,x+152,y+89,86,54,0xE2EADC);
    triangle(ctx,x+225,y+105,92,38,0xE7EDDF);
    rect(ctx,x,y+142,w,18,0xDCE6D3);line(ctx,x,y+141,x+w,y+141,0x8BA585,2);
    cat(ctx,x+54,y+110);rect(ctx,x+185,y+117,18,26,0x7D9A78,3);
    rect(ctx,x+242,y+112,25,31,0x9AAF8E,3);
    rect(ctx,x+140,y+95,9,9,Gold,5);rect(ctx,x+221,y+85,9,9,Gold,5);
  }else if(k==6){
    fruit(ctx,x+64,y+107,16,Red);fruit(ctx,x+160,y+72,17,0xE1AE50);fruit(ctx,x+256,y+129,18,0x7EA774);
    fruit(ctx,x+226,y+59,14,Ink,true);
    line(ctx,x+70,y+166,x+104,y+135,0x9AB9A3,2);line(ctx,x+104,y+135,x+150,y+90,Green,3);
    line(ctx,x+150,y+90,x+173,y+64,Green,2);
    rect(ctx,x+128,y+88,4,4,Gold,2);rect(ctx,x+147,y+112,3,3,Gold,2);rect(ctx,x+180,y+76,4,4,Red,2);
  }
}
lv_obj_t *decor(lv_obj_t *p,int x,int y,int w,int h,int kind,bool clickable=false,int command=0){
  auto *o=box(p,x,y,w,h,Paper);lv_obj_add_event_cb(o,drawDecor,LV_EVENT_DRAW_MAIN,(void*)intptr_t(kind));
  if(clickable)bind(o,command);return o;
}

void lobby(){
  clear();button(root,"返回",0,0,40,36,1);text(root,"小黑游戏厅",52,7,190,26,&fd_sans_bold_16);
  const char *names[]={"小黑快跑","拼块逃脱","切水果","猜色密码"};
  const char *tips[]={"点按跳跃","拖动拼块","划动切开","点选推理"};
  for(int i=0;i<4;i++){
    int x=i%2?164:8,y=i/2?140:42;auto *card=box(root,x,y,148,90,White,9);bind(card,10+i);
    decor(card,2,2,144,50,i+1);text(card,names[i],8,54,132,22,&fd_sans_bold_14,Ink,LV_TEXT_ALIGN_CENTER);
    text(card,tips[i],8,76,132,13,&fd_sans_10,Muted,LV_TEXT_ALIGN_CENTER);
  }
}
void runner(){
  clear();topbar("小黑快跑","分数 128");decor(root,0,32,320,160,5,true,30);
  button(root,"点击跳跃",0,192,320,48,31,true);
}
void slide(bool solved=false){
  clear();topbar("拼块逃脱","");auto *board=box(root,8,36,192,192,0xDFE6DC,7);bind(board,40);
  for(int r=0;r<6;r++)for(int c=0;c<6;c++)box(board,c*32+1,r*32+1,30,30,0xEDF1E8,3);
  struct Piece {int x,y,w,h;uint32_t c;const char *s;};
  Piece pieces[]={{0,2,2,1,Gold,"目标"},{2,1,2,2,0x9DB8AC,""},{4,0,1,3,0x91ACC3,""},
    {2,4,2,1,0xB2A7C7,""},{0,4,1,2,0xC4AD98,""},{5,4,1,2,0xB6BF8D,""}};
  if(solved){pieces[0].x=4;pieces[1].y=3;pieces[2].y=3;pieces[3].y=5;}
  for(auto &p:pieces){auto *b=box(board,p.x*32+2,p.y*32+2,p.w*32-4,p.h*32-4,p.c,5);
    if(*p.s)text(b,p.s,0,5,p.w*32-4,18,&fd_sans_12,Ink,LV_TEXT_ALIGN_CENTER);}
  text(root,"出口",202,101,38,18,&fd_sans_10,Green);
  text(root,"关卡 01",212,40,100,24,&fd_sans_bold_14);text(root,solved?"步数 04":"步数 00",212,70,100,20);
  button(root,"撤销",212,118,100,40,41);button(root,"提示一步",212,166,100,40,42);
  text(root,"拖动后吸附",212,218,100,18,&fd_sans_12,Muted);
}
void fruits(){clear();topbar("切水果","128 分   生命 3");decor(root,0,32,320,208,6,true,50);}
void disc(lv_obj_t *p,int value,int x,int y,int size,bool interactive=false,int command=0,bool selected=false){
  auto *b=box(p,x,y,size,size,value?colors[value-1]:Light,size/2);
  if(selected)border(b,Ink,2);if(interactive)bind(b,command);
  if(value){char s[4];snprintf(s,sizeof s,"%d",value);text(b,s,0,(size-18)/2,size,18,&fd_sans_12,Ink,LV_TEXT_ALIGN_CENTER);}
}
void code(bool empty=false,bool complete=false){
  clear();topbar("猜色密码",empty?"第 1 / 12 次":"第 4 / 12 次");
  auto *history=box(root,8,36,304,112,Paper);bind(history,60);
  if(empty){text(history,"四个位置，六种颜色",12,30,280,22,&fd_sans_14,Muted,LV_TEXT_ALIGN_CENTER);
    text(history,"颜色可重复；猜后查看反馈",12,58,280,20,&fd_sans_12,Muted,LV_TEXT_ALIGN_CENTER);}
  else{
    // All example feedback is consistent with the same secret [2,4,3,6].
    int values[3][4]={{1,1,3,4},{2,5,6,3},{2,4,1,6}};const char *feedback[]={"正位 1  同色 1","正位 1  同色 2","正位 3  同色 0"};
    for(int r=0;r<3;r++){
      auto *row=box(history,0,r*38,304,34,White,5);char n[4];snprintf(n,sizeof n,"%02d",r+1);
      text(row,n,6,8,20,18,&fd_sans_10,Muted);
      for(int c=0;c<4;c++)disc(row,values[r][c],34+c*34,5,24);
      text(row,feedback[r],182,8,118,18,&fd_sans_12,Muted);
    }
  }
  box(root,8,154,304,44,White,6);
  for(int i=0;i<4;i++)disc(root,empty?0:i==0?2:i==1?4:i==2?3:complete?6:0,28+i*42,158,36,true,70+i,i==(empty?0:3));
  auto *submit=button(root,"提交",220,156,92,40,74,complete);
  if(!complete){
    lv_obj_set_style_bg_color(submit,lv_color_hex(Light),0);
    lv_obj_clear_flag(submit,LV_OBJ_FLAG_CLICKABLE);actionable.pop_back();
    assert(!lv_obj_has_flag(submit,LV_OBJ_FLAG_CLICKABLE));
  }
  for(int i=0;i<6;i++){
    auto *b=box(root,4+i*52,202,48,36,colors[i],6);bind(b,80+i);
    char s[3];snprintf(s,sizeof s,"%d",i+1);text(b,s,0,9,48,20,&fd_sans_14,Ink,LV_TEXT_ALIGN_CENTER);
  }
}
void overlay(const char *type){
  const bool ready=!strcmp(type,"ready"),restart=!strcmp(type,"restart"),win=!strcmp(type,"win"),over=!strcmp(type,"gameover");
  if(win)slide(true);else runner();actionable.clear();auto *shade=box(root,0,0,320,240,Ink);lv_obj_set_style_bg_opa(shade,165,0);lv_obj_add_flag(shade,LV_OBJ_FLAG_CLICKABLE);
  auto *card=box(shade,20,20,280,200,Paper,10);
  const char *title=ready?"小黑快跑":restart?"重新开始？":win?"成功逃脱":over?"本局结束":"已暂停";
  const char *detail=ready?"点下方大按钮跳跃，避开障碍":restart?"本局进度将清除":win?"第 01 关完成 · 共 4 步":over?"本局 128 分 · 最高 246 分":"小黑快跑 · 分数 128";
  text(card,title,12,11,256,28,&fd_sans_bold_16,Ink,LV_TEXT_ALIGN_CENTER);
  text(card,detail,12,40,256,22,&fd_sans_12,Muted,LV_TEXT_ALIGN_CENTER);
  button(card,restart?"确认重开":ready?"开始游戏":win?"下一关":over?"再玩一次":"继续游戏",16,64,248,40,90,true);
  if(ready)button(card,"返回列表",16,112,248,36,92);
  else{
    button(card,restart?"取消":"重新开始",16,112,120,36,91);
    button(card,"返回列表",144,112,120,36,92);
  }
  button(card,"玩法与操作",16,156,248,32,93);
}

lv_obj_t *hit(lv_obj_t *p,int x,int y){
  lv_area_t a;lv_obj_get_coords(p,&a);if(x<a.x1||x>a.x2||y<a.y1||y>a.y2)return nullptr;
  for(int i=int(lv_obj_get_child_cnt(p))-1;i>=0;i--)if(auto *o=hit(lv_obj_get_child(p,i),x,y))return o;
  return lv_obj_has_flag(p,LV_OBJ_FLAG_CLICKABLE)?p:nullptr;
}
void check(lv_obj_t *o){
  lv_area_t a;lv_obj_get_coords(o,&a);++bounds;
  if(a.x1<0||a.y1<0||a.x2>=320||a.y2>=240){std::cerr<<"Object out of screen "<<a.x1<<","<<a.y1<<".."<<a.x2<<","<<a.y2<<"\n";assert(false);}
  if(lv_obj_has_flag(o,LV_OBJ_FLAG_CLICKABLE))++controls;
  if(lv_obj_check_type(o,&lv_label_class)){
    ++labels;const char *s=lv_label_get_text(o);auto *f=lv_obj_get_style_text_font(o,0);
    uint32_t pos=0;while(s[pos]){uint32_t cp=_lv_txt_encoded_next(s,&pos);if(cp<=32)continue;++glyphs;lv_font_glyph_dsc_t d{};
      if(!lv_font_get_glyph_dsc(f,&d,cp,0)){std::cerr<<"Missing glyph "<<cp<<" in "<<s<<"\n";assert(false);}}
    lv_point_t need{};lv_txt_get_size(&need,s,f,0,0,lv_obj_get_width(o),LV_TEXT_FLAG_NONE);
    if(need.x>lv_obj_get_width(o)+1||need.y>lv_obj_get_height(o)+1){std::cerr<<"Clipped "<<s<<" needs "<<need.x<<"x"<<need.y<<" has "<<lv_obj_get_width(o)<<"x"<<lv_obj_get_height(o)<<"\n";assert(false);}
  }
  for(unsigned i=0;i<lv_obj_get_child_cnt(o);i++)check(lv_obj_get_child(o,i));
}
void checkInput(){
  for(auto &c:actionable){
    lv_area_t a;lv_obj_get_coords(c.o,&a);int w=lv_obj_get_width(c.o),h=lv_obj_get_height(c.o);
    // Gameplay discs/gesture zones and all buttons have >=32 px physical height.
    assert(w>=32&&h>=32);int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;
    if(hit(root,x,y)!=c.o){std::cerr<<"Blocked control "<<c.command<<"\n";assert(false);}
    pointer={(lv_coord_t)x,(lv_coord_t)y};down=true;advance(45);lastCommand=0;down=false;advance(65);
    if(lastCommand!=c.command){std::cerr<<"Pointer event failed "<<c.command<<" got "<<lastCommand<<"\n";assert(false);}++taps;
  }
}
void save(const char *name){
  lv_obj_update_layout(root);check(root);checkInput();lv_refr_now(nullptr);
  std::ofstream f(output+"/"+name+".rgb565",std::ios::binary);f.write((char*)pixels,sizeof pixels);assert(f.good());
  std::cout<<"frame "<<name<<"\n";
}
}
int main(int argc,char **argv){
  std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);if(argc!=2)return 2;output=argv[1];
  std::filesystem::create_directories(output);lv_init();static lv_disp_draw_buf_t draw;lv_disp_draw_buf_init(&draw,buffer,nullptr,320*40);
  static lv_disp_drv_t drv;lv_disp_drv_init(&drv);drv.hor_res=320;drv.ver_res=240;drv.draw_buf=&draw;drv.flush_cb=flush;lv_disp_drv_register(&drv);
  static lv_indev_drv_t in;lv_indev_drv_init(&in);in.type=LV_INDEV_TYPE_POINTER;in.read_cb=readPointer;lv_indev_drv_register(&in);
  root=lv_scr_act();lv_obj_remove_style_all(root);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
  lobby();save("lobby");runner();save("runner");slide();save("slide");fruits();save("fruit");code();save("code");code(true);save("code-empty");code(false,true);save("code-full");
  for(auto *s:{"ready","pause","restart","gameover","win"}){overlay(s);save(s);}
  std::cout<<"QA labels="<<labels<<" glyphs="<<glyphs<<" controls="<<controls<<" bounds="<<bounds<<" taps="<<taps<<"\n";
  return 0;
}
