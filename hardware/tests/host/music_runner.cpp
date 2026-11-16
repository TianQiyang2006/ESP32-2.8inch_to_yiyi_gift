// Host-only offscreen renderer for the music app. It drives the real
// GiftUI::render()/MusicPage widgets and the real button callbacks; fixtures
// below exist only in this host executable (the firmware never links them).
#include "Arduino.h"
#include "LittleFS.h"
#include "Preferences.h"
#include "hardware/arduino/yiyi_desk/src/gift_ui.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/app_state.h"
#include "hardware/arduino/yiyi_desk/src/ui_widgets.h"
#include "hardware/arduino/yiyi_desk/src/pages/mini_apps.h"
#include "hardware/arduino/yiyi_desk/src/pages/music_page.h"
#include "hardware/arduino/yiyi_desk/src/pages/page_api.h"
#include "hardware/arduino/yiyi_desk/src/music_player.h"
#include "hardware/arduino/yiyi_desk/src/radio_player.h"
#include "hardware/arduino/yiyi_desk/src/pages/radio_page.h"
#include "hardware/arduino/yiyi_desk/src/saved_design/saved_design.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int64_t HostEpoch = 0;  // set in init(); Tetris has no calendar dependency
lv_color_t pixels[320 * 240], buffer[320 * 40];
std::string out;
unsigned frames = 0;
lv_indev_t *pointer = nullptr;
lv_point_t pointerPoint{};
bool pointerDown = false;

