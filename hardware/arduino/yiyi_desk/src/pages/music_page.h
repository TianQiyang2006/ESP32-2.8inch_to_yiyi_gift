#pragma once
#include <lvgl.h>
namespace MusicPage {
void enter();
void build(lv_obj_t *parent);
void refresh(bool force=false);
bool back();
bool canBack();
bool inputActive();
void cancel();
}
