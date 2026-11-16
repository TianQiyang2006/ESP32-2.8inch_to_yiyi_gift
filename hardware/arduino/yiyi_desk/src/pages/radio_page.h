#pragma once
#include <lvgl.h>

namespace RadioPage {
void enter();
void build(lv_obj_t *parent);
void refresh(bool force=false);
bool back();
bool canBack();
bool inputActive();
void cancel();
// The launcher owns the resulting objects; this creates no image resource.
void createIcon(lv_obj_t *parent,int x,int y);
}
