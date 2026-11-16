#include "desk.h"
#include "board_config.h"
#include "desk_logic.h"
#include "src/app_state.h"
#include "src/display.h"
#include "src/transition_effect.h"
#include "src/jobs.h"
#include "src/net.h"
#include "src/retry_policy.h"
#include "src/captive_portal.h"
#include "src/screens.h"
#include "src/text_util.h"
#include "src/touch.h"
#include "src/ui_widgets.h"
#include "src/ui_export/ui_demo.h"
#include "src/wifi_setup.h"
#include "src/gift_data.h"
#include "src/gift_ui.h"
#include "src/pages/page_api.h"
#include "src/nav_logic.h"
#include "src/gift_assets/gift_fonts.h"
#include "src/gift_assets/gift_glyph_coverage.h"
#include "src/gift_lunar.h"
#include "src/saved_design/saved_resources.h"
#include "src/saved_design/saved_design.h"
#include "src/power_status.h"
#include "src/xiaozhi_voice.h"
#include "src/schedule_store.h"
#include "src/audio_board.h"
#include "src/pages/mini_apps.h"
#include "src/pages/dashboard_clock_page.h"
#include "src/pages/music_page.h"
#include "src/music_player.h"
#include "src/radio_player.h"
#include "src/pages/radio_page.h"
#include "src/pages/focus_service.h"
#include "src/pages/timer_service.h"
#include "src/pages/days_store.h"
#include "gift_portal.h"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <time.h>
#include <algorithm>
#include <atomic>

extern "C" const lv_font_t desk_font;