void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            if (x >= 0 && x < 320 && y >= 0 && y < 240) pixels[y * 320 + x] = *data;
            ++data;
        }
    lv_disp_flush_ready(drv);
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
lv_obj_t *label(const char *value, unsigned skip = 0, lv_obj_t *top = nullptr) {
    return findLabel(top ? top : root, value, skip);
}
void checkLabels(lv_obj_t *obj, bool inRoller = false) {
    inRoller = inRoller || lv_obj_check_type(obj, &lv_roller_class);
    if (lv_obj_check_type(obj, &lv_label_class) && !inRoller && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        const char *value = lv_label_get_text(obj);
        auto *font = lv_obj_get_style_text_font(obj, 0);
        lv_point_t size{};
        lv_txt_get_size(&size, value, font, lv_obj_get_style_text_letter_space(obj, 0),
                        lv_obj_get_style_text_line_space(obj, 0), LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
        if (size.x > lv_obj_get_width(obj)) {
            std::cerr << "Clipped label: " << value << " " << size.x << "/" << lv_obj_get_width(obj) << "\n";
            assert(false);
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) checkLabels(lv_obj_get_child(obj, i), inRoller);
}
void save(const std::string &name, bool validate = true) {
    lv_obj_update_layout(root);
    if (validate) {
        checkLabels(root);
        checkLabels(lv_layer_top());
    }
    lv_refr_now(nullptr);
    std::ofstream file(out + "/" + name + ".rgb565", std::ios::binary);
    file.write((char *)pixels, sizeof(pixels));
    ++frames;
    std::cout << "frame " << name << "\n";
}
// Rebuilds the current view exactly like desk.cpp does for a page switch. The
// App/page are set before leaveView(), mirroring desk.cpp (which sets app before
// calling render()); TetrisPage::cancel() uses `app` to tell "left the app" from
// "same-app background rebuild".
void show(App target, uint8_t targetPage = 6) {
    app = target;
    page = targetPage;
    GiftUI::leaveView();
    Ui::cancelAnimations();
    Ui::clearStatusCluster();
    lv_obj_clean(root);
    SavedDesign::invalidate();
    assert(GiftUI::render(root));
    lv_obj_update_layout(root);
}
void click(const char *value, unsigned nth = 0, lv_obj_t *top = nullptr) {
    std::cout << "click " << value << "\n";
    unsigned skip = nth;
    auto *caption = findLabel(top ? top : root, value, skip);
    if (!caption) std::cerr << "Missing caption: " << value << "\n";
    assert(caption);
    lv_obj_t *target = caption;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    lv_event_send(target, LV_EVENT_CLICKED, nullptr);
    MusicPage::refresh(true);
    lv_obj_update_layout(root);
}
// Emulates a real finger: PRESSED -> RELEASED -> CLICKED, then lets the shared
// press-feedback animation finish. The device sends the same sequence, which is
// what exposed a stale press-feedback handle after an overlay deleted its button.
void settle(uint32_t ms = 240) {
    for (uint32_t done = 0; done < ms; done += 20) {
        const uint32_t step = ms - done < 20 ? ms - done : 20;
        HostMillis += step;
        lv_tick_inc(step);
        lv_timer_handler();
    }
    lv_obj_update_layout(root);
}
void pressTap(const char *value, unsigned nth = 0) {
    std::cout << "press " << value << std::endl;
    unsigned skip = nth;
    auto *caption = findLabel(root, value, skip);
    assert(caption);
    lv_obj_t *target = caption;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    lv_event_send(target, LV_EVENT_PRESSED, nullptr);
    lv_event_send(target, LV_EVENT_RELEASED, nullptr);
    lv_event_send(target, LV_EVENT_CLICKED, nullptr);
    MusicPage::refresh(true);
    settle();
}
void longPressRepeat(const char *value) {
    unsigned skip = 0;
    auto *caption = findLabel(root, value, skip);
    assert(caption);
    lv_obj_t *target = caption;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) target = lv_obj_get_parent(target);
    assert(target);
    lv_event_send(target, LV_EVENT_LONG_PRESSED_REPEAT, nullptr);
    MusicPage::refresh(true);
}
void touch(int x, int y) {
    lv_obj_update_layout(root);
    lv_obj_t *target = nullptr;
    auto search = [&](auto &self, lv_obj_t *p) -> void {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) return;
        lv_area_t a; lv_obj_get_coords(p, &a);
        if (x < a.x1 || x > a.x2 || y < a.y1 || y > a.y2) return;
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_CLICKABLE)) target = p;
        for (unsigned i = 0; i < lv_obj_get_child_cnt(p); ++i) self(self, lv_obj_get_child(p, i));
    };
    search(search, root);
    assert(target);
    lv_event_send(target, LV_EVENT_CLICKED, nullptr);
    MusicPage::refresh(true);
    lv_obj_update_layout(root);
}
// Advances both the firmware millis() clock and LVGL's tick, then runs the real
// lv_timer callbacks (including TetrisPage's gravity timer).
void advance(uint32_t ms) {
    for (uint32_t done = 0; done < ms; done += 20) {
        const uint32_t step = ms - done < 20 ? ms - done : 20;
        HostMillis += step;
        lv_tick_inc(step);
        lv_timer_handler();
    }
    lv_obj_update_layout(root);
}
uint32_t pixelDiff(const lv_color_t *before, const lv_color_t *after) {
    uint32_t changed = 0;
    for (int i = 0; i < 320 * 240; ++i)
        if (before[i].full != after[i].full) ++changed;
    return changed;
}
std::vector<lv_color_t> snapshot() { return std::vector<lv_color_t>(pixels, pixels + 320 * 240); }


