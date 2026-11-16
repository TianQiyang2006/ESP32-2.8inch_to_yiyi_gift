// Host-only offscreen renderer for the hand-written pages (home / weather /
// reading / daily). It drives the firmware's real GiftUI::render() with the
// host stubs, so the screenshots show the actual firmware widget tree.
//
// Run through hardware/scripts/test_native_lvgl.py --pages; that script compiles
// this file with the rest of the UI and converts the raw frames to PNG.
#include "Arduino.h"
#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/power_status.h"
#include "hardware/arduino/yiyi_desk/src/pages/page_api.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int64_t HostEpoch = DeskLogic::beijingEpoch(2026, 10, 27, 9, 56);
extern int HostBatteryMv;
extern int HostBrightness;

lv_color_t framebuffer[320 * 240], drawbuffer[320 * 40];
static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    (void)drv;
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 320 && y >= 0 && y < 240) framebuffer[y * 320 + x] = *pixels;
            ++pixels;
        }
    lv_disp_flush_ready(drv);
}

static std::string outDir;

static void save(const std::string &name) {
    lv_refr_now(nullptr);
    std::ofstream file(outDir + "/" + name + ".rgb565", std::ios::binary);
    file.write(reinterpret_cast<char *>(framebuffer), sizeof(framebuffer));
    std::cout << "frame " << name << "\n";
}

// Rebuilds the current view from scratch (the same call desk.cpp makes for a
// page switch) and saves one frame.
static void frame(const std::string &name) {
    GiftUI::reset();
    lv_obj_clean(root);
    Ui::setRoot(root);
    Ui::setPage(page);
    SavedDesign::invalidate();
    GiftUI::render(root);
    GiftUI::refresh(true);
    lv_obj_update_layout(root);
    save(name);
}

static void show(int pageIndex, App target) {
    app = target;
    page = (uint8_t)pageIndex;
}

static void weatherData(bool valid) {
    weather = Weather{};
    weather.valid = valid;
    weather.fetchedAt = HostEpoch;
    weather.query = "kunming";
    if (!valid) return;
    weather.temperature = 18;
    weather.condition = "阴";
    weather.code = 3;
    weather.feels = 18;
    weather.low = 15;
    weather.high = 24;
    weather.humidity = 80;
    weather.wind = 8;
    weather.windText = "南风 2级";
    weather.pressure = 820;
    weather.visibility = 16;
    weather.uv = 5;
    weather.aqi = 44;
    weather.aqiCategory = "优";
    weather.sunrise = "07:14";
    weather.sunset = "18:33";
    for (int i = 0; i < 7; ++i) {
        weather.forecast[i].date = "2026-10-" + String(27 + i);
        weather.forecast[i].high = 24 - i;
        weather.forecast[i].low = 15 - i / 2;
        weather.forecast[i].code = i % 3 == 0 ? 0 : i % 3 == 1 ? 2 : 61;
    }
}

static void checkBattery() {
    const auto setPower=[](bool installed,bool external) {
        JsonDocument patch;
        patch["power"]["batteryInstalled"]=installed;
        patch["power"]["externalPower"]=external;
        String error;
        assert(GiftData::patch(patch.as<JsonVariantConst>(),error));
        PowerStatus::poll();
    };
    // External power never establishes 100% or actual charging.
    setPower(true,true);
    HostBatteryMv=1850;PowerStatus::begin();
    Ui::BatteryView view;Ui::batteryCreate(view,root,10,10,80,18);
    assert(std::string(lv_label_get_text(view.percent))=="30 %");
    assert(view.present&&!view.charging&&view.fillPx==4);
    const auto healthyColor=view.color;
    setPower(true,false);
    HostBatteryMv=1650;PowerStatus::begin();HostMillis=4000;
    Ui::batteryRefresh(view);
    assert(PowerStatus::get().critical&&view.blinkOff&&view.color!=healthyColor);
    HostMillis+=500;Ui::batteryRefresh(view);assert(!view.blinkOff);
    HostBatteryMv=0;HostMillis+=2000;PowerStatus::poll();Ui::batteryRefresh(view);
    assert(!view.present&&!view.charging&&view.fillPx==0);
    assert(std::string(lv_label_get_text(view.percent))=="—");
    setPower(false,true);Ui::batteryRefresh(view);
    assert(!view.present&&!view.charging);
    Ui::batteryNoticeRefresh();
    const auto overlays=lv_obj_get_child_cnt(lv_layer_top());
    const int brightness=HostBrightness;
    HostBatteryMv=1825;setPower(true,true);PowerStatus::begin();
    Ui::batteryNoticeRefresh();
    assert(lv_obj_get_child_cnt(lv_layer_top())==overlays+1);
    auto *notice=lv_obj_get_child(lv_layer_top(),-1);
    auto *card=lv_obj_get_child(notice,0);
    assert(std::string(lv_label_get_text(lv_obj_get_child(card,1)))=="请通过外置充电模块充电");
    save("battery-low-popup");
    Ui::batteryNoticeRefresh();assert(lv_obj_get_child_cnt(lv_layer_top())==overlays+1);
    lv_event_send(lv_obj_get_child(card,2),LV_EVENT_CLICKED,nullptr);
    lv_timer_handler();
    assert(lv_obj_get_child_cnt(lv_layer_top())==overlays);
    HostMillis+=60000;PowerStatus::poll();Ui::batteryNoticeRefresh();
    assert(lv_obj_get_child_cnt(lv_layer_top())==overlays); // Dismissal is not a 1 Hz popup loop.
    HostBatteryMv=1650;PowerStatus::begin();Ui::batteryNoticeRefresh();
    assert(lv_obj_get_child_cnt(lv_layer_top())==overlays+1); // Escalate to critical once.
    save("battery-critical-popup");
    HostBatteryMv=2000;PowerStatus::begin();Ui::batteryNoticeRefresh();
    assert(lv_obj_get_child_cnt(lv_layer_top())==overlays); // Charging recovery closes the notice.
    HostBatteryMv=1825;PowerStatus::begin();Ui::batteryNoticeRefresh();
    assert(lv_obj_get_child_cnt(lv_layer_top())==overlays+1); // A new real low episode is rearmed.
    setPower(false,true);Ui::batteryNoticeRefresh();
    assert(lv_obj_get_child_cnt(lv_layer_top())==overlays&&HostBrightness==brightness);
    Ui::batteryClear(view);lv_obj_clean(root);HostBatteryMv=1850;
    std::cout<<"PASS: battery percentage, charging, blink and low/critical popup dismissal, recovery/rearm without changing brightness\n";
}

