#pragma once
#include <cstdint>
struct lv_obj_t {};
// Minimal input state for touch.h's inline swipe helper. The panel suite drives
// read_cb directly; LVGL dispatch itself is exercised by the native UI runners.
struct lv_indev_t { lv_obj_t *scroll_obj = nullptr; bool wait_until_release = false; };
inline lv_obj_t *lv_indev_get_scroll_obj(const lv_indev_t *input) { return input ? input->scroll_obj : nullptr; }
inline void lv_indev_wait_release(lv_indev_t *input) { if(input) input->wait_until_release = true; }
enum { LV_INDEV_STATE_REL = 0, LV_INDEV_STATE_PR = 1, LV_INDEV_TYPE_POINTER = 1 };
struct lv_indev_data_t { struct { int16_t x = 0, y = 0; } point; int state = 0; };
struct lv_indev_drv_t { int type = 0; void (*read_cb)(lv_indev_drv_t *, lv_indev_data_t *) = nullptr; };
inline void lv_indev_drv_init(lv_indev_drv_t *driver) { *driver = lv_indev_drv_t{}; }
inline void lv_label_set_text_fmt(lv_obj_t *, const char *, ...) {}
