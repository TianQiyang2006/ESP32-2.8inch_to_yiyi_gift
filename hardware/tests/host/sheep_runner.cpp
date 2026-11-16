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
#include "hardware/arduino/yiyi_desk/src/pages/sheep_engine.h"
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
std::string state() { return SheepPage::status().state; }
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
    SheepPage::refresh(true);
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
const SheepEngine::Game &game() { return SheepPage::debugGame(); }
void pick(int id) {
    const auto tile = game().tiles[id];
    assert(game().canSelect(id));
    const auto before = SheepPage::status().remaining;
    tap(tile.x + 16, tile.y + 32 + 16);
    SheepPage::refresh(true);
    assert(SheepPage::status().remaining + 1 == before);
}
void fresh() {
    fingerDown = false; lv_indev_reset(pointer, nullptr);
    SheepPage::debugVariant(0); press("开始游戏");
    assert(state() == "playing");
}
void playProof(bool firstTripleOnly = false) {
    assert(game().proofValid);
    const auto initialScore = game().score;
    const auto count = game().tileCount;
    std::vector<int> path(game().proof, game().proof + count);
    for (int id : path) {
        if (game().tiles[id].removed) continue;
        if (game().remaining == 1) {
            assert(lv_obj_has_state(control("洗牌"), LV_STATE_DISABLED));
            const auto randomState = game().randomState;
            press("洗牌");
            assert(game().remaining == 1 && game().proofValid && game().shuffleAvailable &&
                   game().randomState == randomState);
        }
        assert(state() == "playing"); pick(id);
        if (firstTripleOnly && game().score > initialScore) return;
    }
    assert(state() == "levelcleared" || state() == "won");
}
std::vector<int> losingPath(const SheepEngine::Game &initial) {
    // Search copies only. The discovered legal order is then played through
    // actual pointer coordinates on the untouched production page.
    uint32_t random = 20261113u;
    for (int attempt = 0; attempt < 512; ++attempt) {
        auto search = initial;
        std::vector<int> path;
        for (unsigned step = 0; search.state == SheepEngine::State::Playing && step < SheepEngine::MaxTiles; ++step) {
            std::vector<int> choices, preferred;
            for (int id = 0; id < search.tileCount; ++id) {
                if (!search.canSelect(id)) continue;
                choices.push_back(id);
                unsigned same = 0;
                for (int t = 0; t < search.trayCount; ++t) same += search.tray[t] == search.tiles[id].type;
                if (same != 2) preferred.push_back(id);
            }
            if (choices.empty()) break;
            if (!preferred.empty() && (attempt % 3 != 0)) choices = preferred;
            random = random * 1664525u + 1013904223u;
            const int chosen = choices[(random >> 8) % choices.size()];
            assert(search.select(chosen)); path.push_back(chosen);
        }
        if (search.state == SheepEngine::State::GameOver) return path;
    }
    std::cerr << "No full-tray losing path found for actual board\n"; assert(false);
    return {};
}
void lose() {
    const auto path = losingPath(game());
    for (int id : path) pick(id);
    assert(state() == "gameover" && SheepPage::status().trayCount == 7);
}
void blockedTile() {
    for (int id = 0; id < game().tileCount; ++id) {
        const auto tile = game().tiles[id];
        if (tile.removed || game().exposed(id)) continue;
        for (int y = tile.y + 1; y < tile.y + 31; ++y)
            for (int x = tile.x + 1; x < tile.x + 31; ++x) {
                if (game().hitTest(x, y) != id) continue;
                const auto before = SheepPage::status();
                tap(x, y + 32);
                assert(SheepPage::status().remaining == before.remaining &&
                       SheepPage::status().trayCount == before.trayCount);
                return;
            }
    }
    std::cerr << "Expected at least one visible but blocked lower tile\n"; assert(false);
}
void uiTests() {
    GiftUI::setAppsPage(1); show(App::None); save("launcher-2", false);
    press("游戏"); assert(app == App::Games); show(App::Games);
    save("games-1", false);
    assert(label("跳一跳") && label("羊了个羊"));
    press("羊了个羊"); assert(app == App::Sheep && renderPending); show(App::Sheep);
    assert(state() == "ready"); save("sheep-ready");
    press("返回", 1); assert(app == App::Games && renderPending);
    show(App::Games); press("羊了个羊"); show(App::Sheep); press("开始游戏");
    assert(state() == "playing" && SheepPage::status().remaining == 18 && SheepPage::status().level == 1);
    save("sheep-start"); blockedTile();
    // Sliding off a tile cancels its selection. A press that began on the
    // board must also never activate the back button on release over it.
    const auto from = game().tiles[game().proof[0]];
    swipe(from.x + 16, from.y + 48, 20, 16);
    assert(state() == "playing" && app == App::Sheep && !renderPending &&
           game().remaining == 18 && game().trayCount == 0);
    int other = -1;
    for (int id = 0; id < game().tileCount; ++id)
        if (id != game().proof[0] && game().canSelect(id)) { other = id; break; }
    assert(other >= 0);
    const auto to = game().tiles[other];
    swipe(from.x + 16, from.y + 48, to.x + 16, to.y + 48);
    assert(state() == "playing" && app == App::Sheep && !renderPending &&
           game().remaining == 18 && game().trayCount == 0);
    const int first = game().proof[0]; pick(first);
    assert(SheepPage::status().trayCount == 1); save("sheep-tray");
    press("撤回");
    assert(SheepPage::status().trayCount == 0 && SheepPage::status().remaining == 18);
    assert(!SheepPage::status().undoAvailable);
    const auto beforeRepeatUndo = SheepPage::status().remaining; press("撤回");
    assert(SheepPage::status().remaining == beforeRepeatUndo);

    // Rebuilding a page while a tile is pressed must consume the old contact;
    // only a fresh press/release may select it afterward.
    fresh(); const auto tile = game().tiles[game().proof[0]];
    sample(tile.x+16, tile.y+48, true); show(App::Sheep);
    sample(tile.x+16, tile.y+48, false); advance(40);
    assert(state() == "playing" && SheepPage::status().remaining == 18);
    pick(game().proof[0]);

    fresh(); playProof(true);
    assert(game().score > 0 && game().trayCount < 7); save("sheep-triple");
    press("暂停"); assert(state() == "paused"); save("sheep-paused");
    const auto paused = SheepPage::status(); advance(2000);
    assert(SheepPage::status().remaining == paused.remaining && SheepPage::status().score == paused.score);
    press("继续"); assert(state() == "playing");
    for (int i = 0; i < 24; ++i) {
        press("暂停"); assert(state() == "paused"); press("继续"); assert(state() == "playing");
    }

    // Shuffle changes only unpicked faces, preserves their counts and cannot
    // be used a second time during the level.
    int beforeTypes[SheepEngine::TypeCount] = {};
    for (int id = 0; id < game().tileCount; ++id) if (!game().tiles[id].removed) ++beforeTypes[game().tiles[id].type];
    const auto remaining = game().remaining, trayCount = game().trayCount;
    press("洗牌"); assert(!SheepPage::status().shuffleAvailable && !game().proofValid);
    int afterTypes[SheepEngine::TypeCount] = {};
    for (int id = 0; id < game().tileCount; ++id) if (!game().tiles[id].removed) ++afterTypes[game().tiles[id].type];
    assert(!memcmp(beforeTypes, afterTypes, sizeof(beforeTypes)) && game().remaining == remaining && game().trayCount == trayCount);
    std::vector<uint8_t> shuffled;
    for (int id = 0; id < game().tileCount; ++id) shuffled.push_back(game().tiles[id].type);
    press("洗牌");
    for (int id = 0; id < game().tileCount; ++id) assert(game().tiles[id].type == shuffled[id]);

    // Natural loss and actual last-move undo restore six occupied tray slots.
    fresh(); lose(); save("sheep-gameover"); assert(game().canUndo());
    const auto beforeUndo = game().remaining;
    press("撤回", 1);
    assert(state() == "playing" && game().trayCount == 6 && game().remaining == beforeUndo + 1 && !game().canUndo());
    show(App::None); assert(state() == "paused"); show(App::Sheep); assert(state() == "paused");
    press("继续"); assert(state() == "playing");
    press("暂停"); const auto best = game().best; press("重新开始");
    assert(state() == "playing" && game().score == 0 && game().best == best && game().remaining == 18);

    // Both stages are solved from a real new game's certificate using the same
    // board coordinates as the user, including every upper-layer dependency.
    fresh(); playProof(); assert(state() == "levelcleared" && game().remaining == 0 && game().trayCount == 0);
    save("sheep-levelcleared"); press("下一关");
    assert(state() == "playing" && game().level == 2 && game().remaining == 54);
    assert(game().shuffleAvailable && !game().undoAvailable); save("sheep-level2");
    blockedTile(); playProof(); assert(state() == "won" && game().remaining == 0 && game().trayCount == 0);
    save("sheep-won"); press("再来一局");
    assert(state() == "playing" && game().level == 1 && game().score == 0);
    press("暂停"); press("返回", 1); assert(app == App::Games && renderPending); show(App::None);
    GiftUI::setAppsPage(0); show(App::None); save("launcher-1", false);
    GiftUI::setAppsPage(1); show(App::None); save("launcher-2", false);
    std::cout << "PASS: actual coordinate tile picks, drag cancellation, covered tile rejection, triples, full tray/undo, one-use tools, final-card shuffle disabled, "
                 "two certified stage wins, pause/restart/leave/rebuild, 24 overlay cycles and launcher entry\n";
}
int persistent(const std::string &mode) {
    GiftUI::setAppsPage(2); show(App::Sheep);
    if (mode == "best-restore") assert(SheepPage::bestScore() > 0);
    else if (mode == "best-play" || mode == "best-leave") {
        press("开始游戏"); playProof(true); assert(game().score > 0);
        if (mode == "best-play") {
            if (state() == "levelcleared") press("下一关");
            lose();
        } else { show(App::None); assert(state() == "paused"); }
    } else return 2;
    std::cout << "BEST=" << SheepPage::bestScore() << "\nPASS process " << mode << "\n";
    return 0;
}
void init() {
    LittleFS.directory = "build/sheep-host/store";
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
