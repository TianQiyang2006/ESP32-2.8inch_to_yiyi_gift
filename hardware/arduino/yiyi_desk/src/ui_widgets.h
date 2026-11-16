#pragma once
// Shared widgets and palette for the built-in UI.
//
// Signatures deliberately match the functions that used to live in desk.cpp, so
// call sites only need `using namespace Ui;` rather than being rewritten. The two
// pieces of screen context these widgets need are set once: the root screen never
// changes, and the page index only matters while a view is being built.
#include <Arduino.h>
#include <lvgl.h>

extern "C" const lv_font_t desk_font;

namespace Ui {

constexpr uint32_t Ink = 0x294C3D, Muted = 0x637F68, Paper = 0xF6F7EF, Green = 0xDCE8D4;

// All hand-written widgets share one easing curve so local animations feel the
// same as the existing flip-card clock (which already used ease_in_out).
inline lv_anim_path_cb_t easeInOut(){ return lv_anim_path_ease_in_out; }

// Every built-in view is drawn into this screen. Set once during setup.
// No accessor is exposed: desk.cpp already owns a variable named `root`, and an
// accessor of the same name inside namespace Ui would make it ambiguous.
void setRoot(lv_obj_t *screen);
// For long-lived handles to transient children cleared by a page rebuild.
// The handle storage must outlive the widget (e.g. a static/global pointer).
void clearOnDelete(lv_obj_t *object,lv_obj_t **handle);

// Footer dots highlight the current page. Set at the start of each render.
void setPage(int page);

// `font == nullptr` (the default) keeps the historical desk_font/Ink look; a
// font passed explicitly is applied as-is (so saved_gift_font_bold_14_0 really
// renders Bold, it is no longer replaced by desk_font).
lv_obj_t *label(lv_obj_t *parent,const String &text,int x,int y,int width,
                const lv_font_t *font=nullptr,uint32_t color=Ink);
// radius defaults to 12 (the historical Ui::panel look). 10 and LV_RADIUS_CIRCLE
// select the shared `card` / `circle` styles, every other value is a local radius.
lv_obj_t *panel(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color=Green,int radius=12);

// Enumeration of every clickable action in the built-in UI. The handler that
// interprets them stays in the UI layer.
enum Action { Back=1, OpenMessages, OpenWeather, OpenVoice, OpenCalendar, OpenSettings,
    OpenPhone, OpenAbout, OpenDiagnostics, RefreshWeather,
    CalendarPrev, CalendarNext, CalendarToday, SelectDate,
    SetCountdown, ClearCountdown, ToggleBgr, ToggleInvert, ChangeTouch, ToggleWhite, OpenWifi, RefreshQuote, RefreshPoem,
    CalendarCancel, CalendarConfirm, OpenFocus };

using ActionHandler = void (*)(lv_event_t *);
void setActionHandler(ActionHandler handler);
void dispatchAction(lv_event_t *event);

lv_obj_t *button(lv_obj_t *parent,const String &text,int x,int y,int w,int h,Action action,uint32_t color=Green);

// Small press feedback for a clickable object: the background is tinted on
// press and eased back on release. Only the button's own area is invalidated.
// Handler is shared; objects must be freed through a full page rebuild.
void attachPressFeedback(lv_obj_t *object);

// Cancels the press-feedback animation of the currently pressed object. It is
// called from GiftUI::render() AFTER desk.cpp cleaned the old page, so the
// tracked object may already be freed: the object is dereferenced (to put the
// base colour back) only after lv_obj_is_valid() confirms it still exists.
// lv_anim_del() itself only compares the pointer value, never dereferences it.
void cancelAnimations();

// Wi-Fi glyph plus the red cross shown while the link is down. The two objects are
// owned here because this function creates them; the UI layer updates their text
// and visibility through these handles.
extern lv_obj_t *networkLabel;
extern lv_obj_t *networkCross;
void wifiIcon(int x,int y);

void header(const char *title,bool isApp,int titleX=52,int titleY=12,bool offlineIcon=false);
// Compact 36 px header used by settings / about / phone, matching the locked
// design: a thin drawn chevron at (12,14) and the title at (30,9). The generic
// header() above keeps a 36 px glyph button at 8..44, which is why it cannot be
// used for these three pages.
void backChevron(int x,int y,Action action=Back,uint32_t color=0x394637);
void compactHeader(const char *title,bool offlineIcon=false);
// ---- locked status-bar battery (public component) -------------------------
// Geometry given by the locked design (screen pixels, relative to wrapper):
//   wrapper 24x15, transparent, position only;
//   body 21x15 @(0,0), 1 px frame, radius 3, no white highlight bar;
//   4 fixed fill cells 3/4/3/4 wide at x = 2/6/11/15, y = 2, height 11;
//   cap 3x7 @(21,4) - outside the 21 px body, never enclosed by a 24 px box;
//   white charging bolt painted above the cells (never covered by the fill);
//   percent Bold11, right edge = `percentRight`, vertical centre = the battery
//   centre; the label box starts 6 px right of the cap.
// All drawing happens in one LVGL draw callback so the pixels are deterministic
// (no nested-object layout surprises). Every page-top status cluster and the
// home bottom status bar use this component.
struct BatteryView {
    lv_obj_t *wrapper = nullptr;  // 24x15 transparent draw surface
    lv_obj_t *percent = nullptr;  // Bold11 percentage label
    // render state consumed by the draw callback (set by batteryCreate/Refresh)
    uint32_t color = 0;
    int fillPx = 0;
    bool present = false;
    bool charging = false;
    bool blinkOff = false;
};
void batteryCreate(BatteryView &view, lv_obj_t *parent, int x, int y, int percentRight, float percentCentreY);
// Repaints colours / fill / charging bolt / blinking low-battery frame from
// PowerStatus. Safe to call at any rate; only updates the cached state.
void batteryRefresh(BatteryView &view);
// Forgets the pointers after the parent layer was cleaned.
void batteryClear(BatteryView &view);
// One dismissible notice per low-battery episode; never sleeps or changes settings.
void batteryNoticeRefresh();
bool batteryNoticeVisible();

// Shared right-top cluster used by every app/sub page in the latest design:
// Wi-Fi (or offline) icon, Bluetooth icon, battery icon and measured, estimated percentage (or an unknown marker).
void statusCluster(bool offlineIcon=false);
void refreshStatusIcons();
void clearStatusCluster();
void footer();

// Verifies the palette plumbing and that the action handler is wired.
bool selfTest();

}
