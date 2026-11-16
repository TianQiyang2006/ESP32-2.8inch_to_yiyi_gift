#pragma once
// Shared LVGL styles for the hand-written UI layer.
//
// The generated designer pages (src/saved_design/screen_*.c) keep their inline
// styles; this table only replaces repeated property groups in the widgets that
// are built by hand (ui_widgets.cpp and gift_ui.cpp) so every object no longer
// allocates its own copy of the same properties.
//
// Every value below is transcribed from an existing lv_obj_set_style_* call with
// the same value, so applying a style instead of the inline calls must not move
// a single pixel.  `selector` passed to lv_obj_add_style is always 0 (part MAIN,
// state DEFAULT), matching the original inline calls.
//
// UiStyles::init() is idempotent and must run after lv_init().  Ui::setRoot()
// calls it, and the widget helpers call it too as a safety net, so no call site
// outside this module has to change.
#include <lvgl.h>

namespace UiStyles {

// panel:  was border_width 0 / radius 12 / pad_all 0 in Ui::panel()
extern lv_style_t panel;
// card:   same but radius 10 - the default radius of the hand-written panel()
extern lv_style_t card;
// button: was shadow_width 0 / border_width 0 / radius 10 / pad_all 2
extern lv_style_t button;
// buttonCaption: was text_font desk_font / text_color Ink / text_align CENTER
extern lv_style_t buttonCaption;
// titleInk: was text_font desk_font / text_color Ink (the Ui::label() defaults)
extern lv_style_t titleInk;
// labelLine2: was text_line_space 2 on every hand-written label
extern lv_style_t labelLine2;
// textCenter: was text_align CENTER
extern lv_style_t textCenter;
// bgTransparent: was bg_opa LV_OPA_TRANSP
extern lv_style_t bgTransparent;
// iconTint: was img_recolor_opa LV_OPA_COVER (colour itself stays inline)
extern lv_style_t iconTint;
// slash: was line_width 2 / line_color 0xAD514C / line_rounded true
extern lv_style_t slash;
// sliderTrack: was bg_color 0x414747 on the main part
extern lv_style_t sliderTrack;
// sliderKnob: was pad_all 4 on the knob part
extern lv_style_t sliderKnob;
// circle: was radius LV_RADIUS_CIRCLE
extern lv_style_t circle;
// flat: was border_width 0 / shadow_width 0
extern lv_style_t flat;

// Initialises the table once.  Safe to call repeatedly and from any helper.
void init();

// True after init() has run; helpers assert/handle the not-yet-ready case.
bool ready();

}  // namespace UiStyles
