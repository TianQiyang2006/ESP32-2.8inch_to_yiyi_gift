#include "ui_widgets.h"
#include "ui_styles.h"
#include "app_state.h"
#include "power_status.h"
#include "saved_design/saved_resources.h"
#include "gift_assets/gift_fonts.h"
#include "pages/assets/ui_status_icons.h"
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace Ui {
void clearOnDelete(lv_obj_t *object,lv_obj_t **handle){
    lv_obj_add_event_cb(object,[](lv_event_t *e){auto **slot=static_cast<lv_obj_t **>(lv_event_get_user_data(e));if(*slot==lv_event_get_target(e))*slot=nullptr;},LV_EVENT_DELETE,handle);
}
namespace {

lv_obj_t *rootScreen = nullptr;
int currentPage = 0;
ActionHandler actionHandler = nullptr;

// ---- press feedback ------------------------------------------------------
// Only one pointer can be down on this panel, so a single animation slot is
// enough. `animBase` is the object's background colour before the press, and
// `animFrom` is the colour the release fade starts from.
lv_obj_t *animTarget = nullptr;
lv_obj_t *batteryNotice = nullptr;
lv_color_t animBase;
lv_color_t animFrom;

lv_color_t shade(lv_color_t color) {
    // 0.78 brightness: visible on the pastel buttons without turning them black.
    return lv_color_make(uint8_t(color.ch.red * 199 / 255),
                         uint8_t(color.ch.green * 199 / 255),
                         uint8_t(color.ch.blue * 199 / 255));
}

lv_color_t mix(lv_color_t from, lv_color_t to, int amount) {  // amount 0..255
    return lv_color_make(uint8_t((from.ch.red * (255 - amount) + to.ch.red * amount) / 255),
                         uint8_t((from.ch.green * (255 - amount) + to.ch.green * amount) / 255),
                         uint8_t((from.ch.blue * (255 - amount) + to.ch.blue * amount) / 255));
}

void tintExec(void *var, int32_t value) {
    auto *object = static_cast<lv_obj_t *>(var);
    lv_obj_set_style_bg_color(object, mix(animBase, shade(animBase), int(value)), 0);
}

void restoreExec(void *var, int32_t value) {
    auto *object = static_cast<lv_obj_t *>(var);
    lv_obj_set_style_bg_color(object, mix(animFrom, animBase, int(value)), 0);
}

// Stops whatever press animation is running. `restore` puts the old object back
// to its base colour (used when a new press interrupts the previous feedback).
// The previously tracked object can already be deleted when a press handler
// removes its parent (the Tetris pause overlay deletes its buttons on click):
// lv_anim_del() only compares the pointer, and the colour restore is skipped for
// an invalid object, so a stale handle can never be dereferenced here.
void stopFeedback(bool restore) {
    if (!animTarget) return;
    const bool valid = lv_obj_is_valid(animTarget);
    lv_anim_del(animTarget, nullptr);
    if (restore && valid && lv_obj_get_style_bg_color(animTarget, 0).full != animBase.full)
        lv_obj_set_style_bg_color(animTarget, animBase, 0);
    animTarget = nullptr;
}

void pressEvent(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    auto *object = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (code == LV_EVENT_PRESSED) {
        // Fully transparent hit areas have nothing to tint.
        if (lv_obj_get_style_bg_opa(object, 0) == LV_OPA_TRANSP) return;
        stopFeedback(true);
        animTarget = object;
        animBase = lv_obj_get_style_bg_color(object, 0);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, object);
        lv_anim_set_values(&anim, 0, 255);
        lv_anim_set_time(&anim, 90);
        lv_anim_set_path_cb(&anim, easeInOut());
        lv_anim_set_exec_cb(&anim, tintExec);
        lv_anim_start(&anim);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (animTarget != object) return;
        lv_anim_del(object, nullptr);
        animFrom = lv_obj_get_style_bg_color(object, 0);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, object);
        lv_anim_set_values(&anim, 0, 255);
        lv_anim_set_time(&anim, 140);
        lv_anim_set_path_cb(&anim, easeInOut());
        lv_anim_set_exec_cb(&anim, restoreExec);
        lv_anim_start(&anim);
    }
}

}  // namespace

lv_obj_t *networkLabel = nullptr;
lv_obj_t *networkCross = nullptr;

void setRoot(lv_obj_t *screen) { UiStyles::init(); rootScreen = screen; }
void setPage(int page) { currentPage = page; }

void setActionHandler(ActionHandler handler) { actionHandler = handler; }
void dispatchAction(lv_event_t *event) { if(actionHandler)actionHandler(event); }

