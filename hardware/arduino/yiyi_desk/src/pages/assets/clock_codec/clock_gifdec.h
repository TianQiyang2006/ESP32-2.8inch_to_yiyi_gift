/* LVGL 8.4 gifdec; see LICENSE.txt. Private clock decoder. */
#ifndef CLOCK_GIFDEC_H
#define CLOCK_GIFDEC_H

#include <stdint.h>
#include <lvgl.h>



typedef struct clock_gd_Palette {
    int size;
    uint8_t colors[0x100 * 3];
} clock_gd_Palette;

typedef struct clock_gd_GCE {
    uint16_t delay;
    uint8_t tindex;
    uint8_t disposal;
    int input;
    int transparency;
} clock_gd_GCE;



typedef struct clock_gd_GIF {
    lv_fs_file_t fd;
    const char * data;
    uint8_t is_file;
    uint32_t f_rw_p;
    int32_t anim_start;
    uint16_t width, height;
    uint16_t depth;
    int32_t loop_count;
    clock_gd_GCE gce;
    clock_gd_Palette *palette;
    clock_gd_Palette lct, gct;
    void (*plain_text)(
        struct clock_gd_GIF *gif, uint16_t tx, uint16_t ty,
        uint16_t tw, uint16_t th, uint8_t cw, uint8_t ch,
        uint8_t fg, uint8_t bg
    );
    void (*comment)(struct clock_gd_GIF *gif);
    void (*application)(struct clock_gd_GIF *gif, char id[8], char auth[3]);
    uint16_t fx, fy, fw, fh;
    uint8_t bgindex;
    uint8_t *canvas, *frame;
} clock_gd_GIF;

clock_gd_GIF * clock_gd_open_gif_file(const char *fname);

clock_gd_GIF * clock_gd_open_gif_data(const void *data);

void clock_gd_render_frame(clock_gd_GIF *gif, uint8_t *buffer);

int clock_gd_get_frame(clock_gd_GIF *gif);
void clock_gd_rewind(clock_gd_GIF *gif);
void clock_gd_close_gif(clock_gd_GIF *gif);



#endif /* CLOCK_GIFDEC_H */
