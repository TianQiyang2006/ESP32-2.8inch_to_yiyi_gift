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
#include "hardware/arduino/yiyi_desk/src/pages/mines_engine.h"
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
std::string state() { return MinesPage::status().state; }
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
    MinesPage::refresh(true);
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
const MinesEngine::Game &game() { return MinesPage::debugGame(); }
void tapCell(int id) {
    tap(MinesEngine::BoardX+(id%MinesEngine::Cols)*MinesEngine::CellSize+14,
        32+MinesEngine::BoardY+(id/MinesEngine::Cols)*MinesEngine::CellSize+14);
    MinesPage::refresh(true);
}
void holdCell(int id, uint32_t ms = 550) {
    const int x=MinesEngine::BoardX+(id%MinesEngine::Cols)*28+14;
    const int y=32+MinesEngine::BoardY+(id/MinesEngine::Cols)*28+14;
    sample(x,y,true); advance(ms); sample(x,y,false); MinesPage::refresh(true);
}
int coveredCell(bool wantMine) {
    for(int id=0;id<MinesEngine::Cells;++id)
        if(!game().cells[id].revealed&&!game().cells[id].flagged&&game().cells[id].mine==wantMine)return id;
    assert(false);return -1;
}
void fresh() {
    fingerDown=false;lv_indev_reset(pointer,nullptr);
    MinesPage::debugVariant(0);press("开始游戏");assert(state()=="playing");
}
void earnFromNewGame() {
    assert(state()=="playing"&&!game().generated&&game().score==0);
    // The first real coordinate tap generates the original board and earns
    // points through the production reveal/flood implementation.
    tapCell(25);
    assert(game().generated&&game().score>0&&game().best>=game().score);
}
void uiTests() {
    GiftUI::setAppsPage(1); show(App::None); save("launcher-2", false);
    press("游戏"); assert(app == App::Games); show(App::Games);
    assert(GiftUI::swipeApps(-80)); show(App::Games);
    save("games-2", false);
    assert(label("扫雷"));press("扫雷");assert(app==App::Mines&&renderPending);show(App::Mines);
    assert(state()=="ready");save("mines-ready");press("返回",1);assert(app==App::Games&&renderPending);
    show(App::Games);press("扫雷");show(App::Mines);press("开始游戏");
    assert(state()=="playing"&&!game().generated&&game().remaining==48);save("mines-start");
    // Mode buttons and long holds are actual pointer contacts. Neither places
    // mines or starts the clock before a safe reveal.
    press("标记关");assert(game().flagMode);tapCell(0);
    assert(game().flags==1&&game().cells[0].flagged&&!game().generated);save("mines-flag-mode");
    tapCell(0);assert(game().flags==0&&!game().cells[0].flagged);press("标记开");assert(!game().flagMode);
    holdCell(1);assert(game().flags==1&&game().cells[1].flagged&&!game().cells[1].revealed&&!game().generated);
    save("mines-long-flag");holdCell(1,900);
    assert(game().flags==0&&!game().cells[1].flagged&&!game().cells[1].revealed&&!game().generated);
    // Sparse sampling can report release as the first event after 500ms.
    // The elapsed hold still marks the tile instead of revealing it.
    sample(90,78,true);lv_timer_pause(pointer->driver->read_timer);advance(510);
    lv_timer_resume(pointer->driver->read_timer);sample(90,78,false);
    assert(game().flags==1&&game().cells[2].flagged&&!game().cells[2].revealed&&!game().generated);
    holdCell(2);assert(game().flags==0&&!game().cells[2].flagged&&!game().generated);
    // A drag cancels permanently, including returning to the original cell.
    sample(34,78,true);sample(90,78,true);sample(34,78,true);sample(34,78,false);
    assert(!game().generated&&game().flags==0);
    swipe(34,78,20,16);assert(app==App::Mines&&!renderPending&&!game().generated);
    swipe(34,78,62,78);assert(!game().generated);
    // A short held contact is a reveal; it must not become a flag later.
    sample(174,134,true);advance(300);sample(174,134,false);
    assert(game().generated&&game().score>0&&game().flags==0);save("mines-opening");
    const auto openedScore=game().score;tapCell(25);assert(game().score==openedScore);
    const int mineId=coveredCell(true);holdCell(mineId);assert(game().cells[mineId].flagged&&game().flags==1);
    tapCell(mineId);assert(state()=="playing"&&game().cells[mineId].flagged);save("mines-board");
    press("暂停");assert(state()=="paused");save("mines-paused");
    const auto pausedTime=game().elapsedMs,pausedScore=game().score;
    std::vector<lv_color_t> pausedPixels(pixels,pixels+320*240);
    advance(1500);lv_refr_now(nullptr);
    assert(game().elapsedMs==pausedTime&&game().score==pausedScore&&!memcmp(pausedPixels.data(),pixels,sizeof(pixels)));
    press("继续");assert(state()=="playing");
    for(int i=0;i<12;++i){press("暂停");assert(state()=="paused");press("继续");assert(state()=="playing");}
    const auto beforeRebuild=game().elapsedMs;show(App::Mines);
    assert(state()=="playing"&&game().elapsedMs==beforeRebuild);
    // Same-app rebuild while pressed consumes the held contact; dragging and
    // its eventual release may not reveal or flag a fresh cell.
    fresh();sample(174,134,true);show(App::Mines);advance(650);sample(202,134,true);sample(202,134,false);
    assert(!game().generated&&game().flags==0);tapCell(25);assert(game().score>0);
    const auto best=game().best;show(App::None);assert(state()=="paused");show(App::Mines);assert(state()=="paused");
    press("继续");assert(state()=="playing");press("暂停");press("重新开始");
    assert(state()=="playing"&&game().score==0&&game().best==best&&!game().generated&&game().elapsedMs==0);
    fresh();earnFromNewGame();const int losingCell=coveredCell(true);tapCell(losingCell);
    assert(state()=="gameover"&&game().exploded==losingCell);save("mines-gameover");press("再来一局");assert(state()=="playing"&&!game().generated);
    // Complete a genuinely generated board through only tile coordinates. The
    // snapshot supplies the oracle, never a private mutation or fake UI event.
    fresh();earnFromNewGame();
    for(int id=0;id<MinesEngine::Cells;++id)
        if(!game().cells[id].mine&&!game().cells[id].revealed)tapCell(id);
    assert(state()=="won"&&game().score==MinesEngine::MaximumScore&&game().remaining==0&&game().flags==12);
    save("mines-won");press("返回",1);assert(app==App::Games&&renderPending);
    GiftUI::setAppsPage(0);show(App::None);save("launcher-1",false);
    GiftUI::setAppsPage(1);show(App::None);save("launcher-2",false);
    std::cout<<"PASS: actual coordinate safe opening/flood and natural win/loss, explicit flag mode, 500ms hold flags without release reveal, drag/header cancellation, exact paused framebuffer/time, held rebuild isolation, pause/restart/back/reentry and 12 overlay cycles\n";
}
int persistent(const std::string &mode) {
    GiftUI::setAppsPage(2);show(App::Mines);
    if(mode=="best-restore")assert(MinesPage::bestScore()>0&&MinesPage::bestScore()<=MinesEngine::MaximumScore);
    else if(mode=="best-pause"||mode=="best-leave"){
        press("开始游戏");earnFromNewGame();
        if(mode=="best-pause"){press("暂停");assert(state()=="paused");}
        else{show(App::None);assert(state()=="paused");}
    }else return 2;
    std::cout<<"BEST="<<MinesPage::bestScore()<<"\nPASS process "<<mode<<"\n";return 0;
}
void init() {
    LittleFS.directory = "build/mines-host/store";
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