void rebuild() {if(renderPending){renderPending=false;show(app,page);}}
void tapSymbol(const char *s) {click(s);rebuild();}
// Pump the pointer driver and the same refresh/rebuild gate as deskLoop().
// Sending CLICKED directly bypasses hit testing, scroll cancellation and RELEASED.
void pumpInput(unsigned ms) {
    for (unsigned time=0;time<ms;time+=5) {
        HostMillis+=5;lv_tick_inc(5);
        if(renderPending&&!GiftUI::inputActive())rebuild();
        if(app==App::Music)MusicPage::refresh(false);
        lv_timer_handler();
    }
    lv_obj_update_layout(root);
}
void pointerTap(int x,int y) {
    pointerPoint={(lv_coord_t)x,(lv_coord_t)y};pointerDown=true;pumpInput(100);
    pointerDown=false;pumpInput(160);
}
void pointerDrag(int x,int from,int to) {
    pointerPoint={(lv_coord_t)x,(lv_coord_t)from};pointerDown=true;pumpInput(80);
    for(int i=1;i<=12;++i){pointerPoint.y=from+(to-from)*i/12;pumpInput(30);}
    pointerDown=false;pumpInput(400);
}
void pointerTests() {
    GiftPages::openApp((int)App::Music);rebuild();pumpInput(100);
    pointerTap(284,214);assert(label("播放列表"));
    pointerTap(100,56);assert(MusicPlayer::state().playlist==0);
    pointerTap(108,134);assert(MusicPlayer::state().track==1);assert(!label("播放列表"));
    pointerTap(284,214);assert(label("播放列表"));
    pointerDrag(120,180,95);assert(MusicPlayer::state().track==1);assert(label("播放列表"));save("music-scrolled",false);
    auto *scroll=lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(label("歌曲 6"))));
    int y=lv_obj_get_scroll_y(scroll);assert(y>=40);
    auto *content=lv_obj_get_child(scroll,0);
    for(unsigned i=0;i<5;++i)assert((uintptr_t)lv_obj_get_user_data(lv_obj_get_child(content,i))==unsigned(y/40)+i);
    int target=y/40+1;
    pointerTap(108,138-y%40);
    assert(!label("播放列表"));assert(MusicPlayer::state().track==target);
    pointerTap(284,214);assert(label("播放列表"));
    scroll=lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(label("歌曲 6"))));
    assert(lv_obj_get_scroll_y(scroll)==y);
    lv_obj_scroll_to_y(scroll,0,LV_ANIM_OFF);pumpInput(100);
    lv_obj_scroll_by(scroll,0,80,LV_ANIM_OFF);MusicPage::refresh(true);lv_obj_update_layout(root);
    auto *first=lv_obj_get_parent(label("晴天"));assert(!lv_obj_has_flag(first,LV_OBJ_FLAG_HIDDEN));
    lv_obj_scroll_to_y(scroll,0,LV_ANIM_OFF);pumpInput(100);
    lv_obj_add_state(first,LV_STATE_PRESSED);lv_event_send(first,LV_EVENT_PRESSED,nullptr);
    lv_obj_scroll_to_y(scroll,80,LV_ANIM_OFF);MusicPage::refresh(true);
    assert((uintptr_t)lv_obj_get_user_data(first)==0);
    lv_obj_clear_state(first,LV_STATE_PRESSED);lv_event_send(first,LV_EVENT_PRESS_LOST,nullptr);
    lv_event_send(first,LV_EVENT_CLICKED,nullptr);assert(MusicPlayer::state().track==target);
    assert(label("播放列表"));
    pointerTap(41,13);assert(!label("播放列表"));assert(app==App::Music);
    MusicPlayer::play(2);MusicPage::refresh(true);pointerTap(150,149);
    auto *viewport=lv_obj_get_parent(label(MusicPlayer::lyric().c_str()));
    pointerPoint={140,128};pointerDown=true;pumpInput(80);
    pointerPoint.y=80;pumpInput(100);assert(lv_obj_is_scrolling(viewport));
    lv_event_send(lv_obj_get_parent(label(LV_SYMBOL_LEFT)),LV_EVENT_CLICKED,nullptr);
    assert(!GiftUI::inputActive());pointerDown=false;rebuild();pumpInput(100);assert(app==App::Music);
    pointerTap(41,13);assert(app==App::None&&page==6);
    std::cout<<"PASS real LVGL pointer: list tap, scrolling without selection, restored scroll, overscroll, pressed identity, cancelled press, back during lyric scrolling and to launcher\n";
}
void checkLongTitles(lv_obj_t *obj, unsigned &count) {
    if (lv_obj_check_type(obj, &lv_label_class) &&
        !strncmp(lv_label_get_text(obj), "You're", 6)) {
        // DOT only limits wrapping when the label has an explicit one-line height.
        assert(lv_obj_get_height(obj) == lv_obj_get_style_text_font(obj, 0)->line_height);
        auto *artist = lv_obj_get_child(lv_obj_get_parent(obj), 1);
        if (lv_obj_get_parent(obj) != root) {
            lv_area_t titleBox{}, artistBox{};
            lv_obj_get_coords(obj, &titleBox);
            lv_obj_get_coords(artist, &artistBox);
            // The mini player also contains a cover image before its two labels.
            if (!lv_obj_check_type(artist, &lv_label_class) || artist == obj)
                artist = lv_obj_get_child(lv_obj_get_parent(obj), 2);
            lv_obj_get_coords(artist, &artistBox);
            assert(titleBox.y2 < artistBox.y1);
        }
        ++count;
    }
    for (unsigned i=0;i<lv_obj_get_child_cnt(obj);++i)
        checkLongTitles(lv_obj_get_child(obj,i),count);
}
void uiTests() {
    GiftUI::setAppsPage(1);show(App::None,6);save("launcher-2",false);
    const char *names[]={"闹钟","番茄时钟","音乐","游戏","倒计时","倒数日"};
    for(unsigned i=0;i<6;++i){auto*tile=lv_obj_get_parent(label(names[i]));lv_area_t a;lv_obj_get_coords(tile,&a);assert(a.x1==20+(i%3)*100&&a.y1==16+(i/3)*109);}
    assert(!label("游戏厅")&&!label("小黑游戏厅"));
    click("音乐");assert(app==App::Music);rebuild();save("music-player",false);
    lv_area_t iconArea{},valueArea{};lv_obj_get_coords(label(LV_SYMBOL_VOLUME_MAX),&iconArea);lv_obj_get_coords(label("40"),&valueArea);assert(iconArea.x2+4<valueArea.x1);
    assert(!label("播放列表"));
    for(unsigned i=0;i<lv_obj_get_child_cnt(root);++i){auto*o=lv_obj_get_child(root,i);if(lv_obj_check_type(o,&lv_slider_class)){lv_obj_add_state(o,LV_STATE_PRESSED);assert(GiftUI::inputActive());lv_obj_clear_state(o,LV_STATE_PRESSED);assert(!GiftUI::inputActive());lv_slider_set_value(o,420,LV_ANIM_OFF);lv_event_send(o,LV_EVENT_RELEASED,nullptr);assert(MusicPlayer::state().position==112980);}}
    tapSymbol(LV_SYMBOL_PAUSE);assert(MusicPlayer::state().paused);
    MusicPlayer::seek(42000);assert(MusicPlayer::state().paused&&MusicPlayer::state().position==42000);
    tapSymbol(LV_SYMBOL_PLAY);assert(!MusicPlayer::state().paused);
    tapSymbol(LV_SYMBOL_NEXT);assert(MusicPlayer::state().track==1);
    tapSymbol(LV_SYMBOL_PREV);assert(MusicPlayer::state().track==0);
    tapSymbol(LV_SYMBOL_LOOP);assert(label("播放模式"));save("music-mode",false);
    touch(150,130);rebuild();assert(MusicPlayer::state().mode==1);assert(label("单曲循环"));
    tapSymbol(LV_SYMBOL_LOOP);touch(150,173);rebuild();assert(MusicPlayer::state().mode==2);assert(label("随机播放"));
    tapSymbol(LV_SYMBOL_NEXT);int shuffled=MusicPlayer::state().track;assert(shuffled!=0);
    tapSymbol(LV_SYMBOL_PREV);assert(MusicPlayer::state().track==0);
    tapSymbol(LV_SYMBOL_NEXT);assert(MusicPlayer::state().track==shuffled);
    tapSymbol(LV_SYMBOL_SHUFFLE);touch(150,88);rebuild();assert(MusicPlayer::state().mode==0);
    MusicPlayer::play(0);MusicPage::refresh(true);
    tapSymbol(LV_SYMBOL_VOLUME_MAX);save("music-volume",false);
    click("60");assert(MusicPlayer::state().volume==60);tapSymbol(LV_SYMBOL_MINUS);assert(MusicPlayer::state().volume==55);
    tapSymbol(LV_SYMBOL_PLUS);assert(MusicPlayer::state().volume==60);
    click("40");assert(MusicPlayer::state().volume==40);
    tapSymbol(LV_SYMBOL_CLOSE);
    click("让每一天都慢慢发光");rebuild();save("music-lyrics",false);
    assert(label("把心事轻轻唱给你听"));
    tapSymbol(LV_SYMBOL_LIST);assert(label("播放列表"));save("music-playlist",false);
    click("依依歌单");rebuild();assert(MusicPlayer::state().playlist==1);
    MusicPlayer::toggle();assert(MusicPlayer::state().paused);
    pressTap("晚星");rebuild();assert(MusicPlayer::state().track==1&&MusicPlayer::state().playing&&!MusicPlayer::state().paused&&MusicPlayer::state().position==0);assert(!label("播放列表"));
    MusicPlayer::play(2);MusicPage::refresh(true);save("music-long-title",false);
    tapSymbol(LV_SYMBOL_LIST);save("music-long-title-list",false);
    unsigned longTitles=0;checkLongTitles(root,longTitles);assert(longTitles==2);
    assert(GiftUI::back());rebuild();
    String longLyric=MusicPlayer::lyric();click(longLyric.c_str());rebuild();
    auto*current=label(longLyric.c_str());assert(current);lv_obj_update_layout(root);
    auto*viewport=lv_obj_get_parent(current);assert(lv_label_get_long_mode(current)==LV_LABEL_LONG_WRAP);
    assert(lv_obj_get_height(current)>lv_obj_get_height(viewport));
    lv_area_t bounds;lv_obj_get_coords(viewport,&bounds);assert(bounds.y1==69&&bounds.y2==136);
    lv_obj_scroll_to_y(viewport,25,LV_ANIM_OFF);auto scrollY=lv_obj_get_scroll_y(viewport);
    MusicPage::refresh(true);assert(lv_obj_get_scroll_y(viewport)==scrollY);
    save("music-lyrics-long",false);MusicPlayer::play(1);MusicPage::refresh(true);assert(lv_obj_get_scroll_y(viewport)==0);
    assert(GiftUI::back());rebuild();
    for(unsigned i=0;i<20;++i){tapSymbol(LV_SYMBOL_LIST);assert(GiftUI::back());rebuild();tapSymbol(LV_SYMBOL_VOLUME_MAX);assert(GiftUI::back());rebuild();}
    show(App::None,6);assert(label("音乐"));
    std::cout<<"PASS music: paused seek, mode picker, shuffle history, direct row play from pause, wrapped/scrollable lyrics, 40 overlay rebuilds\n";
}
void init() {
    LittleFS.directory = "build/music-host/store";
    lv_init();
    static lv_disp_draw_buf_t drawbuf;
    lv_disp_draw_buf_init(&drawbuf, buffer, nullptr, 320 * 40);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = 320;
    driver.ver_res = 240;
    driver.draw_buf = &drawbuf;
    driver.flush_cb = flush;
    lv_disp_drv_register(&driver);
    static lv_indev_drv_t inputDriver;
    lv_indev_drv_init(&inputDriver);inputDriver.type=LV_INDEV_TYPE_POINTER;
    inputDriver.read_cb=[](lv_indev_drv_t*,lv_indev_data_t*data){data->point=pointerPoint;data->state=pointerDown?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;};
    pointer=lv_indev_drv_register(&inputDriver);
    root = lv_scr_act();
    lv_obj_remove_style_all(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    Ui::setRoot(root);
    Ui::setActionHandler([](lv_event_t *e) {
        if (uintptr_t(lv_event_get_user_data(e)) == Ui::Back) {
            if (!GiftUI::back()) {
                app = App::None;
                page = 6;
                renderPending = true;
            }
        }
    });
    assert(GiftData::begin());
    HostEpoch = DeskLogic::beijingEpoch(2026, 11, 1, 23, 0);
}

void radioCatalogTests(){
    // Exercise the real catalog mutations and shared production selection /
    // favorite reconciliation through the actual GiftUI router and page.
    auto mutate=[](JsonDocument &request){
        request["revision"]=RadioCatalog::revision();String error;unsigned status=0;
        bool ok=RadioCatalog::mutate(request.as<JsonVariantConst>(),error,&status);
        if(!ok)std::cerr<<"Catalog error: "<<error<<"\n";
        assert(ok&&status==200);RadioPlayer::catalogChanged();
        if(app==App::Radio){RadioPage::refresh(true);if(renderPending)rebuild();}
    };
    auto remove=[&](const String &id){JsonDocument request;request["action"]="remove";request["id"]=id;mutate(request);};
    auto upsert=[&](const RadioPlayer::Station &entry){
        JsonDocument request;request["action"]="upsert";auto item=request["station"].to<JsonObject>();
        item["id"]=entry.id;item["name"]=entry.name;item["tag"]=entry.tag;item["url"]=entry.url;mutate(request);
    };
    assert(RadioCatalog::count()==6);RadioPlayer::play(2);RadioPlayer::toggleFavorite();
    String currentId=RadioPlayer::station(2).id;
    remove(RadioPlayer::station(0).id);
    assert(RadioPlayer::state().playing&&RadioPlayer::state().station==1&&RadioPlayer::isFavorite(1));
    assert(RadioPlayer::station(1).id==currentId);
    show(App::Radio);pressTap("选台");rebuild();assert(label("经典947"));
    auto edited=RadioPlayer::station(1);edited.name="经典音乐";edited.tag="音乐广播";upsert(edited);
    assert(label("经典音乐")&&!label("经典947")&&RadioPlayer::state().playing);
    assert(GiftUI::back());rebuild();assert(label("经典音乐"));
    edited.url="https://radio.example/new-live";upsert(edited);
    assert(RadioPlayer::state().playing&&RadioPlayer::station(1).id==currentId&&RadioPlayer::isFavorite(1));
    remove(currentId);assert(!RadioPlayer::state().playing&&RadioPlayer::state().station==0);
    assert(!RadioPlayer::isFavorite(0));
    while(RadioCatalog::count())remove(RadioCatalog::get(0).id);
    assert(RadioPlayer::state().station==-1&&!RadioPlayer::state().playing);
    assert(label("连接手机，添加一个电台"));
    pressTap("选台");rebuild();assert(label("还没有电台"));
    assert(GiftUI::back());rebuild();
    RadioPlayer::toggle();RadioPlayer::next();RadioPlayer::toggleFavorite();
    assert(!RadioPlayer::state().playing&&RadioPlayer::state().station==-1);
    upsert({"","手机添加","音乐","https://radio.example/new"});
    assert(RadioPlayer::state().station==0&&label("手机添加")&&!RadioPlayer::state().playing);
    RadioPlayer::toggle();assert(RadioPlayer::state().playing);RadioPlayer::stop();
    std::ofstream report(out+"/radio-catalog-integration.json");
    report<<"{\"passed\":true,\"real_catalog\":true,\"shared_production_reconciliation\":true,\"production_lvgl_page\":true,\"audio_backend\":\"host state only\",\"device_opened\":false}";
    std::cout<<"PASS radio catalog integration: stable selection/favorites, same-count live edits, URL edits, delete-current stop, delete-all empty view, add-after-empty\n";
}

int main(int argc, char **argv) {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
#ifdef _WIN32
    _set_error_mode(1);  // Assertions must go to stderr, never a modal dialog.
#endif
    if (argc < 2) return 2;
    out = argv[1];
    std::filesystem::create_directories(out);
    std::filesystem::create_directories("build/music-host/store");
    // Radio changes below are fixtures, separate from all other host data.
    LittleFS.directory="build/music-host/store";
    for(const char *path:{"/radio.json","/radio.bak","/radio.tmp"})LittleFS.remove(path);
    init();
    MusicPlayer::begin();
    uiTests();
    pointerTests();
    // Exercise the new app through the production launcher and GiftUI router,
    // including the third application sheet and return to that same sheet.
    GiftUI::setAppsPage(2);show(App::None,6);
    assert(label("在线 FM")&&!label("倒数日"));
    pressTap("在线 FM");rebuild();assert(app==App::Radio&&label("北京音乐广播"));
    save("radio-launcher-integration",false);
    pressTap("选台");rebuild();assert(label("全部电台")&&label("我的收藏"));
    assert(GiftUI::back());rebuild();assert(label("在线 FM"));
    tapSymbol(LV_SYMBOL_PLAY);assert(RadioPlayer::state().playing);
    assert(GiftUI::back());rebuild();assert(app==App::None&&GiftUI::appsPageIndex()==2&&label("在线 FM"));
    assert(RadioPlayer::state().playing);RadioPlayer::stop();
    std::cout<<"PASS radio integration: launcher third sheet, page routing, selection back and background playback state\n";
    radioCatalogTests();
    std::cout << "PASS: " << frames << " screenshots from firmware widgets, no serial port opened or firmware flashed\n";
    return 0;
}
