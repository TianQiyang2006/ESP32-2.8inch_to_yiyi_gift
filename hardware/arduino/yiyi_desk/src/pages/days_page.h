#pragma once
#include <lvgl.h>

namespace DaysPage {
void createIcon(lv_obj_t *parent, int x, int y);
void build(lv_obj_t *parent);
void refresh(bool force = false);
void cancel();
bool back();
bool canBack();
bool inputActive();
}
