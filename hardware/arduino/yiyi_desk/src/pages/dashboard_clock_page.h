#pragma once
#include <lvgl.h>

namespace DashboardClockPage {
void build(lv_obj_t *parent,const char *preset);
void refresh();
void cancel();
bool active();
struct Status {
    const char *preset;
    char time[9];
    unsigned movingDigits;
    bool seconds,tilt,rasterReady;
};
Status status();
#ifdef GIFT_HOST_TEST
int digitShown(unsigned digit);
int digitTarget(unsigned digit);
int digitAngle(unsigned digit); // tenths of a degree
int digitOffset(unsigned digit);
void debugFailRaster(bool fail);
#endif
}
