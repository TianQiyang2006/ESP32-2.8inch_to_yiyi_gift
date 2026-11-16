#pragma once
#include <lvgl.h>

namespace DashboardClockSettings {
void build(lv_obj_t *parent);
void refresh();
void cancel();
bool active();
bool inputActive();
// Handles only the custom-colour subpage. The outer settings page belongs to
// the clock picker and returns through GiftUI's normal back action.
bool back();
}
