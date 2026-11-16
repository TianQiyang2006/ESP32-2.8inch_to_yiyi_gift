// Host-only integration tests render the production LVGL page and send pointer
// samples through LVGL's input driver. Board gestures are never fabricated events.
#include "Arduino.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/pages/pin_engine.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int64_t HostEpoch = 0;
lv_color_t pixels[320 * 240], buffer[320 * 40];
std::string out;
unsigned frames = 0;
lv_indev_t *pointer = nullptr;
lv_point_t finger = {0, 0};
bool fingerDown = false;

void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 320 && y >= 0 && y < 240) pixels[y * 320 + x] = *data;
            ++data;
        }
    lv_disp_flush_ready(drv);
}
void readPointer(lv_indev_drv_t *, lv_indev_data_t *data) {
    data->point = finger;
    data->state = fingerDown ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
lv_obj_t *findLabel(lv_obj_t *obj, const char *value, unsigned &skip) {
    if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), value)) {
        if (!skip) return obj;
        --skip;
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i)
        if (auto *found = findLabel(lv_obj_get_child(obj, i), value, skip)) return found;
    return nullptr;
}
lv_obj_t *label(const char *value, unsigned skip = 0) { return findLabel(root, value, skip); }
std::string state() { return PinPage::status().state; }
void checkLabels(lv_obj_t *obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_point_t size{};
        lv_txt_get_size(&size, lv_label_get_text(obj), lv_obj_get_style_text_font(obj, 0),
                        lv_obj_get_style_text_letter_space(obj, 0), lv_obj_get_style_text_line_space(obj, 0),
                        lv_obj_get_width(obj), LV_TEXT_FLAG_NONE);
        if (size.y > lv_obj_get_height(obj)) {
            std::cerr << "Clipped label: " << lv_label_get_text(obj) << " height "
                      << size.y << "/" << lv_obj_get_height(obj) << "\n";
            assert(false);
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) checkLabels(lv_obj_get_child(obj, i));
}
void save(const std::string &name, bool validate = true) {
    lv_obj_update_layout(root);
    if (validate) checkLabels(root);
    lv_refr_now(nullptr);
    std::ofstream file(out + "/" + name + ".rgb565", std::ios::binary);
    file.write((char *)pixels, sizeof(pixels));
    ++frames;
    std::cout << "frame " << name << "\n";
}
void advance(uint32_t ms) {
    for (uint32_t done = 0; done < ms;) {
        const uint32_t step = std::min<uint32_t>(5, ms - done);
        HostMillis += step;
        lv_tick_inc(step);
        lv_timer_handler();
        done += step;
    }
    lv_obj_update_layout(root);
}
void sample(int x, int y, bool down) {
    finger.x = x; finger.y = y; fingerDown = down;
    lv_timer_ready(pointer->driver->read_timer);
    advance(5);
}
void tap(int x, int y) { sample(x, y, true); sample(x, y, false); }
lv_obj_t *control(const char *value, unsigned nth = 0) {
    lv_obj_update_layout(root);
    auto *caption = label(value, nth);
    if (!caption) std::cerr << "Missing caption: " << value << " occurrence " << nth << "\n";
    assert(caption);
    auto *target = caption;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    return target;
}
void press(const char *value, unsigned nth = 0) {
    auto *target = control(value, nth);
    lv_area_t area; lv_obj_get_coords(target, &area);
    std::cout << "finger tap " << value << "\n";
    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
    PinPage::refresh(true);
}
void swipe(int x0, int y0, int x1, int y1) {
    sample(x0, y0, true);
    for (int i = 1; i <= 6; ++i) sample(x0 + (x1-x0)*i/6, y0 + (y1-y0)*i/6, true);
    sample(x1, y1, false);
}
void show(App target, uint8_t targetPage = 6) {
    app = target; page = targetPage;
    renderPending = false;
    GiftUI::leaveView();
    Ui::cancelAnimations(); Ui::clearStatusCluster();
    lv_obj_clean(root); SavedDesign::invalidate();
    lv_indev_reset(pointer, nullptr);
    assert(GiftUI::render(root));
    lv_obj_update_layout(root);
}
const PinEngine::Game &game(){return PinPage::debugGame();}
void fresh() {
    fingerDown=false;lv_indev_reset(pointer,nullptr);PinPage::debugVariant(0);press("开始游戏");
    assert(state()=="playing");
}
void earnFromNewGame() {
    assert(state()=="playing"&&game().score==0);tap(160,195);
    assert(state()=="flying"&&game().score==0);
    for(unsigned elapsed=0;!game().score&&elapsed<500;elapsed+=5)advance(5);
    assert(game().score==1&&game().best>=1&&state()=="playing");
}
void safePointerShot() {
    assert(state()=="playing");
    for(unsigned elapsed=0;elapsed<9000;elapsed+=10) {
        auto forecast=game();const auto now=HostMillis;
        if(forecast.shoot(now+10)) {
            forecast.tick(now+10+PinEngine::FlightMs);
            if(forecast.state!=PinEngine::State::GameOver) {
                const auto before=game().score;tap(160,195);advance(PinEngine::FlightMs+30);
                assert(game().score==before+1&&state()!="gameover");return;
            }
        }
        advance(10);
    }
    std::cerr<<"No playable timed pointer shot found\n";assert(false);
}
void finishLevel() {
    const auto count=game().remaining;
    for(unsigned i=0;i<count;++i)safePointerShot();
    assert(!game().remaining&&(state()=="levelcleared"||state()=="won"));
}
void uiTests() {
    GiftUI::setAppsPage(1); show(App::None); save("launcher-2", false);
    press("游戏"); assert(app == App::Games); show(App::Games);
    assert(GiftUI::swipeApps(-80)); show(App::Games);
    save("games-2", false);
    assert(label("见缝插针"));press("见缝插针");assert(app==App::Pin&&renderPending);show(App::Pin);
    assert(state()=="ready");save("pin-ready");
    press("返回",1);assert(app==App::Games&&renderPending);
    show(App::Games);press("见缝插针");show(App::Pin);press("开始游戏");
    assert(state()=="playing"&&game().remaining==6&&game().attached==3);save("pin-start");
    tap(160,195);assert(state()=="flying"&&game().score==0);advance(80);save("pin-flight");
    tap(160,195);assert(game().score==0&&state()=="flying");
    press("暂停");assert(state()=="paused");save("pin-paused");
    const auto elapsed=game().elapsedMs;const auto progress=game().flightElapsedMs;
    const auto angle=game().rotation,y=game().flightY;advance(1500);
    assert(game().elapsedMs==elapsed&&game().flightElapsedMs==progress&&game().rotation==angle&&game().flightY==y);
    press("继续");assert(state()=="flying");
    const auto beforeRebuild=game().flightElapsedMs;show(App::Pin);
    assert(state()=="flying"&&game().flightElapsedMs>=beforeRebuild&&game().flightElapsedMs<=beforeRebuild+20);
    for(unsigned i=0;!game().score&&i<500;i+=5)advance(5);
    assert(game().score==1&&game().remaining==5&&game().attached==4);save("pin-score");
    for(int i=0;i<12;++i){press("暂停");assert(state()=="paused");press("继续");assert(state()=="playing");}
    fresh();swipe(160,195,20,16);
    assert(state()=="playing"&&app==App::Pin&&!renderPending&&!game().score);
    sample(160,195,true);sample(200,195,true);sample(160,195,true);sample(160,195,false);
    assert(state()=="playing"&&!game().score);
    sample(160,195,true);show(App::Pin);sample(160,195,false);advance(40);
    assert(state()=="playing"&&!game().score);
    tap(1,195);assert(state()=="flying"&&app==App::Pin&&!renderPending);
    show(App::None);assert(state()=="paused");advance(1000);show(App::Pin);assert(state()=="paused");
    press("继续");assert(state()=="flying");
    press("暂停");const auto best=game().best;press("重新开始");
    assert(state()=="playing"&&!game().score&&game().best==best&&game().level==1);
    // A naturally timed shot meets an existing spoke near its downward angle.
    fresh();advance(800);tap(160,195);advance(300);
    assert(state()=="gameover"&&!game().score);save("pin-gameover");
    press("再来一局");assert(state()=="playing");
    // Search copies only for safe timing; all 24 score-bearing launches below
    // still travel through the actual registered pointer and production timer.
    fresh();finishLevel();assert(game().score==6);save("pin-levelcleared");
    press("下一关");assert(game().level==2&&game().speed<0&&game().remaining==8);save("pin-level2");
    finishLevel();assert(game().score==14);press("下一关");
    assert(game().level==3&&game().speed>0&&game().remaining==10);save("pin-level3");
    finishLevel();assert(state()=="won"&&game().score==24);save("pin-won");
    press("返回",1);assert(app==App::Games&&renderPending);
    GiftUI::setAppsPage(0);show(App::None);save("launcher-1",false);
    GiftUI::setAppsPage(1);show(App::None);save("launcher-2",false);
    std::cout<<"PASS: real pointer flight/scoring/collision, no queued shot, paused flight/rebuild, old contact isolation, drag cancellation, edge ownership, leave/reentry, 12 overlay cycles and all three levels won by timed pointer launches\n";
}
int persistent(const std::string &mode) {
    GiftUI::setAppsPage(3);show(App::Pin);
    if(mode=="best-restore")assert(PinPage::bestScore()>0);
    else if(mode=="best-pause"||mode=="best-leave") {
        press("开始游戏");earnFromNewGame();
        if(mode=="best-pause"){press("暂停");assert(state()=="paused");}
        else{show(App::None);assert(state()=="paused");}
    }else return 2;
    std::cout<<"BEST="<<PinPage::bestScore()<<"\nPASS process "<<mode<<"\n";return 0;
}
void init() {
    LittleFS.directory = "build/pin-host/store";
    std::filesystem::create_directories(LittleFS.directory);
    lv_init();
    static lv_disp_draw_buf_t drawbuf; lv_disp_draw_buf_init(&drawbuf, buffer, nullptr, 320*40);
    static lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res = 320; display.ver_res = 240; display.draw_buf = &drawbuf; display.flush_cb = flush;
    lv_disp_drv_register(&display);
    static lv_indev_drv_t input; lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER; input.read_cb = readPointer;
    pointer = lv_indev_drv_register(&input); lv_timer_set_period(pointer->driver->read_timer, 5);
    root = lv_scr_act(); lv_obj_remove_style_all(root); lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    Ui::setRoot(root);
    Ui::setActionHandler([](lv_event_t *e) {
        if (uintptr_t(lv_event_get_user_data(e)) == Ui::Back && !GiftUI::back()) {
            app = App::None; page = 6; renderPending = true;
        }
    });
    assert(GiftData::begin()); HostEpoch = DeskLogic::beijingEpoch(2026, 11, 13, 12, 0);
}
int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf); std::cerr.setf(std::ios::unitbuf);
#ifdef _WIN32
    _set_error_mode(1);
#endif
    init();
    if (argc > 2 && std::string(argv[1]) == "--mode") return persistent(argv[2]);
    if (argc < 2) return 2;
    out = argv[1]; std::filesystem::create_directories(out); uiTests();
    std::cout << "PASS: " << frames << " screenshots from production firmware widgets; no device access\n";
}