void attachPressFeedback(lv_obj_t *object) {
    if (object) lv_obj_add_event_cb(object, pressEvent, LV_EVENT_ALL, nullptr);
}

void cancelAnimations() {
    // Called from GiftUI::render() *after* desk.cpp has already cleaned the old
    // page, so the tracked object may be freed. lv_anim_del() only compares the
    // pointer value (it never dereferences it), but putting the base colour back
    // must not touch a freed object: check validity first. The cached home layer
    // is not cleaned on a page switch, so its card is still alive and is restored.
    if (!animTarget) return;
    stopFeedback(lv_obj_is_valid(animTarget));
}

lv_obj_t *label(lv_obj_t *parent,const String &text,int x,int y,int width,const lv_font_t *font,uint32_t color) {
    // Historical default (no font picked by the caller) keeps the shared
    // desk_font/Ink style. An explicitly requested font now wins, so callers
    // passing `saved_gift_font_bold_14_0` really get Bold 14 instead of being
    // silently replaced by the Regular desk_font.
    const bool defaultFont = (font == nullptr);
    if (!font) font = saved_gift_font_bold_14_0;

    UiStyles::init();
    auto *obj=lv_label_create(parent); lv_obj_set_pos(obj,x,y); lv_obj_set_width(obj,width);
    lv_label_set_text(obj,text.c_str());
    if (defaultFont && color == Ink) {
        lv_obj_add_style(obj,&UiStyles::titleInk,0);
    } else {
        lv_obj_set_style_text_font(obj,font,0); lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    }
    lv_obj_add_flag(obj,LV_OBJ_FLAG_GESTURE_BUBBLE); return obj;
}

lv_obj_t *panel(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius) {
    UiStyles::init();
    auto *obj=lv_obj_create(parent); lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
    if (radius == 10) {
        lv_obj_add_style(obj,&UiStyles::card,0);
    } else {
        lv_obj_add_style(obj,&UiStyles::panel,0);
        if (radius == LV_RADIUS_CIRCLE) lv_obj_add_style(obj,&UiStyles::circle,0);
        else if (radius != 12) lv_obj_set_style_radius(obj,radius,0);
    }
    lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *button(lv_obj_t *parent,const String &text,int x,int y,int w,int h,Action action,uint32_t color) {
    UiStyles::init();
    auto *obj=lv_btn_create(parent); lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,w,h);
    lv_obj_add_style(obj,&UiStyles::button,0); lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);
    auto *caption=lv_label_create(obj); lv_label_set_text(caption,text.c_str());
    lv_obj_add_style(caption,&UiStyles::buttonCaption,0);
    lv_obj_set_width(caption,w-4);
    lv_obj_center(caption);
    if (actionHandler) lv_obj_add_event_cb(obj,actionHandler,LV_EVENT_CLICKED,(void*)(uintptr_t)action);
    attachPressFeedback(obj);
    return obj;
}

void wifiIcon(int x,int y) {
    auto *holder=panel(rootScreen,x,y,32,25,Paper);
    lv_obj_add_style(holder,&UiStyles::bgTransparent,0);
    networkLabel=label(holder,LV_SYMBOL_WIFI,0,0,28,&lv_font_montserrat_20,Muted);
    networkCross=panel(holder,18,11,11,11,Paper);
    lv_obj_set_style_radius(networkCross,5,0);
    static const lv_point_t slash1[]={{2,2},{8,8}}, slash2[]={{2,8},{8,2}};
    auto *a=lv_line_create(networkCross), *b=lv_line_create(networkCross);
    lv_line_set_points(a,slash1,2); lv_line_set_points(b,slash2,2);
    for (auto *line : {a,b}) lv_obj_add_style(line,&UiStyles::slash,0);
    if (WiFi.status()==WL_CONNECTED) lv_obj_add_flag(networkCross,LV_OBJ_FLAG_HIDDEN);
}

// ==========================================================================
// Locked status bar: Wi-Fi 17x17 @(220,10) + the shared 24x15 battery and
// Bold11 percentage right-aligned to x=309. Bluetooth is intentionally absent.
// ==========================================================================
namespace {

lv_obj_t *gStatusWifi = nullptr;
BatteryView gStatusBatteryView;

void plainStatusObj(lv_obj_t *obj){
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj,0,0);
    lv_obj_set_style_shadow_width(obj,0,0);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);
}