int main(int argc, char **argv) {
    outDir = argc > 1 ? argv[1] : "build/host-pages/frames";
    std::filesystem::create_directories(outDir);
    std::filesystem::create_directories("build/host-lvgl");
    LittleFS.directory = "build/host-lvgl/store";

    lv_init();
    static lv_disp_draw_buf_t buf;
    lv_disp_draw_buf_init(&buf, drawbuffer, nullptr, 320 * 40);
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = 320;
    drv.ver_res = 240;
    drv.draw_buf = &buf;
    drv.flush_cb = flush;
    lv_disp_drv_register(&drv);

    root = lv_scr_act();
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    Ui::setRoot(root);
    Ui::setActionHandler([](lv_event_t *) {});
    if (!GiftData::begin()) {
        std::cerr << "GiftData::begin failed\n";
        return 1;
    }
    PowerStatus::begin();
    checkBattery();
    FocusApps::begin();
    show(0, App::Alarm);
    frame("app-alarm");
    show(0, App::Pomodoro);
    frame("app-pomodoro");
    settings.city = "昆明";
    settings.hasLocation = true;
    HostBatteryMv = 4150;

    // ---- home -------------------------------------------------------------
    weatherData(true);
    show(0, App::None);
    HomePage::debugVariant(0);
    frame("home-online");

    weatherData(false);
    show(0, App::None);
    HomePage::debugVariant(0);
    frame("home-offline");

    HomePage::debugVariant(1);  // clock placeholder while the time is not synced
    show(0, App::None);
    frame("home-no-time");
    HomePage::debugVariant(0);

    weatherData(true);

    // ---- weather page (page 1 and the weather app slot) --------------------
    weatherData(true);
    show(1, App::None);
    WeatherPage::debugVariant(1);
    frame("weather-online");

    weatherData(false);
    WeatherPage::debugVariant(2);
    frame("weather-offline-cache");

    WeatherPage::debugVariant(3);
    frame("weather-offline-empty");

    weatherData(true);
    weatherPending = true;
    WeatherPage::debugVariant(4);
    frame("weather-updating");
    weatherPending = false;

    // ---- reading ----------------------------------------------------------
    show(3, App::None);
    ReadingPage::debugVariant(0);
    frame("reading-main");

    // B4 boundary: a 52-character quote exercises the 14px auto-fit tier. The
    // host store is patched for this frame only, then restored.
    {
        const String prevText = GiftData::get("reading")["quoteText"] | "";
        const String prevAuthor = GiftData::get("reading")["quoteAuthor"] | "";
        const String prevMode = GiftData::get("reading")["quoteMode"] | "api";
        JsonDocument patch;
        patch["reading"]["quoteText"] =
            "世界上只有一种真正的英雄主义，那就是在认清生活的真相之后依然热爱生活，"
            "并且愿意为此付出努力。";
        patch["reading"]["quoteAuthor"] = "罗曼·罗兰";
        patch["reading"]["quoteMode"] = "manual";
        String error;
        GiftData::patch(patch.as<JsonVariantConst>(), error);
        show(3, App::None);
        ReadingPage::debugVariant(0);
        frame("reading-main-longquote");
        JsonDocument restore;
        restore["reading"]["quoteText"] = prevText;
        restore["reading"]["quoteAuthor"] = prevAuthor;
        restore["reading"]["quoteMode"] = prevMode;
        GiftData::patch(restore.as<JsonVariantConst>(), error);
    }

    ReadingPage::debugVariant(1);
    show(3, App::None);
    frame("reading-quote-detail");
    ReadingPage::debugVariant(2);
    show(3, App::None);
    frame("reading-poem-detail");
    ReadingPage::debugVariant(0);

    // ---- daily ------------------------------------------------------------
    show(4, App::None);
    DailyPage::debugVariant(0);
    frame("daily-main");
    show(4, App::None);
    DailyPage::debugVariant(1);
    frame("daily-mood-editor");
    show(4, App::None);
    DailyPage::debugVariant(2);
    frame("daily-water-detail");
    show(4, App::None);
    DailyPage::debugVariant(3);
    frame("daily-wood-detail");
    DailyPage::debugVariant(0);

    // ---- schedule / apps / settings --------------------------------------
    show(5, App::None);
    frame("schedule-current");
    show(6, App::None);
    frame("apps");
    show(0, App::Settings);
    frame("settings");

    // ---- clock app (D-5: day/night background pair) ----------------------
    show(0, App::Clock);
    frame("app-clock");

    // ---- page 2 (holiday) stays a saved-design page ----------------------
    // Added so layout-neutral cleanups of the retired saved screens can be
    // proven byte-identical on the one legacy page that is still reachable.
    weatherData(true);
    show(2, App::None);
    frame("holiday");

    std::cout << "PASS: host page renderer wrote " << outDir << "\n";
    return 0;
}
