#pragma once
#include <lvgl.h>
namespace TimerPage {
void build(lv_obj_t *parent);
void refresh(bool force = false);
void poll();
void cancel();
bool back();
bool canBack();
bool inputActive();
}