// The two Wi-Fi assets are generated as exact 17x17 images (see
// build/impl/gen_status_icons.py), so they are never zoomed or clipped here.
lv_obj_t *statusIcon(const lv_img_dsc_t *src,int x,int y){
    auto *icon=lv_img_create(rootScreen);
    lv_img_set_src(icon,src);
    lv_obj_set_pos(icon,x,y);
    lv_obj_clear_flag(icon,LV_OBJ_FLAG_CLICKABLE);
    return icon;
}

void setStatusImage(lv_obj_t *icon,const lv_img_dsc_t *src){
    if(icon&&lv_img_get_src(icon)!=(const void*)src)lv_img_set_src(icon,src);
}

}  // namespace

namespace {

// ---- locked battery geometry (see ui_widgets.h) --------------------------
constexpr uint32_t BatBlue=0x3E8FD3,BatGreen=0x6CB46B,BatAmber=0xE1A33B,BatRed=0xD9534F,BatGray=0x9AA694;
constexpr uint32_t BatInner=0xF1F5EA;
constexpr int BatCellX[4]={2,6,11,15};
constexpr int BatCellW[4]={3,4,3,4};
constexpr int BatCellStarts[4]={0,3,7,10};  // cumulative widths of cells 0..i-1
constexpr int BatCellY=2,BatCellH=11;
constexpr int BatFillTotal=14;              // 3+4+3+4

void fillCell(lv_draw_ctx_t *ctx,const lv_area_t &base,int x,int y,int w,int h,uint32_t color,int radius){
    if(w<=0||h<=0)return;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_opa=LV_OPA_COVER; d.bg_color=lv_color_hex(color); d.radius=radius;
    lv_area_t a; a.x1=base.x1+x; a.y1=base.y1+y; a.x2=a.x1+w-1; a.y2=a.y1+h-1;
    lv_draw_rect(ctx,&d,&a);
}

void batteryDraw(lv_event_t *event){
    auto *view=static_cast<BatteryView*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    if(!view||!view->wrapper)return;
    lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(event);
    lv_area_t base; lv_obj_get_coords(view->wrapper,&base);

    // body interior (21x15, radius 3, pale)
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_opa=LV_OPA_COVER; d.bg_color=lv_color_hex(BatInner); d.radius=3;
    lv_area_t body; body.x1=base.x1; body.y1=base.y1; body.x2=base.x1+20; body.y2=base.y1+14;
    lv_draw_rect(ctx,&d,&body);

    // four fixed-width fill cells (100 % fills all of them, no right gap)
    for(int i=0;i<4;i++){
        int width=view->fillPx-BatCellStarts[i];
        if(width<0)width=0;
        if(width>BatCellW[i])width=BatCellW[i];
        fillCell(ctx,base,BatCellX[i],BatCellY,width,BatCellH,view->color,1);
    }

    // positive terminal 3x7 @(21,4), outside the 21 px body
    fillCell(ctx,base,21,4,3,7,view->color,1);

    // 1 px frame above the cells; the <=5 % blink simply skips it
    if(!view->blinkOff){
        lv_draw_rect_dsc_init(&d);
        d.bg_opa=LV_OPA_TRANSP;
        d.border_width=1; d.border_opa=LV_OPA_COVER;
        d.border_color=lv_color_hex(view->color); d.radius=3;
        lv_draw_rect(ctx,&d,&body);
    }

    // white charging bolt painted last (above the fill cells)
    if(view->charging&&view->present){
        lv_draw_label_dsc_t ld; lv_draw_label_dsc_init(&ld);
        ld.font=&lv_font_montserrat_10; ld.color=lv_color_white();
        lv_point_t size; lv_txt_get_size(&size,LV_SYMBOL_CHARGE,&lv_font_montserrat_10,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        lv_area_t a;
        a.x1=base.x1+(21-size.x)/2; a.y1=base.y1+(15-size.y)/2;
        a.x2=a.x1+size.x-1; a.y2=a.y1+size.y-1;
        lv_draw_label(ctx,&ld,&a,LV_SYMBOL_CHARGE,nullptr);
    }
}

}  // namespace

void batteryCreate(BatteryView &view,lv_obj_t *parent,int x,int y,int percentRight,float percentCentreY){
    view=BatteryView{};
    view.wrapper=lv_obj_create(parent);
    plainStatusObj(view.wrapper);
    lv_obj_set_pos(view.wrapper,x,y);
    lv_obj_set_size(view.wrapper,24,15);
    lv_obj_set_style_bg_opa(view.wrapper,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(view.wrapper,0,0);
    lv_obj_set_style_radius(view.wrapper,0,0);
    lv_obj_set_user_data(view.wrapper,&view);
    lv_obj_add_event_cb(view.wrapper,batteryDraw,LV_EVENT_DRAW_MAIN,nullptr);

    // percentage: Bold11, right edge at percentRight, centred on the battery.
    // The 36 px label box starts 6 px right of the cap (cap right edge = x+24).
    // LVGL puts the label's line box at labelY while the visible glyph box sits
    // ~1 px higher, so compensate to land the ink centre exactly on the battery
    // centre (18.5 for the 24x15 cluster).
    const int labelY=(int)lroundf(percentCentreY-saved_gift_font_bold_11_0->line_height/2.0f)+1;
    view.percent=label(parent,"",percentRight-36,labelY,36,saved_gift_font_bold_11_0,Ink);
    lv_obj_set_style_text_align(view.percent,LV_TEXT_ALIGN_RIGHT,0);

    batteryRefresh(view);
}

void batteryRefresh(BatteryView &view){
    if(!view.wrapper||!view.percent)return;
    auto power=PowerStatus::get();
    int pct=power.installed&&power.voltageValid?power.percent:-1;
    if(pct>100)pct=100;
    const bool charging=power.installed&&power.chargingKnown&&power.charging;
    const bool present=pct>=0;
    const bool low=present&&(power.low||power.protection.high);
    const uint32_t color=!present?BatGray:(charging?BatBlue:(low?BatRed:(pct<=50?BatAmber:BatGreen)));
    const int fillPx=!present?0:(pct*BatFillTotal+50)/100;
    const bool blinkOff=power.critical&&((millis()/500)%2==0);

    char text[16];
    if(!present)snprintf(text,sizeof(text),"—");
    else snprintf(text,sizeof(text),"%d %%",pct);
    const char *current=lv_label_get_text(view.percent);
    if(!current||strcmp(current,text)!=0)lv_label_set_text(view.percent,text);

    // The wrapper pixels are produced by the draw callback from these cached
    // fields, and the text colour is a pure function of them. Only repaint (and
    // re-set the colour) when one of them actually changes, so a 2 Hz caller no
    // longer re-invalidates the battery area every tick.
    if(color==view.color&&fillPx==view.fillPx&&present==view.present&&
       charging==view.charging&&blinkOff==view.blinkOff)return;
    view.color=color;view.present=present;view.charging=charging;
    view.fillPx=fillPx;view.blinkOff=blinkOff;
    const uint32_t textColor=!present?BatGray:(charging?BatBlue:(low?BatRed:Ink));
    lv_obj_set_style_text_color(view.percent,lv_color_hex(textColor),0);

    lv_obj_invalidate(view.wrapper);
}

void batteryClear(BatteryView &view){ view=BatteryView{}; }

void batteryNoticeRefresh() {
    static BatteryPolicy::NoticeGate gate;
    auto &notice=batteryNotice;
    const auto power=PowerStatus::get();
    const auto event=gate.update(power.protection);
    if(notice&&!lv_obj_is_valid(notice))notice=nullptr;
    if(event==BatteryPolicy::Notice::None) {
        if(notice&&!power.protection.low&&!power.protection.sensorFault&&!power.protection.high) {
            lv_obj_del(notice);notice=nullptr;
        }
        return;
    }
    if(notice){lv_obj_del(notice);notice=nullptr;}
    notice=lv_obj_create(lv_layer_top());
    lv_obj_add_event_cb(notice,[](lv_event_t*){batteryNotice=nullptr;},LV_EVENT_DELETE,nullptr);
    lv_obj_remove_style_all(notice);lv_obj_set_size(notice,320,240);
    lv_obj_set_style_bg_color(notice,lv_color_black(),0);lv_obj_set_style_bg_opa(notice,LV_OPA_50,0);
    lv_obj_clear_flag(notice,LV_OBJ_FLAG_SCROLLABLE);
    auto *card=panel(notice,25,56,270,128,0xFFFDF8,12);
    const bool fault=event==BatteryPolicy::Notice::SensorFault;
    const bool high=event==BatteryPolicy::Notice::HighVoltage;
    const char *title=high?"电压偏高":fault?"电压读取异常":event==BatteryPolicy::Notice::Critical?"电量很低":"电量不足";
    label(card,title,16,12,238,saved_gift_font_bold_14_0,0xB52A2A);
    label(card,high?"请断开充电并检查电池":fault?"请检查电池连接":"请通过外置充电模块充电",16,41,238,saved_gift_font_bold_10_0,Ink);
    auto *close=lv_btn_create(card);lv_obj_set_pos(close,75,80);lv_obj_set_size(close,120,32);
    auto *caption=lv_label_create(close);lv_label_set_text(caption,"知道了");
    lv_obj_set_style_text_font(caption,saved_gift_font_bold_14_0,0);lv_obj_center(caption);
    lv_obj_add_event_cb(close,[](lv_event_t *event){
        auto *overlay=static_cast<lv_obj_t*>(lv_event_get_user_data(event));
        lv_obj_del_async(overlay);
    },LV_EVENT_CLICKED,notice);
    lv_obj_move_foreground(notice);
}

bool batteryNoticeVisible(){return batteryNotice!=nullptr;}

void statusCluster(bool offlineIcon) {
    (void)offlineIcon;
    const bool online=WiFi.status()==WL_CONNECTED;
    gStatusWifi=statusIcon(online?&ui_status_wifi:&ui_status_nowifi,220,10);
    batteryCreate(gStatusBatteryView,rootScreen,243,11,309,18.5f);
    refreshStatusIcons();
}

void refreshStatusIcons() {
    if(gStatusWifi){
        const bool online=WiFi.status()==WL_CONNECTED;
        setStatusImage(gStatusWifi,online?&ui_status_wifi:&ui_status_nowifi);
    }
    batteryRefresh(gStatusBatteryView);
}

void clearStatusCluster() {
    gStatusWifi=nullptr;
    batteryClear(gStatusBatteryView);
}

void header(const char *title,bool isApp,int titleX,int titleY,bool offlineIcon) {
    if (isApp) {
        auto *back=button(rootScreen,"",8,6,36,32,Back);
        lv_obj_add_style(back,&UiStyles::bgTransparent,0);
        if(auto *cap=lv_obj_get_child(back,0)){lv_label_set_text(cap,LV_SYMBOL_LEFT);lv_obj_set_style_text_font(cap,&lv_font_montserrat_20,0);}
    }
    label(rootScreen,title,titleX,titleY,210);
    statusCluster(offlineIcon);
}

void backChevron(int x,int y,Action action,uint32_t color) {
    // The design draws the back arrow as a 5x10 polyline with a 1.8 px stroke,
    // not a font glyph. Points are relative to (x,y).
    static const lv_point_t points[]={{5,0},{0,5},{5,10}};
    auto *line=lv_line_create(rootScreen);
    lv_line_set_points(line,points,3);
    lv_obj_set_pos(line,x,y);
    lv_obj_set_style_line_width(line,2,0);
    lv_obj_set_style_line_color(line,lv_color_hex(color),0);
    lv_obj_set_style_line_rounded(line,true,0);
    lv_obj_clear_flag(line,LV_OBJ_FLAG_CLICKABLE);
    // Invisible hit area: the drawn arrow alone is too small to tap reliably.
    auto *hit=lv_obj_create(rootScreen);
    lv_obj_remove_style_all(hit);
    lv_obj_set_pos(hit,x-6,y-8);
    lv_obj_set_size(hit,48,34);
    lv_obj_add_flag(hit,LV_OBJ_FLAG_CLICKABLE);
    if (actionHandler) lv_obj_add_event_cb(hit,actionHandler,LV_EVENT_CLICKED,(void*)(uintptr_t)action);
}

void compactHeader(const char *title,bool offlineIcon) {
    backChevron(12,14);
    label(rootScreen,title,30,9,220,&fd_sans_bold_16,0x394637);
    statusCluster(offlineIcon);
}

void footer() {
    for (int i=0;i<7;++i) {
        auto *touch=panel(rootScreen,109+i*14,229,14,11,Paper);
        lv_obj_add_style(touch,&UiStyles::bgTransparent,0);lv_obj_add_flag(touch,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(touch,[](lv_event_t *e){page=(uintptr_t)lv_event_get_user_data(e);app=App::None;renderPending=true;lastInputAt=millis();},LV_EVENT_CLICKED,(void*)(uintptr_t)i);
        auto *dot=panel(touch,i==currentPage?3:5,6,i==currentPage?8:4,3,i==currentPage ? Ink : 0xBFCEB6);
        lv_obj_clear_flag(dot,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(dot,3,0);
    }
}

bool selfTest() {
    // The palette must stay distinguishable: the footer relies on Ink vs the
    // inactive dot colour, and the page highlight depends on the stored index.
    bool ok = true;
    ok &= (Ink != Muted) && (Muted != Paper) && (Ink != 0xBFCEB6);
    const int saved = currentPage;
    setPage(3);
    ok &= (currentPage == 3);
    setPage(saved);
    // A registered handler is required or every button silently stops working.
    ok &= (actionHandler != nullptr);
    return ok;
}

}