namespace {
using namespace Board;
static_assert(NavAppCount == (int)App::Games + 1, "Navigation table must cover every app");
// Cost of the two hottest loop stages, exposed through `status` so before/after
// numbers come from the panel build rather than a spreadsheet estimate.
static uint32_t lastRenderUs=0,lastRefreshUs=0;
// Split of render(): capturing the old frame, building widgets, flushing the new one.
static uint32_t lastCaptureUs=0,lastBuildUs=0,lastFlushUs=0;
// Last render() cost per view. Index 0..6 = built-in pages when app==App::None,
// 7+app = the hand-written App pages (App::None never uses those slots).
static uint16_t viewRenderMs[7+NavAppCount]={0};
// Job pipeline types/queues now live in src/jobs.h.
using namespace Jobs;
// Pure formatting helpers now live in src/text_util.h.
using namespace TextUtil;
// Palette colours Ink/Muted/Paper/Green now live in src/ui_widgets.h (namespace Ui).
// Shared state (settings, weather, page/app, label pointers, card state) is defined
// in src/app_state.cpp so the UI layer can be split into its own modules.
WebServer server(80);
DNSServer portalDns;
Preferences prefs;
lv_indev_drv_t touchDriver;
lv_indev_t *touchDevice = nullptr;
bool dirty = false;
uint32_t dirtyAt = 0, reconnectAt = 0, reconnectDue = 0;

uint32_t weatherAt = 0;
uint32_t weatherGeneration = 0;

// The job pipeline's types and queues live in src/jobs.h (namespace Jobs).

String serialInput;
bool screenshotPending = false;
// A2: a timetable imported from the phone portal is stored here and committed by
// deskLoop(), so the HTTP handler answers before the LittleFS write.
String pendingSchedule;
bool schedulePending = false;
// Set by the IO worker when it cannot allocate a Result; the loop thread releases
// the pending flags and reports the failure instead of leaving a request stuck.
std::atomic<bool> resultOutOfMemory{false};
bool liveRefreshPending = false;
Ui::BatteryView aboutBatteryView;
lv_obj_t *aboutPowerState=nullptr;
uint32_t readingAt=0;String readingDay;
// Last synchronous reading error (network or glyph rejection), exposed through
// `status` and /api/gift. readingNoticeAt throttles the user-facing toast.
String readingError;
uint32_t readingNoticeAt=0;
uint8_t weatherFailures=0;
uint32_t weatherRetryDelay=WeatherRetryMs;
String renderedView;uint8_t renderedPage=0;App renderedApp=App::None;

// ---- V2 navigation model ---------------------------------------------------
// One origin slot per App: "which app/page did this App get entered from". Unlike
// the old single (phoneReturn, phoneReturnPage) pair it survives a second-level
// entry (page 6 -> Settings -> Wi-Fi); see src/nav_logic.h and audit P0-1.
NavOrigin navHistory[NavAppCount] = {};
// Direction for the next render when back() switches App -> App. Only that case
// needs an override (App -> page already renders with -1); the value is consumed
// and cleared by every render, whether or not a transition actually starts.
int pendingBackDirection = 0;

// ---- cached home / permanent page layers (P1-3) ---------------------------
// Two permanent children of the LVGL screen. The home view is built once into
// homeLayer; every other view is (re)built inside pageLayer. Both layers are
// transparent and 320x240. Design backgrounds used to be painted on the screen
// object itself, so after every build the layer colour is mirrored onto the
// screen (mirrorBackground) - the visible pixels are unchanged.
lv_obj_t *homeLayer = nullptr, *pageLayer = nullptr;
lv_obj_t *activeLayer = nullptr;   // layer renderToast() draws into
GiftUI::HomeRefs homeRefs;
bool homeCached = false;
bool cachedHomeShown = false;
uint32_t homeCacheHits = 0;   // number of cached-home returns (status diagnostics)

void ensureLayers() {
    if (homeLayer && pageLayer) return;
    auto makeLayer = []() {
        auto *layer = lv_obj_create(root);
        lv_obj_remove_style_all(layer);   // no theme border/pad/background
        lv_obj_set_pos(layer, 0, 0);
        lv_obj_set_size(layer, Board::Width, Board::Height);
        lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
        return layer;
    };
    homeLayer = makeLayer();
    pageLayer = makeLayer();
}

void showLayer(lv_obj_t *layer) {
    if (!homeLayer || !pageLayer) return;
    if (layer == homeLayer) {
        lv_obj_clear_flag(homeLayer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pageLayer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(pageLayer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(homeLayer, LV_OBJ_FLAG_HIDDEN);
    }
    activeLayer = layer;
}

// Copies the background colour the UI layer painted on a transparent layer onto
// the screen object (where that background used to be painted before the layers
// existed). Only writes when the colour actually changed.
void mirrorBackground(lv_obj_t *layer) {
    if (!layer || !root) return;
    lv_color_t backgroundColor = lv_obj_get_style_bg_color(layer, 0);
    if (lv_obj_get_style_bg_color(root, 0).full != backgroundColor.full)
        lv_obj_set_style_bg_color(root, backgroundColor, 0);
}

bool isHomeView() { return app == App::None && page == 0; }

// ---- calendar page (B1) ---------------------------------------------------
// The stock lv_calendar widget draws its today/highlight states with the theme
// primary colour, so it cannot express the three states this page needs.  The
// month view is therefore a small 7x6 grid of transparent hit areas; each one
// owns a soft selected pill, a today ring and an optional target dot.  The date
// maths still comes from DeskLogic, not a second calendar implementation.
struct CalendarCell {
    lv_obj_t *hit = nullptr;
    lv_obj_t *pill = nullptr;
    lv_obj_t *ring = nullptr;
    lv_obj_t *number = nullptr;
    lv_obj_t *dot = nullptr;
    uint16_t year = 0;
    uint8_t month = 0, day = 0;
    bool current = false;
};
CalendarCell calendarCells[42];
lv_obj_t *calendarBar = nullptr, *calendarBarDate = nullptr;
bool calendarSelectionActive = false;
constexpr uint32_t CalendarInk = 0x244432;
constexpr uint32_t CalendarDayInk = 0x385443;
constexpr uint32_t CalendarMuted = 0xAAB5A2;
constexpr uint32_t CalendarGreen = 0x6E9878;
constexpr uint32_t CalendarSelect = 0xD6E4CC;
constexpr uint32_t CalendarSelectLine = 0xA9C49B;
constexpr uint32_t CalendarTarget = 0xC58B4F;
constexpr uint32_t CalendarLine = 0xE2E7DA;

// Lightweight confirmation toast.  The text and deadline live outside the widget
// tree because every render starts by deleting all of root's children.
lv_obj_t *toast = nullptr;
uint32_t toastUntil = 0;
String toastText;
bool toastError = false;
String countdownError;

void queueRender() { renderPending = true; }
bool timeValid() { return DeskLogic::timeValid((int64_t)time(nullptr)); }
void changed() { dirty = true; dirtyAt = millis(); }
bool saveSettings() {
    // A4: NVS writes can fail (partition full / handle not open); report it so the
    // caller keeps `dirty` set and retries instead of losing the setting silently.
    // NOTE: this core's putString() returns strlen(value), so an empty value
    // returns 0 even on success; verify the persisted value by read-back first.
    auto str=[&](const char *key,const String &value){
        const char *text=value.c_str(); if(!text) text="";
        if(prefs.putString(key,text)>0) return true;
        return prefs.getString(key).equals(text);
    };
    bool ok=true;
    ok=str("ssid",settings.ssid)&&ok;
    ok=str("pass",settings.password)&&ok;
    ok=str("city",settings.city)&&ok;
    ok=(prefs.putDouble("lat",settings.latitude)>0)&&ok;
    ok=(prefs.putDouble("lon",settings.longitude)>0)&&ok;
    ok=(prefs.putBool("location",settings.hasLocation)>0)&&ok;
    ok=str("cname",settings.countdownName)&&ok;
    ok=(prefs.putLong64("target",settings.target)>0)&&ok;
    ok=(prefs.putBool("hastarget",settings.hasTarget)>0)&&ok;
    ok=(prefs.putUChar("brightness",settings.brightness)>0)&&ok;
    ok=(prefs.putUChar("touch",settings.touchMode)>0)&&ok;
    ok=(prefs.putBool("invert",settings.inverted)>0)&&ok;
    ok=(prefs.putBool("bgr",settings.bgr)>0)&&ok;
    ok=(prefs.putBool("white",settings.white)>0)&&ok;
    if(ok)dirty=false;
    return ok;
}
// Two-point affine touch calibration lives in NVS alongside the other settings.
// The Touch module itself stays storage-free.
void saveTouchCalibration() {
    const Touch::Calibration &cal = Touch::calibration();
    prefs.putShort("calx0",cal.xMin); prefs.putShort("calx1",cal.xMax);
    prefs.putShort("caly0",cal.yMin); prefs.putShort("caly1",cal.yMax);
    prefs.putBool("calok",cal.valid);
}
// A6: the legacy affine mapping conflicts with a freshly measured two-point
// calibration; clear both the live mapping and its NVS enable flag.
void clearAffineCalibration() {
    Touch::setAffineCalibration(TouchTransform::Affine{});
    prefs.putBool("affok",false);
}
void loadSettings() {
    prefs.begin("yiyi-desk",false);
    settings.ssid = prefs.getString("ssid"); settings.password = prefs.getString("pass");
    settings.city = prefs.getString("city"); settings.latitude = prefs.getDouble("lat",0); settings.longitude = prefs.getDouble("lon",0);
    settings.hasLocation = prefs.getBool("location",false); settings.countdownName = prefs.getString("cname");
    settings.target = prefs.getLong64("target",0);
    settings.hasTarget = prefs.getBool("hastarget",settings.target!=0);
    settings.brightness = std::max<uint8_t>(12,prefs.getUChar("brightness",180));
    // Older builds did not persist the LCD orientation. Reapply the default
    // touch mapping when the saved orientation does not match this firmware.
    const bool hasOrientation = prefs.isKey("orient");
    const uint8_t savedOrientation = prefs.getUChar("orient",0xFF);
    const bool migratePanel = prefs.getUChar("panelrev",0) != Board::PanelProfileRevision;
    settings.touchMode = prefs.getUChar("touch",Board::TouchMode) % 4;
    if (!hasOrientation || savedOrientation != Board::LcdRotation) {
        settings.touchMode = Board::TouchMode;
        prefs.putUChar("touch",settings.touchMode);
        prefs.putUChar("orient",Board::LcdRotation);
    }
    settings.inverted = prefs.getBool("invert",Board::LcdInverted); settings.bgr = prefs.getBool("bgr",Board::LcdBgr);
    if (migratePanel) {
        // The actual board retained bgr=false and touch=2 from earlier diagnostics.
        // Restore this IPS/CTP profile once, without resetting Wi-Fi or gift data.
        settings.bgr = Board::LcdBgr; settings.inverted = Board::LcdInverted;
        settings.touchMode = Board::TouchMode;
        prefs.putBool("bgr",settings.bgr); prefs.putBool("invert",settings.inverted);
        prefs.putUChar("touch",settings.touchMode); prefs.putBool("calok",false);
        prefs.putUChar("panelrev",Board::PanelProfileRevision);
    }
    settings.white = prefs.getBool("white",false);
    Touch::Calibration cal;
    cal.xMin = prefs.getShort("calx0",0); cal.xMax = prefs.getShort("calx1",319);
    cal.yMin = prefs.getShort("caly0",0); cal.yMax = prefs.getShort("caly1",239);
    cal.valid = prefs.getBool("calok",false) && cal.xMax > cal.xMin && cal.yMax > cal.yMin;
    Touch::setCalibration(cal);
    TouchTransform::Affine affine;
    affine.ax=prefs.getFloat("affax",0);affine.bx=prefs.getFloat("affbx",1);affine.cx=prefs.getFloat("affcx",0);
    affine.ay=prefs.getFloat("affay",-1);affine.by=prefs.getFloat("affby",0);affine.cy=prefs.getFloat("affcy",239);
    affine.valid=prefs.getBool("affok",false)&&prefs.getUChar("affver",0)==Board::PanelProfileRevision;
    Touch::setAffineCalibration(affine);
}
// Panel, backlight and LVGL display driver live in src/display.cpp.
void applyDisplay() {
    Display::Options options;
    options.bgr=settings.bgr; options.inverted=settings.inverted; options.brightness=settings.brightness;
    Display::apply(options);
    Touch::setRotationMode(settings.touchMode);
}
bool deskReady = false;
// The Touch module owns hardware access and gesture recognition; navigation stays
// here so page/app state remains in the UI layer.
void back();  // defined below; the gesture router is the only early user
// V2: one route for every swipe. The pure decision table (desk_logic.h
// gestureIntent) owns thresholds/edges; this function only executes its result.
void onTouchGesture(const Touch::GestureInfo &g) {
    if(Ui::batteryNoticeVisible())return;
    // Games own their touch sequences, including the left edge. Preserve the
    // LVGL release for game input; exit via each game's Back button.
    if (app == App::Snake || app == App::Jump || app == App::Sheep || app == App::Miner || app == App::Plane || app == App::Breakout || app == App::Mines || app == App::Pin || app == App::Memory) return;
    // A swipe must not also act as a click on whatever widget sits under the
    // release point. LVGL owns click cancellation for scrolling widgets; only
    // non-scroll swipes need wait_release(), which must not orphan a scroll throw.
    Touch::suppressSwipeClick(touchDevice, g.dx, g.dy);
    if (Display::transitioning()) return;
    if (g.kind != Touch::GestureSwipe) return;
    const GestureIntent intent = gestureIntent(
        g.x0, g.y0, g.dx, g.dy,
        GestureContext{Display::transitioning(), GiftUI::inputActive(), app != App::None, GiftPages::canGoBack(), app == App::Snake || app == App::Jump || app == App::Sheep || app == App::Miner || app == App::Plane || app == App::Breakout || app == App::Mines || app == App::Pin || app == App::Memory});
    // The Games app has its own paged launcher. Keep edge-back first; ordinary
    // horizontal swipes turn its sheets without navigating the desktop pages.
    const int ax = g.dx < 0 ? -g.dx : g.dx, ay = g.dy < 0 ? -g.dy : g.dy;
    if (app == App::Games && intent != GestureIntent::Back && !GiftUI::inputActive() &&
        ax > 45 && ax > ay * 3 / 2 && GiftUI::swipeApps(g.dx)) return;
    if ((intent==GestureIntent::PageNext||intent==GestureIntent::PagePrev) && GiftUI::swipeApps(g.dx)) return;
    switch (intent) {
        case GestureIntent::Back: back(); break;
        case GestureIntent::PageNext:
            // Root-page fallthrough lands here too: the edge gesture is never eaten.
            GiftUI::reset(); page = DeskLogic::nextPage(page, 1); queueRender(); break;
        case GestureIntent::PagePrev:
            GiftUI::reset(); page = DeskLogic::nextPage(page, -1); queueRender(); break;
        case GestureIntent::None: break;
    }
}
bool swiped() { return Touch::swiped(); }
// Rate-limited OOM feedback: allocating on every loop iteration would otherwise
// rebuild the whole view through showToast() at loop rate.
void reportOutOfMemory() {
    static uint32_t lastNoticeAt=0; const uint32_t now=millis();
    if (lastNoticeAt && (int32_t)(now-lastNoticeAt)<5000) return;
    lastNoticeAt=now; notifyUser("内存不足");
}
// Same check as net.cpp/gift_data.cpp for portal-supplied text that this file
// stores itself (city name). Whitespace is layout, every other codepoint must be
// in the F-d font coverage or the panel would draw a tofu box.
bool glyphsCovered(const char *utf8) {
    if (!utf8) return true;
    for (const unsigned char *p=(const unsigned char*)utf8; *p;) {
        uint32_t cp; const unsigned char c=*p++;
        if (c<0x80) cp=c;
        else if ((c&0xE0)==0xC0) { if ((*p&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x1F)<<6)|(*p++&0x3F); }
        else if ((c&0xF0)==0xE0) { if ((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x0F)<<12)|((uint32_t)(p[0]&0x3F)<<6)|(p[1]&0x3F); p+=2; }
        else if ((c&0xF8)==0xF0) { if ((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80||(p[2]&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x07)<<18)|((uint32_t)(p[0]&0x3F)<<12)|((uint32_t)(p[1]&0x3F)<<6)|(p[2]&0x3F); p+=3; }
        else return false;
        if (cp=='\n'||cp=='\r'||cp=='\t') continue;
        if (!GiftCoverage::has(cp)) return false;
    }
    return true;
}

bool submitJob(Job *job) { return Jobs::submit(job); }
void requestWeather(bool manual=false) {
    if (weatherPending || !settings.hasLocation || settings.city.isEmpty() || WiFi.status()!=WL_CONNECTED || !timeValid()) return;
    if (manual && weatherAt && millis()-weatherAt<15000) return;
    auto *j=new Job;
    if (!j) { reportOutOfMemory(); return; }
    j->kind=JobKind::Weather; j->text=settings.city; j->generation=weatherGeneration;
    weatherPending=submitJob(j); if (weatherPending) weatherAt=millis();
}
void requestReading(const char *manualKind=nullptr){
 if(readingPending||WiFi.status()!=WL_CONNECTED||!timeValid()||(!manualKind&&!(GiftData::get("reading")["autoUpdate"]|true)))return;
 if(readingAt&&millis()-readingAt<(manualKind?15000:60000))return;
 if(!manualKind){static uint32_t checkAt=0;uint32_t checkNow=millis();if(checkAt&&checkNow-checkAt<60000)return;checkAt=checkNow;}
 JsonDocument options;String today=GiftData::today();options["date"]=today;bool needed=false;
 for(const char *kind:{"quote","poem"}){String mode=String(kind)+"Mode";auto cache=GiftData::get("readingCache")[kind];bool fetch=GiftData::get("reading")[mode]=="api"&&(manualKind?!strcmp(kind,manualKind):(cache["date"]!=today||(!strcmp(kind,"quote")&&cache["provider"]!="uapis")));options[kind]=fetch;needed|=fetch;}
 if(!needed)return;auto *j=new Job;if(!j){reportOutOfMemory();return;}j->kind=JobKind::Reading;serializeJson(options,j->text);readingPending=submitJob(j);readingAt=millis();readingDay=today;
}
// urlEncode / number / formatDate live in src/text_util.h (namespace TextUtil).
// Weather and city-search requests live in src/net.cpp.
void ioWorker(void *) {
    while (true) {
        Job *job=nullptr; xQueueReceive(jobQueue,&job,portMAX_DELAY);
        if (!job) continue;
        auto *r=new Result;
        if (!r) {
            // Without a Result there is nothing to enqueue; flag the loop thread so
            // it clears the pending flags and tells the user instead of hanging.
            resultOutOfMemory.store(true);
            delete job;
            continue;
        }
        r->kind=job->kind;
        xSemaphoreTake(ioMutex,portMAX_DELAY);
        switch (job->kind) {
            case JobKind::Weather: Net::fetchWeather(*job,*r); break;
            case JobKind::Reading: Net::fetchReading(*job,*r); break;
        }
        xSemaphoreGive(ioMutex);
        delete job;
        xQueueSend(resultQueue,&r,portMAX_DELAY);
    }
}
void consumeResults() {
    if (resultOutOfMemory.exchange(false)) { weatherPending=false; readingPending=false; reportOutOfMemory(); }
    Result *r;
    while (xQueueReceive(resultQueue,&r,0)==pdTRUE) {
        switch (r->kind) {
            case JobKind::Reading: {
                readingPending=false;
                readingError=r->error;
                // Rejected network text keeps the previous cache/example on screen;
                // tell the user once per few minutes instead of every retry.
                if(readingError.indexOf("生僻字")>=0&&(readingNoticeAt==0||millis()-readingNoticeAt>300000)){readingNoticeAt=millis();notifyUser("阅读内容含生僻字，已保留旧内容",true);}
                if(r->success&&readingDay==GiftData::today()){JsonDocument cache;if(!deserializeJson(cache,r->text))for(JsonPair item:cache.as<JsonObject>())GiftData::readingCache(item.key().c_str(),item.value());}
                break;
            }
            case JobKind::Weather:
                weatherPending=false;
                if (r->generation==weatherGeneration && r->weather.query==settings.city) {
                    if (r->success) {weather=r->weather;WeatherPage::storeCache(weather);weatherFailures=0;weatherRetryDelay=WeatherRetryMs;}
                    else {
                        weather.error=r->error;
                        if(weatherFailures<10)++weatherFailures;
                        weatherRetryDelay=RetryPolicy::exponential(WeatherRetryMs,WeatherMs,weatherFailures);
                    }
                    liveRefreshPending=true;
                } else { weatherAt=0; }
                break;
        }
        delete r;
    }
}
void back() {
    // 1) Page details / legacy sections (Reading/Daily/Home/Weather modules).
    if(GiftUI::back())return;
    // 2) Wi-Fi keyboard layer: editing -> list; the list falls through to the App.
    if(WifiSetup::canBack()&&WifiSetup::back())return;
    // 3) Root page: back is a no-op (no more forced jump to page 6).
    if(app==App::None)return;
    // 4) Calendar confirmation page: the calendar flow's own sub-state, not a
    // navigation layer. It returns to the calendar view it belongs to and never
    // re-records an origin (that would ping-pong Calendar <-> Countdown).
    if(app==App::Countdown&&calendarConfirm){calendarConfirm=false;selectedDate={};app=App::Calendar;pendingBackDirection=-1;lastInputAt=millis();queueRender();return;}
    // 5) App -> recorded origin; unresolvable or self-loop falls back to page 6.
    int originApp=-1,originPage=0;
    const bool resolved=navResolve(navHistory,(int)app,&originApp,&originPage);
    if(app==App::Voice)XiaozhiVoice::close();
    if(!resolved||originApp==(int)app){app=App::None;page=6;}
    else{app=(App)originApp;page=(uint8_t)originPage;}
    if(app==App::Countdown){calendarConfirm=false;countdownError="";}
    // Only an App -> App return needs the reverse reveal; App -> page already
    // renders with direction -1. render() consumes and clears this unconditionally.
    if(app!=App::None&&originApp>=0)pendingBackDirection=-1;
    lastInputAt=millis();queueRender();
}
// The built-in UI's widgets and palette live in src/ui_widgets.cpp. The signatures
// are unchanged, so existing call sites resolve through this using-directive.
using namespace Ui;

lv_obj_t *calendarPanel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    auto *object = panel(parent, x, y, w, h, color);
    lv_obj_set_style_radius(object, radius, 0);
    return object;
}

void showToast(const String &value, bool error = false) {
    toastText = value;
    toastError = error;
    toastUntil = millis() + 1800;
    queueRender();
}

void renderToast() {
    toast = nullptr;
    if (toastText.isEmpty() || (int32_t)(millis() - toastUntil) >= 0) return;
    auto *box = calendarPanel(activeLayer ? activeLayer : root, 64, 210, 192, 24, toastError ? 0xA56A4E : 0x2E5740, 12);
    auto *caption = label(box, toastText, 5, 3, 182, saved_gift_font_bold_10_0, 0xFFFDF4);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    toast = box;
    Ui::clearOnDelete(toast,&toast);
}

int calendarWeekday(int year, int month, int day) {
    int64_t n = DeskLogic::civilDays(year, month, day);
    return (int)((n + 4) % 7 + 7) % 7;  // Sunday = 0
}

String calendarDateText(const lv_calendar_date_t &date) {
    static const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};
    return String((int)date.month) + " 月 " + String((int)date.day) + " 日 · 周" +
           week[calendarWeekday(date.year, date.month, date.day)];
}

void renderCalendarConfirm() {
    lv_obj_t *root = pageLayer;   // pages build in the permanent page layer
    header("确认这一天", true, 52, 12);
    String date = String((int)selectedDate.month) + " 月 " + String((int)selectedDate.day) + " 日";
    auto *dateLabel = label(root, date, 12, 42, 296, saved_gift_yahei_b_22_0, Ink);
    lv_obj_set_style_text_align(dateLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(dateLabel, 1, 0);

    static const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};
    String weekday = String("星期") + week[calendarWeekday(selectedDate.year, selectedDate.month, selectedDate.day)];
    auto *weekLabel = label(root, weekday, 12, 71, 296, saved_gift_font_bold_10_0, Muted);
    lv_obj_set_style_text_align(weekLabel, LV_TEXT_ALIGN_CENTER, 0);

    calendarPanel(root, 139, 91, 17, 1, 0xD8B183, 0);
    calendarPanel(root, 158, 89, 4, 4, CalendarTarget, 2);
    calendarPanel(root, 165, 91, 17, 1, 0xD8B183, 0);

    auto *replaceLabel = label(root, settings.hasTarget ? "将替换现在的期待" : "把这一天留作期待",
                               12, 101, 296, saved_gift_font_bold_10_0, Muted);
    lv_obj_set_style_text_align(replaceLabel, LV_TEXT_ALIGN_CENTER, 0);

    if (settings.hasTarget) {
        auto d = DeskLogic::beijingDate(settings.target);
        String name = settings.countdownName.isEmpty() ? String("期待的日子") : settings.countdownName;
        String target = name + " · " + String((int)d.month) + " 月 " + String((int)d.day) + " 日";
        auto *targetLabel = label(root, target, 12, 116, 296, saved_gift_font_bold_10_0, 0x765737);
        lv_obj_set_style_text_align(targetLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(targetLabel, LV_LABEL_LONG_DOT);
    } else {
        auto *empty = label(root, "会显示在首页和倒计时页", 12, 116, 296, saved_gift_font_bold_10_0, Muted);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }

    auto *confirm = button(root, "确认设为期待", 24, 138, 272, 36, SetCountdown, 0x2E5740);
    auto *confirmCaption = lv_obj_get_child(confirm, 0);
    lv_obj_set_style_text_color(confirmCaption, lv_color_hex(0xFFFDF4), 0);

    auto *phone = button(root, "手机补充名称和时间", 70, 185, 180, 22, OpenPhone, Paper);
    lv_obj_set_style_bg_opa(phone, LV_OPA_TRANSP, 0);
    auto *phoneCaption = lv_obj_get_child(phone, 0);
    lv_obj_set_style_text_font(phoneCaption, saved_gift_font_bold_10_0, 0);
    lv_obj_set_style_text_color(phoneCaption, lv_color_hex(0x5C7860), 0);

    if (settings.hasTarget) {
        auto *clear = button(root, "清除当前目标", 92, 206, 136, 20, ClearCountdown, Paper);
        lv_obj_set_style_bg_opa(clear, LV_OPA_TRANSP, 0);
        auto *clearCaption = lv_obj_get_child(clear, 0);
        lv_obj_set_style_text_font(clearCaption, saved_gift_font_bold_10_0, 0);
        lv_obj_set_style_text_color(clearCaption, lv_color_hex(0xA56A4E), 0);
    }

    if (!countdownError.isEmpty()) {
        auto *error = label(root, countdownError, 12, 227, 296, saved_gift_font_bold_8_0, 0xA56A4E);
        lv_obj_set_style_text_align(error, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(error, LV_LABEL_LONG_DOT);
    }
}

void renderCountdown() {
    lv_obj_t *root = pageLayer;   // pages build in the permanent page layer
    if (app==App::Countdown&&calendarConfirm) { renderCalendarConfirm(); return; }
    header("倒计时",true,52,12);
    auto *card=panel(root,14,49,292,125,0xE3ECD9,14);
    auto *name=label(card,settings.hasTarget?settings.countdownName:"把期待装进日子里",12,10,268,saved_gift_font_bold_14_0,0x294C3D);
    lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
    countLabel=label(card,"未设置",12,44,268,saved_gift_font_bold_32_0,0x294C3D);
    if (settings.hasTarget) label(card,formatDate(settings.target,"%Y-%m-%d %H:%M"),12,104,268,saved_gift_font_bold_14_0,Muted);
    countDetail=label(root,"在日历中选日期，或用手机设置",18,179,286,saved_gift_font_bold_14_0,Muted);
    button(root,"选择日期",16,199,136,29,OpenCalendar); button(root,"手机设置",166,199,138,29,OpenPhone);
}
bool sameCalendarDate(const lv_calendar_date_t &date, int year, int month, int day) {
    return date.year == year && date.month == month && date.day == day;
}

void styleCalendarCells() {
    lv_calendar_date_t today = {};
    if (timeValid()) {
        auto d = DeskLogic::beijingDate((int64_t)time(nullptr));
        today = {(uint16_t)d.year, (int8_t)d.month, (int8_t)d.day};
    }
    lv_calendar_date_t target = {};
    if (settings.hasTarget) {
        auto d = DeskLogic::beijingDate(settings.target);
        target = {(uint16_t)d.year, (int8_t)d.month, (int8_t)d.day};
    }

    for (auto &cell : calendarCells) {
        if (!cell.hit) continue;
        bool selected = calendarSelectionActive && sameCalendarDate(selectedDate, cell.year, cell.month, cell.day);
        bool isToday = today.year && sameCalendarDate(today, cell.year, cell.month, cell.day);
        bool isTarget = settings.hasTarget && sameCalendarDate(target, cell.year, cell.month, cell.day);

        if (selected) lv_obj_clear_flag(cell.pill, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cell.pill, LV_OBJ_FLAG_HIDDEN);
        if (isToday) lv_obj_clear_flag(cell.ring, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cell.ring, LV_OBJ_FLAG_HIDDEN);
        if (isTarget) lv_obj_clear_flag(cell.dot, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(cell.dot, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_style_text_font(cell.number, cell.current ? &lv_font_montserrat_12 : &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(cell.number, lv_color_hex(cell.current ? CalendarDayInk : CalendarMuted), 0);
    }

    if (calendarBar) {
        if (calendarSelectionActive) {
            lv_obj_clear_flag(calendarBar, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(calendarBarDate, calendarDateText(selectedDate).c_str());
        } else {
            lv_obj_add_flag(calendarBar, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void clearCalendarSelection() {
    calendarSelectionActive = false;
    selectedDate = {};
    styleCalendarCells();
}

void calendarDayEvent(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    int index = (int)(uintptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= 42) return;
    CalendarCell &cell = calendarCells[index];
    if (!cell.current) return;
    if (calendarSelectionActive && sameCalendarDate(selectedDate, cell.year, cell.month, cell.day)) {
        clearCalendarSelection();
    } else {
        selectedDate = {(uint16_t)cell.year, (int8_t)cell.month, (int8_t)cell.day};
        calendarSelectionActive = true;
        styleCalendarCells();
    }
    lastInputAt = millis();
}

void renderCalendar() {
    lv_obj_t *root = pageLayer;   // pages build in the permanent page layer
    header("日历", true, 52, 12, true);
    calendarConfirm = false;
    for (auto &cell : calendarCells) cell = CalendarCell{};
    calendarBar = nullptr;
    calendarBarDate = nullptr;
    bool validSelection = DeskLogic::validDate(selectedDate.year, selectedDate.month, selectedDate.day);
    bool selectionIsToday = false;
    if (timeValid()) {
        auto nowDate = DeskLogic::beijingDate((int64_t)time(nullptr));
        selectionIsToday = sameCalendarDate(selectedDate, nowDate.year, nowDate.month, nowDate.day);
    }
    calendarSelectionActive = validSelection && !selectionIsToday;
    if (!calendarSelectionActive) selectedDate = {};

    auto *prev = button(root, LV_SYMBOL_LEFT, 8, 38, 32, 26, CalendarPrev, Paper);
    lv_obj_set_style_bg_opa(prev, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(prev, 0), lv_color_hex(Ink), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(prev, 0), &lv_font_montserrat_20, 0);
    calendarTitle = label(root, "", 44, 43, 104, saved_gift_font_bold_14_0, Ink);
    lv_label_set_text_fmt(calendarTitle, "%d 年 %d 月", calendarYear, calendarMonth);
    auto *next = button(root, LV_SYMBOL_RIGHT, 152, 38, 32, 26, CalendarNext, Paper);
    lv_obj_set_style_bg_opa(next, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(next, 0), lv_color_hex(Ink), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(next, 0), &lv_font_montserrat_20, 0);
    auto *todayButton = button(root, timeValid() ? "今天" : "未校时", 244, 38, 58, 26, CalendarToday, 0xE8EFE1);
    lv_obj_set_style_radius(todayButton, 11, 0);
    auto *todayCaption = lv_obj_get_child(todayButton, 0);
    lv_obj_set_style_text_font(todayCaption, saved_gift_font_bold_10_0, 0);
    lv_obj_set_style_text_color(todayCaption, lv_color_hex(0x41664F), 0);
    if (!timeValid()) {
        lv_obj_add_state(todayButton, LV_STATE_DISABLED);
        lv_obj_set_style_text_color(todayCaption, lv_color_hex(Muted), 0);
    }

    static const char *weekdays[] = {"一", "二", "三", "四", "五", "六", "日"};
    for (int i = 0; i < 7; ++i) {
        auto *caption = label(root, weekdays[i], (int)(10 + (300.0 / 7) * i), 70, 42, saved_gift_font_bold_10_0, Muted);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    }
    calendarPanel(root, 12, 84, 296, 1, CalendarLine, 0);

    calendar = lv_obj_create(root);
    lv_obj_set_pos(calendar, 10, 88);
    lv_obj_set_size(calendar, 300, 120);
    lv_obj_set_style_bg_opa(calendar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calendar, 0, 0);
    lv_obj_set_style_pad_all(calendar, 0, 0);
    lv_obj_clear_flag(calendar, LV_OBJ_FLAG_SCROLLABLE);

    const int days = DeskLogic::daysInMonth(calendarYear, calendarMonth);
    const int offset = ((int)(DeskLogic::civilDays(calendarYear, calendarMonth, 1) + 3) % 7 + 7) % 7;
    for (int index = 0; index < 42; ++index) {
        int row = index / 7, col = index % 7;
        int day = index - offset + 1;
        int year = calendarYear, month = calendarMonth;
        bool current = day >= 1 && day <= days;
        if (day < 1) {
            month--;
            if (month < 1) { month = 12; year--; }
            day += DeskLogic::daysInMonth(year, month);
        } else if (day > days) {
            day -= days;
            month++;
            if (month > 12) { month = 1; year++; }
        }

        CalendarCell &cell = calendarCells[index];
        cell.year = (uint16_t)year;
        cell.month = (uint8_t)month;
        cell.day = (uint8_t)day;
        cell.current = current;

        int x = (int)(col * (300.0 / 7) + ((300.0 / 7) - 40) / 2);
        cell.hit = calendarPanel(calendar, x, row * 20, 40, 20, 0xFFFFFF, 9);
        lv_obj_set_style_bg_opa(cell.hit, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell.hit, 0, 0);
        lv_obj_set_style_pad_all(cell.hit, 0, 0);
        lv_obj_clear_flag(cell.hit, LV_OBJ_FLAG_SCROLLABLE);

        cell.pill = calendarPanel(cell.hit, 5, 0, 30, 19, CalendarSelect, 9);
        lv_obj_set_style_border_width(cell.pill, 1, 0);
        lv_obj_set_style_border_color(cell.pill, lv_color_hex(CalendarSelectLine), 0);
        lv_obj_add_flag(cell.pill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(cell.pill, LV_OBJ_FLAG_CLICKABLE);

        cell.ring = calendarPanel(cell.hit, 7, 0, 26, 19, 0xFFFFFF, 9);
        lv_obj_set_style_bg_opa(cell.ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell.ring, 1, 0);
        lv_obj_set_style_border_color(cell.ring, lv_color_hex(CalendarGreen), 0);
        lv_obj_add_flag(cell.ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(cell.ring, LV_OBJ_FLAG_CLICKABLE);

        char buffer[4];
        snprintf(buffer, sizeof(buffer), "%d", day);
        cell.number = label(cell.hit, buffer, 0, 0, 40,
                            current ? &lv_font_montserrat_12 : &lv_font_montserrat_10,
                            current ? CalendarDayInk : CalendarMuted);
        lv_obj_set_style_text_align(cell.number, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(cell.number, LV_ALIGN_CENTER, 0, -2);
        lv_obj_clear_flag(cell.number, LV_OBJ_FLAG_CLICKABLE);

        cell.dot = calendarPanel(cell.hit, 18, 15, 4, 4, CalendarTarget, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(cell.dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(cell.dot, LV_OBJ_FLAG_CLICKABLE);

        if (current) {
            lv_obj_add_flag(cell.hit, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(cell.hit, calendarDayEvent, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        }
    }

    calendarBar = calendarPanel(root, 10, 211, 300, 27, 0xFFFFFF, 12);
    lv_obj_set_style_border_width(calendarBar, 1, 0);
    lv_obj_set_style_border_color(calendarBar, lv_color_hex(0xE3E8DC), 0);
    lv_obj_set_style_shadow_width(calendarBar, 8, 0);
    lv_obj_set_style_shadow_opa(calendarBar, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(calendarBar, 2, 0);
    lv_obj_clear_flag(calendarBar, LV_OBJ_FLAG_CLICKABLE);
    calendarBarDate = label(calendarBar, "", 0, 0, 158, saved_gift_font_bold_10_0, Ink);
    lv_obj_align(calendarBarDate, LV_ALIGN_LEFT_MID, 10, 0);
    auto *cancel = button(calendarBar, "取消", 170, 3, 36, 21, CalendarCancel, 0xFFFFFF);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_TRANSP, 0);
    auto *cancelCaption = lv_obj_get_child(cancel, 0);
    lv_obj_set_style_text_font(cancelCaption, saved_gift_font_bold_10_0, 0);
    lv_obj_set_style_text_color(cancelCaption, lv_color_hex(Muted), 0);
    auto *confirm = button(calendarBar, "设为期待", 208, 3, 92, 21, CalendarConfirm, 0x2E5740);
    lv_obj_set_style_radius(confirm, 9, 0);
    auto *confirmCaption = lv_obj_get_child(confirm, 0);
    lv_obj_set_style_text_font(confirmCaption, saved_gift_font_bold_10_0, 0);
    lv_obj_set_style_text_color(confirmCaption, lv_color_hex(0xFFFDF4), 0);
    lv_obj_add_flag(calendarBar, LV_OBJ_FLAG_HIDDEN);

    styleCalendarCells();
}
void refreshAboutPower() {
    if(!aboutPowerState)return;
    Ui::batteryRefresh(aboutBatteryView);
    auto power=PowerStatus::get();
    String state=PowerStatus::description();
    if(power.voltageValid)state+=" · "+String(power.volts,2)+" V";
    if(state!=lv_label_get_text(aboutPowerState))lv_label_set_text(aboutPowerState,state.c_str());
}

void renderPhone() {
    lv_obj_t *root = pageLayer;
    lv_obj_set_style_bg_color(root,lv_color_hex(0xFBF9F1),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    compactHeader("连接手机");
    panel(root,16,36,288,1,0xE0E5D8,0);
    panel(root,8,44,126,126,0xFFFDF8,10);
    auto *qr=lv_qrcode_create(root,110,lv_color_hex(0x394637),lv_color_white());
    lv_obj_set_pos(qr,16,52);
    String wifiQr="WIFI:T:WPA;S:"+apName+";P:"+portalPassword+";;";
    lv_qrcode_update(qr,wifiQr.c_str(),wifiQr.length());
    label(root,"连接热点",144,50,160,saved_gift_font_bold_8_0,0x619B78);
    label(root,apName,144,63,160,saved_gift_font_bold_13_0,0x394637);
    label(root,"密码 "+portalPassword,144,80,160,saved_gift_font_bold_10_0,0x394637);
    panel(root,144,100,160,1,0xE6E9DF,0);
    label(root,"扫码连接设备热点",144,107,160,saved_gift_font_bold_10_0,0x394637);
    label(root,"连接后自动打开配置",144,120,160,saved_gift_font_bold_8_0,0x82927A);
    label(root,"热点地址",144,140,160,saved_gift_font_bold_8_0,0x82927A);
    label(root,"192.168.4.1",144,151,160,saved_gift_font_bold_14_0,0x619B78);
    label(root,"管理请连接设备热点",144,170,160,saved_gift_font_bold_8_0,0x82927A);
}

void renderAbout() {
    lv_obj_t *root = pageLayer;
    lv_obj_set_style_bg_color(root,lv_color_hex(0xFBF9F1),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    compactHeader("关于");
    panel(root,16,36,288,1,0xE0E5D8,0);

    panel(root,8,42,304,36,0xEDF3E4,10);
    label(root,"给依依的小黑桌面",18,47,280,saved_gift_font_bold_14_0,0x394637);
    label(root,"固件 "+String(Version)+" · LVGL "+String(LVGL_VERSION_MAJOR)+"."+String(LVGL_VERSION_MINOR)+"."+String(LVGL_VERSION_PATCH),18,63,280,saved_gift_font_bold_8_0,0x82927A);

    panel(root,8,82,304,34,0xFFFDF8,10);
    Ui::batteryCreate(aboutBatteryView,root,18,91,94,98.5f);
    lv_obj_set_pos(aboutBatteryView.percent,56,86);
    lv_obj_set_width(aboutBatteryView.percent,120);
    lv_obj_set_style_text_align(aboutBatteryView.percent,LV_TEXT_ALIGN_LEFT,0);
    lv_obj_set_style_text_font(aboutBatteryView.percent,saved_gift_font_bold_14_0,0);
    aboutPowerState=label(root,"",56,102,248,saved_gift_font_bold_8_0,0x82927A);
    label(root,"电量估算",258,88,44,saved_gift_font_bold_7_0,0x82927A);
    refreshAboutPower();

    panel(root,8,122,304,64,0xFFFDF8,10);
    label(root,"设备",18,127,120,saved_gift_font_bold_8_0,0x619B78);
    label(root,"ESP32-S3 / LCDWiki ES3C28P",18,139,280,saved_gift_font_bold_9_0,0x394637);
    label(root,"Flash "+String(ESP.getFlashChipSize()/1048576)+" MB · PSRAM "+String(ESP.getPsramSize()/1048576)+" MB",18,153,280,saved_gift_font_bold_9_0,0x394637);
    label(root,"ILI9341V 320×240 · FT6336G 电容触摸",18,167,280,saved_gift_font_bold_8_0,0x82927A);

    panel(root,8,192,304,42,0xFFFDF8,10);
    label(root,"系统与服务",18,197,280,saved_gift_font_bold_8_0,0x619B78);
    label(root,"NTP 北京时间 · 基于LVGL",18,209,280,saved_gift_font_bold_9_0,0x394637);
    label(root,"天气 UApi · 小智语音 官方 2.5.0",18,222,280,saved_gift_font_bold_8_0,0x82927A);
}

void renderDiagnostics() {
    lv_obj_t *root = pageLayer;   // pages build in the permanent page layer
    header("颜色与触摸",true,52,12,true);
    const uint32_t colors[]={0xFF0000,0x00FF00,0x0000FF,0xFFFFFF,0x000000};
    const char *names[]={"红","绿","蓝","白","黑"};
    for (int i=0;i<5;++i) {
        auto *block=panel(root,10+61*i,44,56,38,colors[i]);
        label(block,names[i],16,10,28,saved_gift_font_bold_14_0,i==4?0xFFFFFF:0x000000);
    }
    touchLabel=label(root,"触摸屏幕四角，检查坐标",14,91,292);
    Touch::setCoordinateLabel(touchLabel);
    button(root,settings.bgr?"红蓝：默认":"红蓝：交换",12,124,142,34,ToggleBgr);
    button(root,settings.inverted?"IPS 反色：开":"IPS 反色：关",166,124,142,34,ToggleInvert);
    button(root,"触摸方向："+String(settings.touchMode),12,171,296,34,ChangeTouch);
    label(root,"横屏范围：x 0–319，y 0–239",14,215,292,saved_gift_font_bold_14_0,Muted);
}
void render() {
    // Consume the back-direction request first: every render clears it, whether or
    // not a transition actually starts, so it can never leak into an unrelated view
    // change. App -> page already renders with -1; only App -> App needs the hint.
    const int backDirection=pendingBackDirection;
    pendingBackDirection=0;
    // Called only from loop(), after LVGL finishes dispatching the current event.
    // Reset a held touch before replacing the page it was pressing.
    if (touchDevice) lv_indev_reset(touchDevice,nullptr);
    if(app!=App::Wifi)WifiSetup::leave();
    clockLabel=dateLabel=weatherLabel=countLabel=countDetail=networkLabel=networkCross=touchLabel=calendar=calendarTitle=nullptr;
    calendarBar=calendarBarDate=nullptr;
    aboutPowerState=nullptr;Ui::batteryClear(aboutBatteryView);
    Touch::setCoordinateLabel(nullptr);
    // The footer highlights the current page; this is the single point where the
    // widget layer learns which page is being built.
    Ui::setPage(page);
    // A generated designer screen may own this view. It switches the active screen
    // itself, so the built-in renderer must be skipped completely rather than
    // cleaning a screen it no longer owns.
    const int slot=Screens::slotFor(page,(int)app);
    if (slot>=0 && Screens::dispatch(slot)){renderedView="";cachedHomeShown=false;renderPending=false;renderedPage=page;renderedApp=app;return;}
    Screens::activateBuiltIn(root);
    ensureLayers();
    const bool home=isHomeView();
    lv_obj_t *target=home?homeLayer:pageLayer;
    const String view=GiftUI::viewKey();

    // ---- cached home: layer switch + pointer refresh only -----------------
    // This is the P1-3 fast path: no clean/build/invalidate of the whole tree
    // and no reveal animation, so renderMs measures only the layer switch and
    // the (1 Hz throttled) data refresh.
    if(home&&homeCached&&homeRefs.valid){
        uint32_t phase=micros();
        showLayer(homeLayer);
        Ui::setRoot(homeLayer);
        GiftUI::leaveView();
        Ui::clearStatusCluster();
        // Force one refresh: the user may return after changing the city/weather.
        GiftUI::refreshHome(homeRefs,true);
        mirrorBackground(homeLayer);
        // A toast is a transient child of the visible layer. No clean happens on
        // this path, so delete the previous toast explicitly before re-drawing.
        if(toast){lv_obj_del(toast);toast=nullptr;}
        renderToast();
        lv_obj_invalidate(homeLayer);
        renderPending=false;cachedHomeShown=true;++homeCacheHits;
        renderedView=view;renderedPage=page;renderedApp=app;
        lastCaptureUs=0;lastBuildUs=0;lastFlushUs=micros()-phase;
        return;
    }

    int distance=(int(page)-int(renderedPage)+7)%7;
    int direction=app==App::None&&renderedApp==App::None?(distance<=3?1:-1):(app==App::None?-1:1);
    // back() asked for a reverse reveal App -> App; App -> page is already -1 above.
    if(backDirection!=0&&app!=App::None&&renderedApp!=App::None)direction=backDirection;
    uint32_t phase=micros();
    // Leaving home / switching pages still uses the narrow-band reveal; only the
    // cached return to home (above) is instantaneous.
    const bool transition=!renderedView.isEmpty()&&view!=renderedView&&Display::beginTransition(direction);
    lastCaptureUs=micros()-phase;
    if(transition)Touch::setBlocked(true);
    phase=micros();
    showLayer(target);
    Ui::setRoot(target);
    // The name table would otherwise keep pointers to children this call frees.
    // The toast is a child of whichever layer is visible; delete it before the
    // clean so it can never be orphaned in the other layer.
    if(toast){lv_obj_del(toast);toast=nullptr;}
    SavedDesign::invalidate();
    Ui::clearStatusCluster();
    GiftUI::leaveView();
    lv_obj_clean(target);
    lv_obj_set_style_bg_color(target,lv_color_hex(settings.white?0xFFFFFF:Paper),0);
    if(home){
        // First home build: create the permanent cache exactly once.
        GiftUI::buildHome(homeLayer,homeRefs);
        homeCached=GiftUI::homeCached()&&homeRefs.valid;
        lv_obj_invalidate(homeLayer);
    } else if(GiftUI::render(target)){lv_obj_invalidate(target);}
    else if (app==App::None) {
        label(target,"界面未就绪",12,80,296);
    } else {
        switch (app) {
            case App::Calendar: renderCalendar(); break;
            case App::Countdown: renderCountdown(); break;
            case App::Phone: renderPhone(); break; case App::About: renderAbout(); break;
            case App::Diagnostics: renderDiagnostics(); break;
            case App::Wifi: header("Wi-Fi 配网",true); WifiSetup::render(target); break;
            case App::Voice: header("小智",true); label(target,"正在打开官方小智…",22,68,276); break;
            default: break;
        }
    }
    lastBuildUs=micros()-phase;
    phase=micros();
    // Populate live labels before the first flush: the reveal must never show the
    // design file's placeholder date/city for one refresh interval. The first
    // home build already filled its data in buildHome().
    if(!home)GiftUI::refresh();
    renderToast();
    mirrorBackground(target);
    lv_obj_invalidate(target); renderPending=false;
    renderedView=view;renderedPage=page;renderedApp=app;
    // Remember whether the visible layer is the cached home so deskLoop routes
    // refresh() to refreshHome(). First home build also lands here.
    cachedHomeShown=home;
    // 40 MHz SPI: keep the historical 200/180 ms reveal duration and 16 px edge.
    // A 150/140 ms + wider-edge + 16 ms-refresh experiment was measured with
    // status.transitionFrames (2-5 bands either way) and reverted: it did not
    // add frames, only changed the blend cost.
    if(transition)Display::startTransition(app==App::None?200:180);
    lastFlushUs=micros()-phase;
}

void actionEvent(lv_event_t *event) {
    if (swiped()) return;
    lastInputAt=millis(); Action action=(Action)(uintptr_t)lv_event_get_user_data(event);
    switch (action) {
        case Back: back(); break;
        case OpenMessages: GiftPages::openPage(5); break;
        case OpenWeather: GiftPages::openApp((int)App::Weather); requestWeather(); break; case OpenVoice: GiftPages::openApp((int)App::Voice); break;
        case OpenCalendar: {
            if (timeValid()) { struct tm t; time_t now=time(nullptr); localtime_r(&now,&t); calendarYear=t.tm_year+1900; calendarMonth=t.tm_mon+1; }
            calendarConfirm=false; calendarSelectionActive=false; selectedDate={};
            GiftPages::openApp((int)App::Calendar); break;
        }
        case OpenSettings: GiftPages::openApp((int)App::Settings); break; case OpenPhone: GiftPages::openApp((int)App::Phone); break;
        case OpenWifi: GiftPages::openApp((int)App::Wifi); WifiSetup::requestScan(); break;
        case OpenAbout: GiftPages::openApp((int)App::About); break; case OpenDiagnostics: GiftPages::openApp((int)App::Diagnostics); break;
        case RefreshWeather: requestWeather(true); queueRender(); break;
        case RefreshQuote: requestReading("quote");break;case RefreshPoem: requestReading("poem");break;
        case CalendarPrev: if (calendarYear>1900 || calendarMonth>1) { if (calendarMonth==1) { --calendarYear; calendarMonth=12; } else --calendarMonth; clearCalendarSelection(); queueRender(); } break;
        case CalendarNext: if (calendarYear<2099 || calendarMonth<12) { if (calendarMonth==12) { ++calendarYear; calendarMonth=1; } else ++calendarMonth; clearCalendarSelection(); queueRender(); } break;
        case CalendarToday: if (timeValid()) { struct tm t; time_t now=time(nullptr); localtime_r(&now,&t); calendarYear=t.tm_year+1900; calendarMonth=t.tm_mon+1; clearCalendarSelection(); queueRender(); } break;
        case CalendarCancel: clearCalendarSelection(); break;
        case CalendarConfirm:
            if (DeskLogic::validDate(selectedDate.year,selectedDate.month,selectedDate.day)) {
                // The confirm view is the calendar flow's sub-state, not a new App
                // layer. Switching to App::Countdown here must NOT record an origin:
                // that would overwrite the Countdown origin and let back() ping-pong
                // Calendar <-> Countdown. back() special-cases this sub-state.
                countdownError="";
                app=App::Countdown;
                calendarConfirm=true;
                lastInputAt=millis();
                queueRender();
            }
            break;
        case SetCountdown: {
            if (!DeskLogic::validDate(selectedDate.year,selectedDate.month,selectedDate.day)) break;
            JsonDocument patch;patch["countdown"]["name"]=settings.countdownName.isEmpty()?"期待的日子":settings.countdownName;
            char date[24];snprintf(date,sizeof(date),"%04d-%02d-%02dT00:00",selectedDate.year,selectedDate.month,selectedDate.day);patch["countdown"]["date"]=date;String error;
            if(GiftData::patch(patch.as<JsonVariantConst>(),error)){
                changed();countdownError="";
                if(calendarConfirm){
                    // Confirmation success is a state transition, not a new entry:
                    // writing app=Calendar keeps the origin Calendar was opened with.
                    // Calling openApp() here would record Calendar<-Countdown and the
                    // next back press would return into the Countdown confirm flow.
                    calendarConfirm=false;selectedDate={};app=App::Calendar;
                    showToast("已设为期待");
                } else queueRender();
            } else {
                countdownError=error;
                queueRender();
            }
            break;
        }
        case ClearCountdown: {
            JsonDocument patch;patch["countdown"]=nullptr;String error;
            if(GiftData::patch(patch.as<JsonVariantConst>(),error)){
                changed();countdownError="";
                if(calendarConfirm){
                    // Same state transition as SetCountdown: keep Calendar's origin.
                    calendarConfirm=false;selectedDate={};app=App::Calendar;
                    showToast("已清除期待");
                } else queueRender();
            } else {
                countdownError=error;
                queueRender();
            }
            break;
        }
        case ToggleBgr: settings.bgr=!settings.bgr; changed(); applyDisplay(); queueRender(); break;
        case ToggleInvert: settings.inverted=!settings.inverted; changed(); applyDisplay(); queueRender(); break;
        case ChangeTouch: settings.touchMode=(settings.touchMode+1)%4; changed(); applyDisplay(); queueRender(); break;
        case ToggleWhite: settings.white=!settings.white; changed(); queueRender(); break;
        default: break;
    }
}
void updateLiveLabels(bool force=false) {
    // The critical battery blink is refreshed independently of the 1 Hz labels.
    if(app==App::About)refreshAboutPower();
    static int64_t previous=-1; int64_t now=time(nullptr);
    if (now==previous && !force) return; previous=now;
    if (toast && (int32_t)(millis()-toastUntil)>=0) {
        // The cached home layer is never cleaned, so delete the expired toast
        // there; on the rebuilding page layer hiding it is enough (the next
        // render cleans the layer).
        if (cachedHomeShown) lv_obj_del(toast);
        else lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
        toast=nullptr;
        toastText="";
    }
    static String calendarDay;String currentDay=GiftData::today();
    if(calendar && calendarDay!=currentDay && app==App::Calendar) queueRender();
    calendarDay=currentDay;
    if (clockLabel) lv_label_set_text(clockLabel,timeValid()?formatDate(now,"%H:%M").c_str():"--:--");
    if (dateLabel && timeValid()) {
        struct tm t; time_t value=now; localtime_r(&value,&t);
        const char *week[]={"日","一","二","三","四","五","六"};
        lv_label_set_text_fmt(dateLabel,"%d月%d日  星期%s",t.tm_mon+1,t.tm_mday,week[t.tm_wday]);
    }
    if (countLabel) {
        if (!settings.hasTarget) { lv_obj_set_style_text_font(countLabel,saved_gift_font_bold_32_0,0); lv_label_set_text(countLabel,"还没有设置目标"); }
        else if (!timeValid()) { lv_obj_set_style_text_font(countLabel,saved_gift_font_bold_32_0,0); lv_label_set_text(countLabel,"请联网校时"); }
        else if (settings.target<=now) { lv_obj_set_style_text_font(countLabel,saved_gift_font_bold_32_0,0); lv_label_set_text(countLabel,"已到达"); }
        else {
            int64_t left=settings.target-now; lv_obj_set_style_text_font(countLabel,saved_gift_font_bold_32_0,0);
            lv_label_set_text_fmt(countLabel,"%lld",(long long)(left/86400));
            lv_label_set_text_fmt(countDetail,"剩余天数 · %02d:%02d:%02d",(int)(left/3600%24),(int)(left/60%60),(int)(left%60));
        }
    }
    if (networkLabel) lv_obj_set_style_text_color(networkLabel,lv_color_hex(WiFi.status()==WL_CONNECTED?Ink:Muted),0);
    if (networkCross) {
        if (WiFi.status()==WL_CONNECTED) lv_obj_add_flag(networkCross,LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(networkCross,LV_OBJ_FLAG_HIDDEN);
    }
}

void respond(JsonDocument &json,int code=200) { String body; serializeJson(json,body); server.send(code,"application/json; charset=utf-8",body); }
void errorResponse(const String &message,int code=400) { JsonDocument json; json["error"]=message; respond(json,code); }
void okResponse(const char *message="已保存") { JsonDocument json; json["message"]=message; respond(json); }
// A full catalog can contain 20 long URLs. Keep both JSON and its response
// buffer in PSRAM so opening the phone page does not exhaust the audio heap.
struct PortalRadioAllocator:ArduinoJson::Allocator {
    void *allocate(size_t n)override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
    void deallocate(void *p)override{free(p);}
    void *reallocate(void *p,size_t n)override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
} portalRadioAllocator;
void radioCatalogResponse() {
    JsonDocument json(&portalRadioAllocator);RadioCatalog::snapshot(json.to<JsonObject>());
    size_t length=measureJson(json);
    char *body=json.overflowed()?nullptr:static_cast<char*>(portalRadioAllocator.allocate(length+1));
    if(!body){errorResponse("电台列表内存不足，请稍后重试",500);return;}
    if(serializeJson(json,body,length+1)!=length){free(body);errorResponse("电台列表读取失败，请重试",500);return;}
    server.sendHeader("Cache-Control","no-store");server.setContentLength(length);
    server.send(200,"application/json; charset=utf-8","");server.sendContent(body,length);free(body);
}
bool requestJson(JsonDocument &json,size_t limit=12000) {
    if (!server.header("Content-Type").startsWith("application/json")) { errorResponse("请使用 JSON 请求",415); return false; }
    const String body=server.arg("plain");
    if (body.length()>limit || deserializeJson(json,body) || !json.is<JsonObject>()) { errorResponse("请求格式错误或内容过长"); return false; }
    return true;
}
// A1 (security): the WebServer binds 0.0.0.0, so once the desk joins a home
// Wi-Fi every LAN host could reach the personal-data APIs (letters, records,
// Wi-Fi credentials). Chosen policy: AP-only. `localIP()` is the device address
// of the accepted connection - softAPIP for clients on the desk's own hotspot,
// the STA address for LAN clients - so the two cases are distinguishable.
bool portalClientAllowed() { return server.client().localIP() == WiFi.softAPIP(); }
bool requirePortalClient() {
    if (portalClientAllowed()) return true;
    server.send(403, "text/plain; charset=utf-8", "仅允许通过设备热点访问，请先连接设备 Wi-Fi");
    return false;
}
void configurePortal() {
    const char *headers[]={"Content-Type"}; server.collectHeaders(headers,1);
    server.on("/",HTTP_GET,[]{if(!requirePortalClient())return;server.send_P(200,"text/html; charset=utf-8",GiftPortalHtml);});
    for(const char *path:CaptivePortal::ProbePaths)server.on(path,HTTP_GET,[]{if(!requirePortalClient())return;server.sendHeader("Location",CaptivePortal::Address,true);server.sendHeader("Cache-Control","no-store");server.send(302,"text/plain","");});
    server.on("/api/gift",HTTP_GET,[]{if(!requirePortalClient())return;JsonDocument config;GiftData::snapshot(config);PowerStatus::poll();PowerStatus::snapshot(config["battery"].to<JsonObject>());config["city"]=settings.city;config["weatherUpdatedAt"]=(int64_t)weather.fetchedAt*1000;config["weatherPending"]=weatherPending;config["weatherError"]=weather.error;config["readingError"]=readingError;config["weatherProvider"]="UApi";config["schedule"]["imported"]=ScheduleStore::imported();config["schedule"]["count"]=ScheduleStore::count();config["schedule"]["termStart"]=ScheduleStore::termStart();if(weather.valid){config["weather"]["temperature"]=weather.temperature;config["weather"]["condition"]=weather.condition;}if(server.hasArg("slim")){config.remove("readingCache");config["slim"]=true;}respond(config);});
    server.on("/api/gift",HTTP_POST,[]{if(!requirePortalClient())return;JsonDocument value;if(!requestJson(value,32000))return;String error;if(!GiftData::patch(value.as<JsonVariantConst>(),error)){errorResponse(error);return;}changed();
        // Content and levels update in place, preserving an open letter's scroll.
        bool layout=value.containsKey("colors")||value.containsKey("holiday")||value["daily"].containsKey("background")||value["daily"].containsKey("cardColor");
        for(const char *key:{"background","quoteColor","quoteInk","poemColor","poemInk"})layout=layout||value["reading"].containsKey(key);
        if(layout)queueRender();okResponse();});
    server.on("/api/countdown-days",HTTP_POST,[]{
        if(!requirePortalClient())return;
        JsonDocument value;if(!requestJson(value,2048))return;
        String error;if(!DaysStore::mutate(value.as<JsonVariantConst>(),error)){errorResponse(error);return;}
        changed();okResponse("倒数日已保存");
    });
    server.on("/api/radio-stations",HTTP_GET,[]{if(!requirePortalClient())return;radioCatalogResponse();});
    server.on("/api/radio-stations",HTTP_POST,[]{
        if(!requirePortalClient())return;
        JsonDocument value(&portalRadioAllocator);if(!requestJson(value,2048))return;
        String error;unsigned status=400;
        if(!RadioCatalog::mutate(value.as<JsonVariantConst>(),error,&status)){errorResponse(error,int(status));return;}
        RadioPlayer::catalogChanged();radioCatalogResponse();
    });
    server.on("/api/schedule",HTTP_POST,[]{if(!requirePortalClient())return;JsonDocument value;if(!requestJson(value,32000))return;if(value["clear"]|false){if(!ScheduleStore::clear()){errorResponse(ScheduleStore::lastError(),500);return;}changed();queueRender();okResponse("已恢复内置课表");return;}String text=value["text"]|"";if(text.length()<10||text.length()>24000){errorResponse("课表内容为空或格式错误");return;}pendingSchedule=text;schedulePending=true;okResponse("课表已保存");});
    server.on("/api/gift-mood",HTTP_POST,[]{if(!requirePortalClient())return;JsonDocument value;if(!requestJson(value))return;String error;auto record=GiftData::record(GiftData::today());if(!value["mood"].is<const char*>()||!GiftData::saveMood(value["mood"],record["note"]|"",error)){errorResponse(error.isEmpty()?"心情格式错误":error);return;}okResponse();});
    server.on("/api/xiaozhi",HTTP_GET,[]{if(!requirePortalClient())return;JsonDocument value;XiaozhiVoice::settings(value);respond(value);});
    server.on("/api/xiaozhi",HTTP_POST,[]{if(!requirePortalClient())return;JsonDocument value;if(!requestJson(value,20000))return;String error;if(!XiaozhiVoice::configure(value.as<JsonVariantConst>(),error)){errorResponse(error);return;}okResponse("小智连接设置已接收，保存结果见小智状态");});
    server.on("/api/wifi",HTTP_POST,[]{
        if(!requirePortalClient())return;
        JsonDocument json; if (!requestJson(json)) return;
        if (!json["ssid"].is<const char*>() || !json["password"].is<const char*>()) { errorResponse("网络名称和密码格式错误"); return; }
        String ssid=json["ssid"].as<String>(), password=json["password"].as<String>();
        if (ssid.isEmpty() || ssid.length()>32 || (!password.isEmpty() && (password.length()<8 || password.length()>63))) { errorResponse("网络名最多 32 字节；密码为 8～63 字节"); return; }
        settings.ssid=ssid; settings.password=password; changed(); reconnectDue=millis()+500;
        okResponse("已保存，设备正在连接 Wi-Fi");
    });
    server.on("/api/location",HTTP_POST,[]{
        if(!requirePortalClient())return;
        JsonDocument json; if (!requestJson(json)) return;
        String city=json["city"] | ""; city.trim(); int length=DeskLogic::utf8Length(city.c_str());
        if (!json["city"].is<const char*>() || length<1 || length>40) { errorResponse("请输入 1～40 字的城市名称，例如杭州"); return; }
        for(const unsigned char *p=(const unsigned char*)city.c_str();*p;++p) if(*p<0x20){errorResponse("城市名称包含控制字符",400);return;}
        if (!glyphsCovered(city.c_str())) { errorResponse("城市名称包含设备字库不支持的生僻字，请修改"); return; }
        settings.city=city; settings.hasLocation=true; ++weatherGeneration;
        weather=Weather{}; weatherAt=0; weatherFailures=0; weatherRetryDelay=WeatherRetryMs; changed(); requestWeather(); queueRender(); okResponse("城市已保存，开发板联网后更新天气");
    });
    server.onNotFound([]{if(!requirePortalClient())return;if(CaptivePortal::redirectUnknown(server.uri().c_str(),server.method()==HTTP_GET)){server.sendHeader("Location",CaptivePortal::Address,true);server.send(302,"text/plain","");}else errorResponse("接口不存在",404);});
    server.begin();
}
void serialCommand(const String &command) {
    lastInputAt=millis();
    if (command=="status") {
        JsonDocument json; json["version"]=Version; json["flash"]=ESP.getFlashChipSize(); json["psram"]=ESP.getPsramSize();
        // V2 baseline: identify exactly which artifact is running, so a device
        // golden run can never be attributed to the wrong .bin (see
        // 审查意见-一轮全量方案-20261028 P1-2).
        json["sketchMd5"]=ESP.getSketchMD5(); json["buildFlavor"]="dev";
        json["uptimeMs"]=millis(); json["resetReason"]=(int)esp_reset_reason();
        json["wifiSleep"]=WiFi.getSleep();
        json["heapFree"]=ESP.getFreeHeap(); json["psramFree"]=ESP.getFreePsram(); json["page"]=page; json["app"]=(int)app;
        json["wifi"]=WiFi.status()==WL_CONNECTED; json["timeValid"]=timeValid();
        // A1 diagnostics: the portal only serves clients on the desk's own SoftAP.
        json["wifiIp"]=WiFi.localIP().toString(); json["apIp"]=WiFi.softAPIP().toString(); json["portalPolicy"]="ap-only";
        // A scan temporarily drops the SoftAP; these expose whether it came back.
        json["apHealthy"]=WifiSetup::accessPointHealthy();json["apRebuilds"]=WifiSetup::accessPointRebuilds();json["apRestoreFailures"]=WifiSetup::accessPointRestoreFailures();
        json["weatherValid"]=weather.valid; json["weatherError"]=weather.error; json["weatherAt"]=weatherAt;
        {auto flip=json["flipClock"].to<JsonObject>();flip["active"]=FlipClockPage::active();
         flip["dark"]=FlipClockPage::darkMode();flip["movingDigits"]=FlipClockPage::movingDigits();
         flip["storageFailed"]=FlipClockPage::storageFailed();}
        {auto clock=json["dashboardClock"].to<JsonObject>();auto state=DashboardClockPage::status();
         clock["active"]=DashboardClockPage::active();clock["preset"]=state.preset;clock["time"]=state.time;
         clock["seconds"]=state.seconds;clock["tilt"]=state.tilt;clock["rasterReady"]=state.rasterReady;clock["movingDigits"]=state.movingDigits;}
        json["readingError"]=readingError;
        json["designerUi"]=UiExport::enabled(); json["externalScreen"]=Screens::externalActive();
        json["uiSource"]="src/pages (hand-written)";json["uiDesignHash"]=SAVED_DESIGN_HASH;json["savedDesignPages"]=3;
        {auto timers=FocusService::snapshot();auto state=json["timers"].to<JsonObject>();state["schedulerReady"]=timers.schedulerReady;state["storageReady"]=timers.storageReady;state["writeFailed"]=timers.writeFailed;state["alarmCount"]=timers.engine.count();state["ringingCount"]=timers.engine.ringingCount();state["pomodoroStage"]=(int)timers.engine.state.session.stage;state["remainingMs"]=timers.engine.remaining(FocusService::monotonic());state["alarmMasterVolume"]=timers.engine.state.audio.alarm;state["pomodoroMasterVolume"]=timers.engine.state.audio.pomodoro;}
        {auto timer=TimerService::snapshot();auto state=json["countdownTimer"].to<JsonObject>();state["stage"]=(int)timer.engine.stage;state["remainingMs"]=timer.engine.remaining(FocusService::monotonic());state["durationSeconds"]=timer.engine.settings.seconds;state["tone"]=timer.engine.settings.tone;state["volume"]=timer.engine.settings.volume;state["ringing"]=timer.ringing;state["storageReady"]=timer.storageReady;state["writeFailed"]=timer.writeFailed;}
        {auto tetris=TetrisPage::status();auto state=json["tetris"].to<JsonObject>();state["state"]=tetris.state;state["score"]=tetris.score;state["best"]=TetrisPage::bestScore();state["level"]=tetris.level;state["lines"]=tetris.lines;}
        {auto snake=SnakePage::status();auto state=json["snake"].to<JsonObject>();state["state"]=snake.state;state["score"]=snake.score;state["best"]=SnakePage::bestScore();state["length"]=snake.length;state["intervalMs"]=snake.intervalMs;state["speed"]=snake.speed;state["headX"]=snake.headX;state["headY"]=snake.headY;state["direction"]=snake.direction;}
        {auto jump=JumpPage::status();auto state=json["jump"].to<JsonObject>();state["state"]=jump.state;state["score"]=jump.score;state["best"]=JumpPage::bestScore();state["jumps"]=jump.jumps;state["combo"]=jump.combo;state["chargeMs"]=jump.chargeMs;}
        {auto sheep=SheepPage::status();auto state=json["sheep"].to<JsonObject>();state["state"]=sheep.state;state["score"]=sheep.score;state["best"]=SheepPage::bestScore();state["remaining"]=sheep.remaining;state["level"]=sheep.level;state["trayCount"]=sheep.trayCount;state["undoAvailable"]=sheep.undoAvailable;state["shuffleAvailable"]=sheep.shuffleAvailable;}
        {auto miner=MinerPage::status();auto state=json["miner"].to<JsonObject>();state["state"]=miner.state;state["score"]=miner.score;state["best"]=MinerPage::bestScore();state["secondsLeft"]=miner.secondsLeft;state["target"]=miner.target;state["level"]=miner.level;state["remaining"]=miner.remaining;}
        {auto plane=PlanePage::status();auto state=json["plane"].to<JsonObject>();state["state"]=plane.state;state["score"]=plane.score;state["best"]=PlanePage::bestScore();state["kills"]=plane.kills;state["lives"]=plane.lives;state["wave"]=plane.wave;}
        {auto v=BreakoutPage::status();auto state=json["breakout"].to<JsonObject>();state["state"]=v.state;state["score"]=v.score;state["best"]=BreakoutPage::bestScore();state["level"]=v.level;state["lives"]=v.lives;state["remaining"]=v.remaining;}
        {auto v=MinesPage::status();auto state=json["mines"].to<JsonObject>();state["state"]=v.state;state["score"]=v.score;state["best"]=MinesPage::bestScore();state["elapsedMs"]=v.elapsedMs;state["remaining"]=v.remaining;state["flags"]=v.flags;}
        {auto v=PinPage::status();auto state=json["pin"].to<JsonObject>();state["state"]=v.state;state["score"]=v.score;state["best"]=PinPage::bestScore();state["level"]=v.level;state["remaining"]=v.remaining;state["attached"]=v.attached;}
        {auto v=MemoryPage::status();auto state=json["memory"].to<JsonObject>();state["state"]=v.state;state["score"]=v.score;state["best"]=MemoryPage::bestScore();state["elapsedMs"]=v.elapsedMs;state["moves"]=v.moves;state["level"]=v.level;state["pairs"]=v.pairs;state["totalPairs"]=v.totalPairs;}
        json["audioReady"]=AudioBoard::ready();json["audioError"]=AudioBoard::error();auto voice=XiaozhiVoice::get();json["voiceStage"]=(int)voice.stage;json["voiceMessage"]=voice.message;json["voiceConnected"]=voice.connected;json["voiceContinuous"]=voice.continuous;json["voiceSleeping"]=voice.sleeping;
        MusicPlayer::diagnostics(json["music"].to<JsonObject>());
        RadioPlayer::diagnostics(json["radio"].to<JsonObject>());
        JsonDocument connection;XiaozhiVoice::diagnostics(connection);json["voiceConnection"].set(connection.as<JsonVariantConst>());
        uint32_t frames,duration;Display::transitionStats(frames,duration);json["transitionActive"]=Display::transitioning();json["transitionFrames"]=frames;json["transitionMs"]=duration;json["transitionPushMs"]=Display::transitionPushMs();json["renderMs"]=lastRenderUs/1000;json["captureMs"]=lastCaptureUs/1000;json["buildMs"]=lastBuildUs/1000;json["flushMs"]=lastFlushUs/1000;json["refreshMs"]=lastRefreshUs/1000;json["animations"]=lv_anim_count_running();uint32_t lastPush,maxPush,pushCalls;uint64_t totalPush;Display::panelPushStats(lastPush,maxPush,totalPush,pushCalls);json["lcdPushLastUs"]=lastPush;json["lcdPushMaxUs"]=maxPush;json["lcdPushTotalUs"]=totalPush;json["lcdPushCalls"]=pushCalls;json["minHeapFree"]=ESP.getMinFreeHeap();json["largestFreeBlock"]=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL);json["animFps"]=Display::framesPerSecond();json["homeCached"]=GiftUI::homeCached();json["homeCacheHits"]=homeCacheHits;json["refrPeriod"]=LV_DISP_DEF_REFR_PERIOD;json["transitionEdge"]=TransitionEffect::Edge;{auto per=json["renderMsByView"].to<JsonArray>();for(unsigned i=0;i<sizeof(viewRenderMs)/sizeof(viewRenderMs[0]);i++)per.add(viewRenderMs[i]);}
        {Display::FrameStats stats;Display::frameStats(stats);auto state=json["displayFrame"].to<JsonObject>();
         state["flushRegions"]=stats.flushRegions;state["commits"]=stats.commits;
         state["lastRegions"]=stats.lastRegions;state["maxRegions"]=stats.maxRegions;
         state["lastPatches"]=stats.lastPatches;state["lastPixels"]=stats.lastPixels;
         state["lastDirtyPixels"]=stats.lastDirtyPixels;state["totalDirtyPixels"]=stats.totalDirtyPixels;
         state["lastCommitUs"]=stats.lastCommitUs;state["maxCommitUs"]=stats.maxCommitUs;
         state["totalCommitUs"]=stats.totalCommitUs;state["totalPixels"]=stats.totalPixels;
         state["lastRenderUs"]=stats.lastRenderUs;state["totalRenderUs"]=stats.totalRenderUs;
         state["presentedFrames"]=stats.presentedFrames;state["drawBufferPixels"]=stats.drawBufferPixels;
         state["internalDraw"]=stats.internalDraw;state["differenceReady"]=stats.differenceReady;}
        json["panelNativeWidth"]=Board::NativeWidth;json["panelNativeHeight"]=Board::NativeHeight;
        json["panelRotation"]=Board::LcdRotation;json["bgr"]=settings.bgr;json["inverted"]=settings.inverted;
        json["touchMode"]=Touch::rotationMode();json["touchPresent"]=Touch::present();json["touchCalibrated"]=Touch::affineCalibration().valid;
        json["displayDriver"]="TFT_eSPI";json["spiMHz"]=SPI_FREQUENCY/1000000u;
        const auto &physical=Touch::lastPhysical();json["physicalTouchPresses"]=physical.presses;
        if(physical.valid){auto sample=json["lastPhysicalTouch"].to<JsonObject>();sample["rawX"]=physical.rawX;sample["rawY"]=physical.rawY;sample["x"]=physical.x;sample["y"]=physical.y;sample["pressed"]=physical.pressed;}
        // V2 diagnostics: the last classified gesture (kind 0=tap, 1=swipe, -1=none).
        {const auto &g=Touch::lastGesture();auto out=json["lastGesture"].to<JsonObject>();
         out["kind"]=g.kind;out["x0"]=g.x0;out["y0"]=g.y0;out["dx"]=g.dx;out["dy"]=g.dy;
         out["suppressClick"]=g.suppressClick;out["swallowed"]=g.swallowed;}
        json["brightness"]=GiftData::get("deviceSettings")["brightness"];json["volume"]=GiftData::get("deviceSettings")["volume"];auto power=PowerStatus::get();json["externalPower"]=power.external;json["batteryInstalled"]=power.installed;PowerStatus::snapshot(json["battery"].to<JsonObject>());
        json["idleAgoMs"]=millis()-lastInputAt; json["idleLimitMs"]=(unsigned)IdleMs;
        serializeJson(json,Serial); Serial.println();
    } else if (command=="selftest") {
        const bool logic=DeskLogic::selfTest(), touch=Touch::selfTest(), screens=Screens::selfTest(),
                   uiExport=UiExport::selfTest(), widgets=Ui::selfTest(),
                   text=TextUtil::selfTest(), lunar=GiftLunar::selfTest();
        Serial.printf("logic=%s touch=%s screens=%s uiExport=%s widgets=%s text=%s lunar=%s\n",
            logic?"PASS":"FAIL",touch?"PASS":"FAIL",screens?"PASS":"FAIL",uiExport?"PASS":"FAIL",widgets?"PASS":"FAIL",text?"PASS":"FAIL",lunar?"PASS":"FAIL");
        Serial.println((logic && touch && screens && uiExport && widgets && text && lunar)?"SELFTEST PASS":"SELFTEST FAIL");
    }
    else if (command=="power" || command.startsWith("power ")) {
        if(command!="power") {
            const String setting=command.substring(6);
            const bool valid=setting=="battery 0"||setting=="battery 1"||setting=="external 0"||setting=="external 1";
            if(!valid){Serial.println("POWER_ERROR: use power [battery|external 0|1]");return;}
            JsonDocument patch;
            patch["power"][setting.startsWith("battery ")?"batteryInstalled":"externalPower"]=setting.endsWith("1");
            String error;
            if(!GiftData::patch(patch.as<JsonVariantConst>(),error)){Serial.println("POWER_ERROR: "+error);return;}
            liveRefreshPending=true;
        }
        PowerStatus::poll();JsonDocument json;PowerStatus::snapshot(json.to<JsonObject>());
        serializeJson(json,Serial);Serial.println();
    }
    else if (command=="screenshot") screenshotPending=true;
    else if (command=="scanprobe") Display::probeScanline();
    else if (command.startsWith("page ")) { int p=command.substring(5).toInt(); if (p>=0 && p<7) GiftPages::openPage(p); }
    else if (command.startsWith("tap ") || command.startsWith("swipe ")) {
        int x1=0,y1=0,x2=0,y2=0,n;
        if (command.startsWith("tap ")) { n=sscanf(command.c_str(),"tap %d %d",&x1,&y1); x2=x1; y2=y1; }
        else n=sscanf(command.c_str(),"swipe %d %d %d %d",&x1,&y1,&x2,&y2);
        if (n==(command.startsWith("tap ")?2:4) && x1>=0 && x1<320 && x2>=0 && x2<320 && y1>=0 && y1<240 && y2>=0 && y2<240) {
            Touch::simulate(x1,y1,x2,y2);
        }
    }
    else if (command.startsWith("app ")) {
        String name=command.substring(4);
        const char *names[]={"clock","weather","calendar","countdown","settings","phone","about","diagnostics","voice","alarm","pomodoro","tetris","music","timer","snake","jump","sheep","miner","plane","breakout","mines","pin","memory","days","radio","games"};
        const App apps[]={App::Clock,App::Weather,App::Calendar,App::Countdown,App::Settings,App::Phone,App::About,App::Diagnostics,App::Voice,App::Alarm,App::Pomodoro,App::Tetris,App::Music,App::Timer,App::Snake,App::Jump,App::Sheep,App::Miner,App::Plane,App::Breakout,App::Mines,App::Pin,App::Memory,App::Days,App::Radio,App::Games};
        for (unsigned i=0;i<sizeof(apps)/sizeof(apps[0]);++i) if (name==names[i]) GiftPages::openApp((int)apps[i]);
    } else if (command=="back") back();
    else if (command=="musicstatus") {JsonDocument json;MusicPlayer::diagnostics(json.to<JsonObject>());serializeJson(json,Serial);Serial.println();}
    else if (command=="musicstop") MusicPlayer::stop();
    else if (command=="radiostatus") {JsonDocument json;RadioPlayer::diagnostics(json.to<JsonObject>());serializeJson(json,Serial);Serial.println();}
    else if (command=="radiostop") RadioPlayer::stop();
    else if (command=="display" || command=="display default" || command.startsWith("displayset ")) {
        bool update=false;
        if(command=="display default") {
            settings.bgr=Board::LcdBgr;settings.inverted=Board::LcdInverted;settings.touchMode=Board::TouchMode;
            Touch::setCalibration(Touch::Calibration{});saveTouchCalibration();clearAffineCalibration();update=true;
        } else if(command.startsWith("displayset ")) {
            int bgr=0,inverted=0,mode=0;char extra;
            if(sscanf(command.c_str(),"displayset %d %d %d %c",&bgr,&inverted,&mode,&extra)!=3 || bgr<0 || bgr>1 || inverted<0 || inverted>1 || mode<0 || mode>3){
                Serial.println("usage: displayset <bgr 0|1> <inverted 0|1> <touch mirror 0..3>");return;
            }
            settings.bgr=bgr;settings.inverted=inverted;settings.touchMode=mode;update=true;
        }
        if(update){changed();saveSettings();applyDisplay();queueRender();}
        Serial.printf("DISPLAY {\"nativeWidth\":%d,\"nativeHeight\":%d,\"width\":%d,\"height\":%d,\"rotation\":%u,\"bgr\":%s,\"inverted\":%s,\"touchMode\":%u,\"profileRevision\":%u}\n",
            Board::NativeWidth,Board::NativeHeight,Board::Width,Board::Height,(unsigned)Board::LcdRotation,settings.bgr?"true":"false",settings.inverted?"true":"false",(unsigned)Touch::rotationMode(),(unsigned)Board::PanelProfileRevision);
    }
    else if (command=="touchraw") {
        Touch::setRawReporting(!Touch::rawReporting());
        Serial.println(Touch::rawReporting() ? "RAW reporting ON: touch the panel, raw I2C values stream as RAW x y"
                                             : "RAW reporting OFF");
    }
    else if (command.startsWith("touchset ")) {
        int x0=0,x1=0,y0=0,y1=0;
        if (sscanf(command.c_str(),"touchset %d %d %d %d",&x0,&x1,&y0,&y1)!=4 ||
            x1<=x0 || y1<=y0 || x0<0 || y0<0 || x1>319 || y1>239) {
            Serial.println("usage: touchset <xMin> <xMax> <yMin> <yMax>   (landscape raw bounds, xMax<320, yMax<240)");
        } else {
            Touch::Calibration cal; cal.xMin=x0; cal.xMax=x1; cal.yMin=y0; cal.yMax=y1; cal.valid=true;
            Touch::setCalibration(cal); saveTouchCalibration(); clearAffineCalibration();
            Serial.printf("touch calibration saved: x %d..%d -> 0..319, y %d..%d -> 0..239\n",x0,x1,y0,y1);
        }
    }
    else if (command=="touchcal" || command=="touchcal clear") {
        if (command.endsWith("clear")) {
            Touch::Calibration cal; Touch::setCalibration(cal); saveTouchCalibration();
            clearAffineCalibration();
            Serial.println("touch calibration cleared (identity mapping)");
        }
        const Touch::Calibration &cal=Touch::calibration();
        Serial.printf("TOUCH {\"rotation\":%u,\"calibrated\":%s,\"xMin\":%d,\"xMax\":%d,\"yMin\":%d,\"yMax\":%d,\"present\":%s}\n",
            (unsigned)Touch::rotationMode(), cal.valid?"true":"false", cal.xMin, cal.xMax, cal.yMin, cal.yMax,
            Touch::present()?"true":"false");
        Serial.println("measure bounds: run touchraw, touch the four corners, then touchset <xMin> <xMax> <yMin> <yMax>");
        const auto &affine=Touch::affineCalibration();
        Serial.printf("AFFINE {\"valid\":%s,\"ax\":%.6f,\"bx\":%.6f,\"cx\":%.6f,\"ay\":%.6f,\"by\":%.6f,\"cy\":%.6f}\n",affine.valid?"true":"false",affine.ax,affine.bx,affine.cx,affine.ay,affine.by,affine.cy);
    }
    else if (command=="uiexport on" || command=="uiexport off") {
        const bool enable = command.endsWith("on");
        UiExport::setEnabled(enable);
        if (enable) UiExport::attach(); else UiExport::detach();
        queueRender();
        Serial.printf("designer screen provider %s (claims the Weather app slot)\n",
                      enable ? "ENABLED" : "disabled");
    }
    else if (command=="weather") requestWeather(true);
    else if (command.startsWith("wificred ")) {
        // Bench helper: set the station credentials over USB without the captive
        // portal (useful when no phone/AP client is available).
        String args=command.substring(9);
        int space=args.indexOf(' ');
        String ssid=space<0?args:args.substring(0,space);
        String password=space<0?String():args.substring(space+1);
        if (ssid.isEmpty()||ssid.length()>32) {
            Serial.println("usage: wificred <ssid> <password>");
        } else {
            settings.ssid=ssid; settings.password=password;
            changed(); saveSettings(); reconnectDue=millis()+500;
            Serial.printf("WIFI CRED saved for \"%s\"\n",ssid.c_str());
        }
    }
    else if (command.startsWith("timeset ")) {
        // Offline bench helper: set the epoch without NTP so the clock faces can
        // be inspected on a desk with no Wi-Fi.
        int64_t epoch=atoll(command.c_str()+8);
        if (epoch>1700000000) {
            struct timeval value;value.tv_sec=(time_t)epoch;value.tv_usec=0;
            if(!settimeofday(&value,nullptr)){Serial.printf("TIME SET %lld\n",(long long)epoch);renderPending=true;}
            else Serial.println("TIME SET FAILED");
        } else Serial.println("TIME INVALID");
    }
    else if (command=="help") Serial.println("status, selftest, power [battery|external 0|1], screenshot, display [default], displayset <bgr> <inverted> <touchMode>, touchcal [clear], touchraw, touchset, timeset <epoch>, uiexport on|off, page 0..6, app clock/weather/calendar/settings/phone/about/diagnostics/voice/alarm/pomodoro/tetris/music/timer/snake/jump/sheep/miner/plane/breakout/mines/pin/memory/days/radio/games, radiostatus, radiostop, back, weather, wificred <ssid> <password>, tap <x> <y>, swipe <x1> <y1> <x2> <y2>, help");
    // The ESP32-S3 USB Serial/JTAG link occasionally drops a byte, which turns a
    // known verb into an unrecognised string. Report it instead of failing mutely.
    else if (command.length()>0) Serial.printf("UNKNOWN \"%s\"\n",command.c_str());
}
void pollSerial() {
    while (Serial.available()) {
        char c=Serial.read();
        if (c=='\n') { serialInput.trim(); serialCommand(serialInput); serialInput=""; }
        else if (c!='\r') { if (serialInput.length()<160) serialInput+=c; else serialInput=""; }
    }
}
}

// ---- unified navigation entry points (V2 Core) ------------------------------
// Every App/root-page switch goes through these two functions (contract §4). They
// own the per-App origin table and the entry side effects, so a missed call site
// cannot silently lose the "where did I come from" information (audit P0-2).
namespace GiftPages {

void openApp(int targetApp) {
    if (targetApp < 0 || targetApp >= NavAppCount) return;
    // Record the origin before switching. One slot per App means page 6 -> Settings
    // -> Wi-Fi keeps both levels; re-entering the current App keeps its origin.
    if (targetApp != (int)app) navRecord(navHistory, targetApp, (int)app, page);
    if (targetApp == (int)App::Countdown || targetApp == (int)App::Calendar) calendarConfirm = false;
    if (targetApp == (int)App::Countdown) countdownError = "";
    if (targetApp == (int)App::Calendar) { selectedDate = {}; calendarSelectionActive = false; }
    // Voice starts the official XiaoZhi session on entry. Wifi's requestScan() is
    // deliberately the caller's job: only the interactive entries want a scan.
    if (targetApp == (int)App::Voice) {RadioPlayer::stop();MusicPlayer::stop();XiaozhiVoice::launch();}
    if (targetApp == (int)App::Music) MusicPage::enter();
    if (targetApp == (int)App::Radio) RadioPage::enter();
    app = (App)targetApp;
    lastInputAt = millis();
    queueRender();
}

void openPage(int targetPage) {
    if (targetPage < 0 || targetPage > 6) return;
    GiftUI::reset();
    app = App::None;
    page = (uint8_t)targetPage;
    lastInputAt = millis();
    queueRender();
}

bool canGoBack() {
    // Exactly the levels desk back() can dismiss, cheapest first.
    if (GiftUI::canBack()) return true;      // page detail / legacy section
    if (WifiSetup::canBack()) return true;   // Wi-Fi keyboard: editing -> list
    return app != App::None;                 // App with an origin slot
}

}  // namespace GiftPages

// ---- navigation / refresh requests from the hand-written page modules -------
// Page modules (src/pages/*.cpp) never touch page/app/renderPending directly;
// they call GiftPages::fire() so navigation rules stay in one place.
void GiftPages::fire(int action) {
    lastInputAt = millis();
    switch (action) {
        case GiftPages::OpenHome: GiftPages::openPage(0); break;
        case GiftPages::OpenReading: GiftPages::openPage(3); break;
        case GiftPages::OpenDaily: GiftPages::openPage(4); break;
        case GiftPages::OpenSchedule: GiftPages::openPage(5); break;
        case GiftPages::OpenApps: GiftPages::openPage(6); break;
        case GiftPages::OpenWeather:
            GiftUI::reset(); GiftPages::openApp((int)App::Weather); requestWeather(); break;
        case GiftPages::OpenCalendar: {
            // Pre-state (current year/month) is applied before the unified entry;
            // openApp() clears the selection/confirm state itself.
            if (timeValid()) { struct tm t; time_t now = time(nullptr); localtime_r(&now, &t); calendarYear = t.tm_year + 1900; calendarMonth = t.tm_mon + 1; }
            GiftUI::reset(); GiftPages::openApp((int)App::Calendar); break;
        }
        case GiftPages::OpenCountdown:
            GiftUI::reset(); GiftPages::openApp((int)App::Countdown); break;
        case GiftPages::OpenClock:
            GiftUI::reset(); GiftPages::openApp((int)App::Clock); break;  // P1-3: Clock returns to its origin now
        case GiftPages::OpenSettings:
            GiftUI::reset(); GiftPages::openApp((int)App::Settings); break;
        case GiftPages::OpenPhone:
            GiftUI::reset(); GiftPages::openApp((int)App::Phone); break;
        case GiftPages::RefreshWeather:
            requestWeather(true); queueRender(); break;
        case GiftPages::RefreshQuote:
            requestReading("quote"); break;
        case GiftPages::RefreshPoem:
            requestReading("poem"); break;
        default: break;
    }
}

void notifyUser(const String &message,bool error) { showToast(message,error); }

void deskSetup() {
    Serial.begin(115200);
    // NOTE: Serial.setTxTimeoutMs() is deliberately NOT used here. A non-zero TX
    // timeout drops bytes when the host briefly stalls, which corrupts the JSON
    // lines that inspect_device.py parses during stress. Blocking writes are the
    // proven behaviour; the 10 ms experiment broke `stress` with JSON errors.
    pinMode(Board::AudioEnable,OUTPUT); digitalWrite(Board::AudioEnable,HIGH);
    loadSettings();
    if(prefs.getBool("voiceReturn",false)){prefs.putBool("voiceReturn",false);app=App::None;page=6;}
    setenv("TZ","CST-8",1); tzset();
    Serial.println("\nYIYI DESK boot");
    Serial.printf("Flash=%u PSRAM=%u\n",ESP.getFlashChipSize(),ESP.getPsramSize());
    Serial.println(DeskLogic::selfTest()?"SELFTEST PASS":"SELFTEST FAIL");
    if (!psramFound() || ESP.getPsramSize()<4*1024*1024) { Serial.println("ERROR: OPI PSRAM is required"); return; }
    Display::Options displayOptions;
    displayOptions.bgr=settings.bgr; displayOptions.inverted=settings.inverted; displayOptions.brightness=settings.brightness;
    if (!Display::begin(displayOptions)) return;
    Touch::begin();
    GiftData::begin();
    ScheduleStore::begin();
    PowerStatus::begin();if(!AudioBoard::begin())Serial.println("AUDIO: "+AudioBoard::error());AudioBoard::setVolume(GiftData::get("deviceSettings")["volume"]|45);FocusApps::begin();XiaozhiVoice::begin();
    Touch::setRotationMode(settings.touchMode);
    Touch::setGestureHandler(onTouchGesture);
    // LVGL retains this pointer, so the driver must outlive setup().
    Touch::attach(&touchDriver);
    touchDevice=lv_indev_drv_register(&touchDriver);
    root=lv_scr_act(); lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_text_font(root,saved_gift_font_bold_14_0,0);
    Ui::setRoot(root);
    // Buttons created by the widget layer dispatch back into the UI layer's handler.
    Ui::setActionHandler(actionEvent);
    if (!Jobs::begin() || xTaskCreatePinnedToCore(ioWorker,"desk-io",16384,nullptr,1,nullptr,0)!=pdPASS) {
        Serial.println("ERROR: IO task allocation failed"); return;
    }
    uint64_t mac=ESP.getEfuseMac(); char suffix[7]; snprintf(suffix,sizeof(suffix),"%06X",(unsigned)(mac>>24)&0xFFFFFF);
    apName="Yiyi-Desk-"+String(suffix); portalPassword=prefs.getString("portalpass");
    if (portalPassword.isEmpty()) {
        char secret[11]; snprintf(secret,sizeof(secret),"%010lu",(unsigned long)esp_random());
        portalPassword=secret; prefs.putString("portalpass",portalPassword);
    }
    WiFi.mode(WIFI_AP_STA); WiFi.setAutoReconnect(true); WiFi.setSleep(false); WiFi.softAP(apName.c_str(),portalPassword.c_str());
    portalDns.setErrorReplyCode(DNSReplyCode::NoError); portalDns.start(53,"*",WiFi.softAPIP());
    if (!settings.ssid.isEmpty()) WiFi.begin(settings.ssid.c_str(),settings.password.c_str());
    configTzTime("CST-8","ntp.aliyun.com","cn.pool.ntp.org","pool.ntp.org");
    configurePortal();
    WifiSetup::configure([](const String &ssid,const String &password){settings.ssid=ssid;settings.password=password;changed();saveSettings();reconnectDue=millis()+500;return true;},queueRender,apName,portalPassword,
        [](){if(!settings.ssid.isEmpty())WiFi.begin(settings.ssid.c_str(),settings.password.c_str());});
    render(); updateLiveLabels(true);
    deskReady = true;
    lastInputAt=millis(); Serial.println("READY: LVGL desktop, portal http://192.168.4.1");
}

void deskLoop() {
    if (!deskReady || !root || !Display::ready() || !jobQueue || !resultQueue) { delay(20); return; }
    static uint32_t tick=millis(); uint32_t now=millis(); lv_tick_inc(now-tick); tick=now;
    PowerStatus::poll();
    pollSerial(); portalDns.processNextRequest(); server.handleClient(); consumeResults();
    WifiSetup::poll();
    RadioPlayer::tick();
    // The scan path temporarily drops the SoftAP; this watchdog rebuilds it with
    // exponential backoff if the restore failed. It is a 5 s flag check, no delay.
    WifiSetup::maintainAccessPoint();
    // Coexistence: keep the SoftAP on the same 2.4 GHz channel as the station.
    // AP+STA on different channels forces the single radio to time-slice; this
    // used to cap the independent music player PCM download below real time.
    {
        static uint32_t apRebuildMark=0; static uint8_t apChannel=0;
        if (WiFi.status()==WL_CONNECTED && !WifiSetup::radioBusy()) {
            uint8_t staChannel=(uint8_t)WiFi.channel();
            uint32_t rebuilds=WifiSetup::accessPointRebuilds();
            if (staChannel && (staChannel!=apChannel || rebuilds!=apRebuildMark)) {
                WiFi.softAP(apName.c_str(), portalPassword.c_str(), staChannel);
                apChannel=staChannel; apRebuildMark=rebuilds;
            }
        } else if (WiFi.status()!=WL_CONNECTED) {
            apChannel=0;
        }
    }
    // Commands handled above stamp lastInputAt with a fresher millis() than `now`,
    // which made (now - lastInputAt) wrap to ~2^32 and fire the idle return on the
    // very next check. Re-read the clock before any comparison against lastInputAt.
    now=millis();
    // The Touch module owns press detection; fold it into the idle timer.
    if (Touch::consumeActivity()) lastInputAt=now;
    // A pending reconnect must not touch the WiFi driver while a scan or the
    // post-scan AP restore owns it. radioBusy() (not busy()) is the right guard:
    // a user-triggered connect sets connecting=true before this branch runs the
    // actual WiFi.begin, so guarding on that flag would deadlock the connect flow.
    if (reconnectDue && (int32_t)(now-reconnectDue)>=0 && !WifiSetup::radioBusy()) {
        reconnectDue=0; WiFi.disconnect(); WiFi.begin(settings.ssid.c_str(),settings.password.c_str()); reconnectAt=now;
    }
    if (!WifiSetup::busy() && !settings.ssid.isEmpty() && WiFi.status()!=WL_CONNECTED && now-reconnectAt>30000) { reconnectAt=now; WiFi.reconnect(); }
    // A4: a failed NVS write keeps `dirty` set and retries after 5 s instead of
    // losing the setting; the signed comparison also accepts the future dirtyAt.
    if (dirty && (int32_t)(now-dirtyAt)>=1200) {
        if (!saveSettings()) {
            notifyUser("保存失败，将重试",true);
            dirtyAt=now+5000;
        }
    }
    GiftData::poll();PowerStatus::poll();FocusApps::poll();XiaozhiVoice::poll();
    // A2: commit a timetable imported from the portal outside the HTTP handler, so
    // the response and LVGL are not blocked by the LittleFS write.
    if (schedulePending) {
        schedulePending=false;
        const bool scheduleSaved=ScheduleStore::saveText(pendingSchedule.c_str(),pendingSchedule.length());
        pendingSchedule="";
        if (scheduleSaved) { changed(); queueRender(); }
        else notifyUser(ScheduleStore::lastError(),true);
    }
    // D-6: autoHome is a persisted JSON field; reading it every loop iteration was
    // a needless lookup + String key. Cache it per GiftData revision (the revision
    // changes on every write), so behaviour is unchanged.
    static uint32_t autoHomeRev=0xFFFFFFFFu; static bool autoHomeOn=true;
    if (GiftData::revision()!=autoHomeRev) { autoHomeRev=GiftData::revision(); autoHomeOn=GiftData::get("deviceSettings")["autoHome"]|true; }
    if (autoHomeOn&&!GiftUI::inputActive()&&app!=App::Clock&&app!=App::Voice&&app!=App::Alarm&&app!=App::Pomodoro&&app!=App::Timer&&app!=App::Tetris&&app!=App::Music&&app!=App::Radio&&app!=App::Snake&&app!=App::Jump&&app!=App::Sheep&&app!=App::Miner&&app!=App::Plane&&app!=App::Breakout&&app!=App::Mines&&app!=App::Pin&&app!=App::Memory&&!XiaozhiVoice::busy()&&now-lastInputAt>=IdleMs && (app!=App::None || page!=0)) { GiftUI::reset();app=App::None; page=0; queueRender(); }
    if (!weatherPending && (weatherAt==0 || now-weatherAt>=(weather.valid && weather.error.isEmpty()?WeatherMs:weatherRetryDelay))) requestWeather();
    requestReading();
    bool rebuilt=renderPending;
    if (rebuilt && !GiftUI::inputActive()&&!Display::transitioning()) {
        uint32_t started=micros(); render(); lastRenderUs=micros()-started;
        int viewIndex=app==App::None?page:7+(int)app;
        if(viewIndex>=0&&viewIndex<int(sizeof(viewRenderMs)/sizeof(viewRenderMs[0])))viewRenderMs[viewIndex]=(uint16_t)std::min<uint32_t>(lastRenderUs/1000u,65535u);
    }
    if(!Display::transitioning()){
        uint32_t started=micros();bool force=liveRefreshPending;
        // The cached home is refreshed through its widget pointers; every other
        // view keeps the rebuilding refresh().
        if(cachedHomeShown)GiftUI::refreshHome(homeRefs,force);else GiftUI::refresh(force);
        if(force)liveRefreshPending=false;
        updateLiveLabels(rebuilt);lastRefreshUs=micros()-started;
    }
    lv_timer_handler();Display::pollTransition();Touch::setBlocked(Display::transitioning());
    Ui::batteryNoticeRefresh();
    if (screenshotPending && xSemaphoreTake(ioMutex,0)==pdTRUE) {
        // Keep worker logs outside the binary frame; sendFrame retries partial CDC writes.
        screenshotPending=false;
        Display::sendFrame();
        xSemaphoreGive(ioMutex);
    }
    delay(5);
}
