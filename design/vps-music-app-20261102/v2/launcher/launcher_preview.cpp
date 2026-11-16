// Desktop preview only. Uses the device's native LVGL font and icon assets.
#define main archived_alarm_preview_main
#include "design/alarm-redesign-20261101/alarm_ui_preview.cpp"
#undef main
#include "hardware/arduino/yiyi_desk/src/pages/assets/focus_app_icons.h"
#include "hardware/arduino/yiyi_desk/src/pages/assets/tetris_app_icon.h"

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    static uint8_t bgPixels[320*240*2], musicPixels[56*56*3];
    std::ifstream bgInput(argv[1],std::ios::binary);
    bgInput.read(reinterpret_cast<char *>(bgPixels),sizeof(bgPixels));
    if (bgInput.gcount()!=sizeof(bgPixels)) return 3;
    std::ifstream musicInput(argv[2],std::ios::binary);
    musicInput.read(reinterpret_cast<char *>(musicPixels),sizeof(musicPixels));
    if (musicInput.gcount()!=sizeof(musicPixels)) return 4;
    out=argv[3]; std::filesystem::create_directories(out);
    lv_init();
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,nullptr,320*40);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv); drv.hor_res=320; drv.ver_res=240;
    drv.draw_buf=&draw; drv.flush_cb=flush; lv_disp_drv_register(&drv);
    screen=lv_scr_act(); begin("",false);
    static lv_img_dsc_t background{}, music{};
    background.header.cf=LV_IMG_CF_TRUE_COLOR;
    background.header.w=320; background.header.h=240;
    background.data_size=sizeof(bgPixels); background.data=bgPixels;
    music.header.cf=LV_IMG_CF_TRUE_COLOR_ALPHA;
    music.header.w=56; music.header.h=56;
    music.data_size=sizeof(musicPixels); music.data=musicPixels;
    auto *base=lv_img_create(screen);
    lv_img_set_src(base,&background); lv_obj_set_pos(base,0,0);
    const lv_img_dsc_t *icons[]={&focus_app_alarm,&focus_app_pomodoro,&music,&tetris_app_icon};
    const char *names[]={"闹钟","番茄时钟","音乐","俄罗斯方块"};
    const int x[]={76,188,76,188}, y[]={24,24,133,133};
    for (int i=0;i<4;++i) {
        auto *icon=lv_img_create(screen);
        lv_img_set_src(icon,icons[i]); lv_obj_set_pos(icon,x[i],y[i]);
        text(names[i],x[i]-12,y[i]+63,80,F14,0xFBF6E9,LV_TEXT_ALIGN_CENTER);
    }
    save("launcher-page-2-final");
    if (clipped) return 5;
    std::cout<<"PASS: four entries fit the native 14px font\n";
    return 0;
}
