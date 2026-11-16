// Review-only launcher extension: preserve the approved 0.7.2 framebuffer.
#define main archived_alarm_preview_main
#include "../alarm-redesign-20261101/alarm_ui_preview.cpp"
#undef main
#include "hardware/arduino/yiyi_desk/src/pages/assets/tetris_app_icon.h"

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    static uint8_t backgroundData[320 * 240 * 2];
    std::ifstream input(argv[1], std::ios::binary);
    input.read(reinterpret_cast<char *>(backgroundData), sizeof(backgroundData));
    if (input.gcount() != sizeof(backgroundData)) return 3;
    out = argv[2];
    std::filesystem::create_directories(out);
    lv_init();
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, buffer, nullptr, 320 * 40);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = 320; drv.ver_res = 240;
    drv.draw_buf = &draw; drv.flush_cb = flush;
    lv_disp_drv_register(&drv); screen = lv_scr_act();
    begin("", false);
    static lv_img_dsc_t background{};
    background.header.cf = LV_IMG_CF_TRUE_COLOR;
    background.header.w = 320; background.header.h = 240;
    background.data_size = sizeof(backgroundData); background.data = backgroundData;
    auto *base = lv_img_create(screen);
    lv_img_set_src(base, &background); lv_obj_set_pos(base, 0, 0);
    auto *icon = lv_img_create(screen);
    lv_img_set_src(icon, &tetris_app_icon); lv_obj_set_pos(icon, 76, 156);
    text("俄罗斯方块", 64, 218, 80, F14, 0xFBF6E9, LV_TEXT_ALIGN_CENTER);
    save("01-launcher");
    if (clipped) return 4;
    std::cout << "PASS: review launcher label fits the existing 14px font\n";
    return 0;
}
