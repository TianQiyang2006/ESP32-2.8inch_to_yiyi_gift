// Review-only native LVGL prototype. Reuse frozen alarm typography/layout helpers.
// No production firmware file is modified or invoked by these demo screens.
#define main archived_alarm_preview_main
#include "../alarm-redesign-20261101/alarm_ui_preview.cpp"
#undef main

namespace {
constexpr uint32_t FocusInk=0x663B29, FocusAccent=0xA24A32, FocusLight=0xFFF0E2;

void pomoButton(const char *value,int x,int y,int w,int h,bool primary=false,bool rest=false) {
    uint32_t fill=primary?(rest?Green:FocusAccent):Light;
    box(x,y,w,h,fill,8);
    text(value,x+4,y+(h-F16->line_height)/2,w-8,F16,primary?White:Ink,LV_TEXT_ALIGN_CENTER);
}
void pomoHeader() {
    begin("番茄时钟");
    pomoButton("设置",242,2,66,36);
}
void stage(const char *name,const char *round,const char *remaining,
           const char *detail,float progress,bool rest=false) {
    uint32_t ink=rest?Ink:FocusInk;
    uint32_t accent=rest?Green:FocusAccent;
    box(12,44,296,122,rest?Light:FocusLight,12);
    text(name,24,51,150,F16,ink);
    text(round,184,52,112,F14,ink,LV_TEXT_ALIGN_RIGHT);
    text(remaining,24,76,272,&lv_font_montserrat_48,ink,LV_TEXT_ALIGN_CENTER);
    text(detail,24,135,272,F14,Muted,LV_TEXT_ALIGN_CENTER);
    box(28,158,264,4,rest?0xCDD9C6:0xE7D4C1,2);
    if(progress>0)box(28,158,(int)(264*progress),4,accent,2);
}
void note(const char *value) {
    text(value,12,174,296,F14,Muted,LV_TEXT_ALIGN_CENTER);
}
void ready() {
    pomoHeader();
    stage("准备专注","第1/4轮","25:00","专注25分 · 短休5分",0);
    note("今日完成2个 · 50分钟");
    pomoButton("切换预设",12,198,112,40);
    pomoButton("开始专注",132,198,176,40,true);
    save("01-ready");
}
void focusing() {
    pomoHeader();
    stage("专注中","第1/4轮","17:42","已专注7分18秒",438.f/1500.f);
    note("返回桌面仍会继续计时");
    pomoButton("结束计时",12,198,96,40);
    pomoButton("暂停",116,198,192,40,true);
    save("02-focusing");
}
void paused() {
    pomoHeader();
    stage("已暂停","第1/4轮","17:42","暂停期间不计时",438.f/1500.f);
    note("继续后从17:42接着计时");
    pomoButton("结束计时",12,198,96,40);
    pomoButton("继续专注",116,198,192,40,true);
    save("03-paused");
}
void focusComplete() {
    pomoHeader();
    stage("专注完成","第1/4轮","05:00","接下来：短休息5分钟",0,true);
    note("本轮已记1个番茄 · 等待开始休息");
    pomoButton("跳过休息",12,198,128,40);
    pomoButton("开始休息",148,198,160,40,true,true);
    save("04-focus-complete");
}
void resting() {
    pomoHeader();
    stage("短休息中","第1/4轮","04:36","本轮已完成 · 放松一下",24.f/300.f,true);
    note("休息结束后进入第2/4轮");
    pomoButton("跳过休息",12,198,128,40);
    pomoButton("暂停休息",148,198,160,40,true,true);
    save("05-resting");
}
void durationSettings() {
    begin("时长与轮次");text("1/2",260,11,48,F14,Muted,LV_TEXT_ALIGN_RIGHT);
    const char *names[]={"专注时长","短休息","长休息","每组轮次"};
    const char *values[]={"25分钟 >","5分钟 >","15分钟 >","4轮 >"};
    for(int i=0;i<4;++i) {
        int y=39+i*38;
        box(12,y,296,35,White,6);
        text(names[i],23,y+7,112,F16);
        text(values[i],138,y+7,157,F16,FocusInk,LV_TEXT_ALIGN_RIGHT);
    }
    pomoButton("取消",12,198,96,40);
    pomoButton("下一步",116,198,192,40,true);
    save("06-duration-settings");
}
void reminderSettings() {
    begin("提醒与衔接");text("2/2",260,11,48,F14,Muted,LV_TEXT_ALIGN_RIGHT);
    const char *names[]={"提示铃声","提示音量","自动休息","自动专注"};
    const char *values[]={"温柔钟声 >","40% >"};
    for(int i=0;i<4;++i) {
        int y=39+i*38;
        box(12,y,296,35,White,6);
        text(names[i],23,y+7,112,F16);
        if(i<2)text(values[i],138,y+7,157,F16,FocusInk,LV_TEXT_ALIGN_RIGHT);
        else toggle(248,y+5,false);
    }
    pomoButton("取消",12,198,96,40);
    pomoButton("保存设置",116,198,192,40,true);
    save("07-reminder-settings");
}
void cycleComplete() {
    begin("本组已完成",false);
    stage("完整完成4轮专注","","4/4","累计专注100分钟",1,true);
    note("休息好后，再开启下一组");
    pomoButton("回到首页",12,198,128,40);
    pomoButton("开始新一组",148,198,160,40,true,true);
    save("08-cycle-complete");
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
    ready();focusing();paused();focusComplete();resting();
    durationSettings();reminderSettings();cycleComplete();
    if(clipped){std::cerr<<clipped<<" labels exceed their text bounds\n";return 3;}
    std::cout<<"PASS: all pomodoro prototype labels fit their text bounds\n";
    return 0;
}
