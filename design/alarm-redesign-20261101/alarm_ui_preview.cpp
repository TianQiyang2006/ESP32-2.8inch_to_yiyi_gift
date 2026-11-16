// Native LVGL layout prototype for review. Not connected to alarm scheduling.
#include "lvgl.h"
#include "hardware/arduino/yiyi_desk/src/gift_assets/gift_fonts.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_resources.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int64_t HostEpoch = 1793491200;
namespace {
lv_color_t pixels[320 * 240], buffer[320 * 40];
lv_obj_t *screen = nullptr;
std::string out;
int clipped = 0;
constexpr uint32_t Bg=0xF7F7EE, Ink=0x213F32, Muted=0x51685A;
constexpr uint32_t Light=0xE7EEDD, Green=0x315C43, White=0xFFFFFF;
const lv_font_t *F14=saved_gift_font_bold_14_0;
const lv_font_t *F16=&fd_sans_bold_16;

void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *data) {
    for(int y=area->y1;y<=area->y2;++y)
        for(int x=area->x1;x<=area->x2;++x) {
            if(x>=0 && x<320 && y>=0 && y<240) pixels[y*320+x]=*data;
            ++data;
        }
    lv_disp_flush_ready(drv);
}
lv_obj_t *box(int x,int y,int w,int h,uint32_t color,int radius=10) {
    auto *obj=lv_obj_create(screen);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_radius(obj,radius,0);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
lv_obj_t *text(const char *value,int x,int y,int w,const lv_font_t *font=nullptr,
               uint32_t color=Ink,lv_text_align_t align=LV_TEXT_ALIGN_LEFT) {
    if(!font)font=F14;
    auto *obj=lv_label_create(screen);lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,font->line_height+2);
    lv_obj_set_style_text_font(obj,font,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_obj_set_style_text_align(obj,align,0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj,value);
    lv_point_t measured{};
    lv_txt_get_size(&measured,value,font,0,0,320,LV_TEXT_FLAG_EXPAND);
    if(measured.x>w) { std::cout<<"CLIPPED: "<<value<<" "<<measured.x<<" > "<<w<<"\n";++clipped; }
    return obj;
}
void button(const char *value,int x,int y,int w,int h,bool primary=false) {
    box(x,y,w,h,primary?Green:Light,8);
    text(value,x+4,y+(h-F16->line_height)/2,w-8,F16,primary?White:Ink,LV_TEXT_ALIGN_CENTER);
}
void back() {
    static lv_point_t points[]={{7,0},{0,7},{7,14}};
    auto *line=lv_line_create(screen);lv_line_set_points(line,points,3);
    lv_obj_set_pos(line,14,11);
    lv_obj_set_style_line_color(line,lv_color_hex(Ink),0);
    lv_obj_set_style_line_width(line,3,0);
    lv_obj_set_style_line_rounded(line,true,0);
}
void begin(const char *title,bool hasBack=true) {
    lv_obj_clean(screen);lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen,320,240);
    lv_obj_set_style_bg_color(screen,lv_color_hex(Bg),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    if(hasBack)back();
    text(title,hasBack?43:12,9,170,F16);
}
void save(const char *name) {
    lv_obj_update_layout(screen);lv_refr_now(nullptr);
    std::ofstream file(out+"/"+name+".rgb565",std::ios::binary);
    file.write(reinterpret_cast<char *>(pixels),sizeof(pixels));
    std::cout<<"Preview "<<name<<"\n";
}
void toggle(int x,int y,bool enabled) {
    // The enclosing row provides a 48x48 touch target in the implementation.
    box(x,y,48,26,enabled?Green:0xCBD3C7,13);
    box(x+(enabled?24:2),y+2,22,22,White,11);
}
void list() {
    begin("闹钟");button("+ 新建",228,2,80,36);
    text("已校时 · 下次明天07:30",14,44,294,F14,Green);
    box(12,68,296,66,White);
    text("07:30",24,69,168,&fd_mont_sb_34);
    text("工作日 · 经典滴滴",24,113,220,F14,Muted);
    toggle(248,86,true);
    box(12,142,296,66,White);
    text("09:00",24,143,168,&fd_mont_sb_34);
    text("仅一次 · 温柔钟声",24,187,220,F14,Muted);
    toggle(248,160,false);
    text("点击编辑 · 上滑查看更多 · 最多5组",12,218,296,F14,Muted,LV_TEXT_ALIGN_CENTER);
    save("01-alarm-list");
}
void timePicker() {
    begin("设置时间");text("1/2",260,11,48,F14,Muted,LV_TEXT_ALIGN_RIGHT);
    text("小时",64,36,80,F14,Muted,LV_TEXT_ALIGN_CENTER);
    text("分钟",176,36,80,F14,Muted,LV_TEXT_ALIGN_CENTER);
    text("06",64,57,80,&fd_sans_bold_22,Muted,LV_TEXT_ALIGN_CENTER);
    text("29",176,57,80,&fd_sans_bold_22,Muted,LV_TEXT_ALIGN_CENTER);
    box(46,90,228,58,Light,12);
    text("07",60,88,88,&lv_font_montserrat_48,Ink,LV_TEXT_ALIGN_CENTER);
    text(":",144,88,32,&lv_font_montserrat_48,Ink,LV_TEXT_ALIGN_CENTER);
    text("30",172,88,88,&lv_font_montserrat_48,Ink,LV_TEXT_ALIGN_CENTER);
    text("08",64,153,80,&fd_sans_bold_22,Muted,LV_TEXT_ALIGN_CENTER);
    text("31",176,153,80,&fd_sans_bold_22,Muted,LV_TEXT_ALIGN_CENTER);
    text("精确到分钟 · 24小时制",12,181,296,F14,Muted,LV_TEXT_ALIGN_CENTER);
    button("取消",12,198,96,40);button("下一步",116,198,192,40,true);
    save("02-time-picker");
}
void settings() {
    begin("提醒设置");text("07:30",238,10,70,F16,Green,LV_TEXT_ALIGN_RIGHT);
    const char *names[]={"重复","铃声","闹铃音量","贪睡时长"};
    const char *values[]={"工作日 >","经典滴滴 >","60% >","10分钟 >"};
    for(int i=0;i<4;++i) {
        int y=39+i*38;
        box(12,y,296,35,White,6);
        text(names[i],23,y+7,112,F16);
        text(values[i],138,y+7,157,F16,Green,LV_TEXT_ALIGN_RIGHT);
    }
    button("取消",12,198,96,40);button("保存并开启",116,198,192,40,true);
    save("03-alarm-settings");
}
void ringtones() {
    begin("选择铃声");
    const char *names[]={"经典滴滴","温柔钟声","渐快提醒"};
    const char *details[]={"清晰双音 · 默认","柔和叮咚","短鸣间隔逐渐缩短"};
    for(int i=0;i<3;++i) {
        int y=42+i*51;
        box(12,y,296,47,i==0?Light:White,8);
        auto *radio=box(23,y+15,16,16,i==0?Green:0xCCD5C8,8);
        (void)radio;
        if(i==0)box(28,y+20,6,6,White,3);
        text(names[i],49,y+2,192,F16);
        text(details[i],49,y+26,192,F14,Muted);
        button("试听",248,y+5,52,36);
    }
    button("取消",12,198,96,40);button("使用此铃声",116,198,192,40,true);
    save("04-ringtones");
}
void ringing() {
    begin("",false);
    text("闹钟响了",12,15,296,F16,Ink,LV_TEXT_ALIGN_CENTER);
    box(12,44,296,132,White,14);
    text("起床提醒",24,48,272,F16,Muted,LV_TEXT_ALIGN_CENTER);
    text("07:30",24,71,272,&lv_font_montserrat_48,Ink,LV_TEXT_ALIGN_CENTER);
    text("11月02日 周一 · 经典滴滴",24,133,272,F14,Green,LV_TEXT_ALIGN_CENTER);
    text("正在响铃",24,154,272,F14,Muted,LV_TEXT_ALIGN_CENTER);
    button("稍后10分钟",12,190,148,46);
    button("停止",168,190,140,46,true);
    save("05-ringing");
}
}
int main(int argc,char **argv) {
    if(argc<2)return 2;
    out=argv[1];std::filesystem::create_directories(out);
    lv_init();static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,nullptr,320*40);
    static lv_disp_drv_t drv;lv_disp_drv_init(&drv);
    drv.hor_res=320;drv.ver_res=240;drv.draw_buf=&draw;drv.flush_cb=flush;
    lv_disp_drv_register(&drv);screen=lv_scr_act();
    list();timePicker();settings();ringtones();ringing();
    if(clipped){std::cerr<<clipped<<" labels exceed their text bounds\n";return 3;}
    std::cout<<"PASS: all prototype labels fit their text bounds\n";
    return 0;
}
