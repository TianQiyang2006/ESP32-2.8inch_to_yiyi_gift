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
#include "hardware/arduino/yiyi_desk/src/pages/memory_engine.h"
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
std::string state() { return MemoryPage::status().state; }
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
    MemoryPage::refresh(true);
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
const MemoryEngine::Game &game() { return MemoryPage::debugGame(); }
void tapCard(int id) {
    assert(id>=0&&id<game().count);
    tap(55+(id%4)*70,32+game().top()+(id/4)*36+15);
    MemoryPage::refresh(true);
}
int mateFor(int id) {
    for(int other=0;other<game().count;++other)
        if(other!=id&&!game().cards[other].matched&&game().cards[other].face==game().cards[id].face)return other;
    assert(false);return -1;
}
int differentFrom(int id) {
    for(int other=0;other<game().count;++other)
        if(other!=id&&!game().cards[other].matched&&game().cards[other].face!=game().cards[id].face)return other;
    assert(false);return -1;
}
void fresh() {
    fingerDown=false;lv_indev_reset(pointer,nullptr);
    MemoryPage::debugVariant(0);press("开始游戏");assert(state()=="playing");
}
void earnFromNewGame() {
    assert(state()=="playing"&&game().score==0&&game().pairs==0&&game().first==-1);
    // Read-only faces choose a genuine pair. All reveals and reward resolution
    // pass through actual pointer coordinates on the untouched production page.
    const int mate=mateFor(0);tapCard(0);assert(game().first==0&&game().visible(0));
    tapCard(mate);assert(game().score==100&&game().pairs==1&&game().moves==1&&game().best>=100);
}
void clearLevel() {
    assert(state()=="playing"&&game().first==-1);
    while(state()=="playing") {
        int first=-1;
        for(int id=0;id<game().count;++id)if(!game().cards[id].matched){first=id;break;}
        assert(first>=0);const int mate=mateFor(first);const auto before=game().score;
        tapCard(first);tapCard(mate);
        assert(game().cards[first].matched&&game().cards[mate].matched&&game().score>=before+100);
    }
    assert(state()=="levelcleared"||state()=="won");
}
void uiTests() {
    GiftUI::setAppsPage(1); show(App::None); save("launcher-2", false);
    press("游戏"); assert(app == App::Games); show(App::Games);
    assert(GiftUI::swipeApps(-80)); show(App::Games);
    save("games-2", false);
    assert(label("记忆翻牌")&&label("见缝插针"));
    press("记忆翻牌");assert(app==App::Memory&&renderPending);show(App::Memory);
    assert(state()=="ready");save("memory-ready");press("返回",1);assert(app==App::Games&&renderPending);
    show(App::Games);press("记忆翻牌");show(App::Memory);press("开始游戏");
    assert(state()=="playing"&&game().score==0&&game().level==1&&game().count==12);save("memory-start");
    tapCard(0);assert(game().first==0&&game().second==-1&&game().moves==0&&game().visible(0));save("memory-one");
    tapCard(0);assert(game().first==0&&game().second==-1&&game().moves==0);
    const int other=differentFrom(0);tapCard(other);
    assert(state()=="comparing"&&game().first==0&&game().second==other&&game().moves==1&&game().score==0);save("memory-mismatch");
    int third=-1;for(int id=0;id<game().count;++id)if(id!=0&&id!=other){third=id;break;}
    tapCard(third);assert(state()=="comparing"&&game().first==0&&game().second==other&&game().moves==1&&!game().visible(third));
    press("暂停");assert(state()=="paused"&&game().pausedState==MemoryEngine::State::Comparing);save("memory-paused");
    const auto pausedTime=game().elapsedMs;const auto pausedCompare=game().compareRemaining;
    const auto pausedFirst=game().first,pausedSecond=game().second;
    std::vector<lv_color_t> pausedPixels(pixels,pixels+320*240);
    advance(2000);lv_refr_now(nullptr);
    assert(game().elapsedMs==pausedTime&&game().compareRemaining==pausedCompare&&game().first==pausedFirst&&game().second==pausedSecond);
    assert(!memcmp(pausedPixels.data(),pixels,sizeof(pixels)));
    // Leaving and rebuilding the paused mismatch keeps both cards and the
    // exact remaining comparison duration rather than consuming wall time.
    show(App::None);assert(state()=="paused");advance(1000);show(App::Memory);
    assert(state()=="paused"&&game().compareRemaining==pausedCompare&&game().first==pausedFirst&&game().second==pausedSecond);
    press("继续");assert(state()=="comparing");
    const unsigned remaining=game().compareRemaining;
    assert(remaining>50&&remaining<=MemoryEngine::CompareMs);
    advance(remaining-50);assert(state()=="comparing"&&game().visible(0)&&game().visible(other));
    advance(100);assert(state()=="playing"&&game().first==-1&&game().second==-1&&!game().visible(0)&&!game().visible(other));

    fresh();earnFromNewGame();save("memory-score");
    const auto score=game().score;const auto moves=game().moves;
    for(int id=0;id<game().count;++id)if(game().cards[id].matched){tapCard(id);break;}
    assert(game().score==score&&game().moves==moves&&game().first==-1);
    for(int i=0;i<12;++i){press("暂停");assert(state()=="paused");press("继续");assert(state()=="playing");}
    const auto beforeRebuild=game().elapsedMs;show(App::Memory);
    assert(state()=="playing"&&game().elapsedMs==beforeRebuild);

    fresh();const int y=32+game().top()+15;
    sample(55,y,true);sample(125,y,true);sample(55,y,true);sample(55,y,false);
    assert(game().first==-1&&game().moves==0&&game().score==0);
    swipe(55,y,20,16);assert(app==App::Memory&&!renderPending&&game().first==-1&&game().moves==0);
    swipe(55,y,125,y);assert(game().first==-1&&game().score==0);
    // Held contact on a destroyed board is consumed after a same-App rebuild.
    sample(55,y,true);show(App::Memory);advance(800);sample(125,y,true);sample(125,y,false);
    assert(game().first==-1&&game().moves==0&&game().score==0);
    tapCard(0);assert(game().first==0);tapCard(mateFor(0));assert(game().score==100);
    const auto best=game().best;show(App::None);assert(state()=="paused");show(App::Memory);assert(state()=="paused");
    press("继续");assert(state()=="playing");press("暂停");press("重新开始");
    assert(state()=="playing"&&game().score==0&&game().best==best&&game().pairs==0&&game().moves==0&&game().level==1);

    // Three real generated levels, solved only through pointer taps. Perfect
    // level rewards are 800 + 1000 + 1200; no score or state is injected.
    fresh();clearLevel();assert(state()=="levelcleared"&&game().level==1&&game().score==800);save("memory-levelcleared");
    press("下一关");assert(state()=="playing"&&game().level==2&&game().count==16&&game().pairs==0);save("memory-level2");
    clearLevel();assert(state()=="levelcleared"&&game().score==1800);press("下一关");
    assert(state()=="playing"&&game().level==3&&game().count==20);save("memory-level3");clearLevel();
    assert(state()=="won"&&game().score==3000&&game().best>=3000&&game().pairs==10);save("memory-won");
    press("再来一局");assert(state()=="playing"&&game().score==0&&game().level==1);press("暂停");press("返回",1);
    assert(app==App::Games&&renderPending);
    GiftUI::setAppsPage(0);show(App::None);save("launcher-1",false);
    GiftUI::setAppsPage(1);show(App::None);save("launcher-2",false);
    std::cout<<"PASS: actual card coordinate pair reward, mismatched third-card rejection, 750ms comparison paused/resumed with frozen framebuffer, permanent drag/header cancellation, held rebuild isolation, leave/reentry/restart, 12 overlay cycles and three generated levels won for 3000\n";
}
int persistent(const std::string &mode) {
    GiftUI::setAppsPage(3);show(App::Memory);
    if(mode=="best-restore")assert(MemoryPage::bestScore()>0);
    else if(mode=="best-pause"||mode=="best-leave"){
        press("开始游戏");earnFromNewGame();
        if(mode=="best-pause"){press("暂停");assert(state()=="paused");}
        else{show(App::None);assert(state()=="paused");}
    }else return 2;
    std::cout<<"BEST="<<MemoryPage::bestScore()<<"\nPASS process "<<mode<<"\n";return 0;
}
void init() {
    LittleFS.directory = "build/memory-host/store";
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
