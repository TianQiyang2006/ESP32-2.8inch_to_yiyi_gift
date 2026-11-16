#include "timer_page.h"
#include "timer_service.h"
#include "focus_service.h"
#include "../app_state.h"
#include "../audio_board.h"
#include "../display.h"
#include "../gift_assets/gift_fonts.h"
#include "../saved_design/saved_resources.h"
#include "../ui_widgets.h"
#include <algorithm>
#include <string.h>

namespace TimerPage {
namespace {
using TimerEngine::Stage;
using Action = TimerService::Action;
enum class View { Home, Duration, Sounds, Name, Delete, Reset };
enum Command { Back = 1, Start, Pause, Reset, ConfirmReset, Settings, SaveSettings,
               SetDuration, OpenPreset, SavePreset, DeletePreset, ConfirmDelete, Dismiss, Again,
               Tone = 30, Preview = 40, Pick = 50, QuickName = 70 };
constexpr uint32_t Bg = 0xF7F7EE, Ink = 0x213F32, Muted = 0x51685A;
constexpr uint32_t Light = 0xE7EEDD, Green = 0x315C43, White = 0xFFFFFF;
constexpr uint32_t Faint = 0xA1B09B;
const char *toneNames[] = {"经典滴滴", "温柔钟声", "渐快提醒"};
const char *quickNames[] = {"学习", "运动", "提醒"};
lv_obj_t *pageRoot = nullptr, *overlay = nullptr, *digits = nullptr, *bar = nullptr;
lv_obj_t *wheels[3]{}, *nameField = nullptr, *volumeLabel = nullptr, *startButton = nullptr;
lv_obj_t *wheelDigits[3][3]{};
View view = View::Home, nameReturnView = View::Duration;
TimerEngine::Settings draft;
TimerEngine::Settings returnDraft;
int editing = -1;
uint32_t drawnRevision = 0, refreshAt = 0;
bool redraw = false, holding = false, longPressConsumed = false;
String lastTime;

String two(unsigned n) { return n < 10 ? "0" + String(n) : String(n); }
String duration(uint32_t seconds) { return two(seconds / 3600) + ":" + two(seconds / 60 % 60) + ":" + two(seconds % 60); }
void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, uint32_t color = White, int radius = 8) {
    auto *o = lv_obj_create(p); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0); lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
lv_obj_t *text(lv_obj_t *p, const String &s, int x, int y, int w, const lv_font_t *font = saved_gift_font_bold_14_0, uint32_t color = Ink, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    auto *o = lv_label_create(p); lv_obj_remove_style_all(o); lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, font->line_height + 2); lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0); lv_obj_set_style_text_align(o, align, 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP); lv_label_set_text(o, s.c_str());
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE); return o;
}
lv_obj_t *button(lv_obj_t *p, const String &s, int x, int y, int w, int h, int command,
                 bool primary = false, const lv_font_t *font = saved_gift_font_bold_14_0) {
    auto *o = box(p, x, y, w, h, primary ? Green : Light, 8);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(o, event, LV_EVENT_CLICKED, (void *)(intptr_t)command);
    Ui::attachPressFeedback(o);
    if (s.length()) text(o, s, 3, (h - font->line_height) / 2, w - 6, font, primary ? White : Ink, LV_TEXT_ALIGN_CENTER);
    return o;
}
void header(const char *title, bool home = false, const char *right = nullptr, int rightCommand = 0) {
    auto *back = button(pageRoot, "", 7, 3, 34, 34, Back);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0); lv_obj_set_ext_click_area(back, 3);
    static const lv_point_t chevron[] = {{7, 0}, {0, 7}, {7, 14}};
    auto *line = lv_line_create(back); lv_line_set_points(line, chevron, 3); lv_obj_set_pos(line, 12, 10);
    lv_obj_set_style_line_color(line, lv_color_hex(Ink), 0); lv_obj_set_style_line_width(line, 2, 0);
    text(pageRoot, title, 43, 9, 180, &fd_sans_bold_16);
    if (rightCommand) button(pageRoot, right ? right : "", 236, 2, 72, 36, rightCommand);
    else if (home) button(pageRoot, "提示音", 236, 2, 72, 36, Settings);
    else if (right) text(pageRoot, right, 230, 11, 78, saved_gift_font_bold_14_0, Muted, LV_TEXT_ALIGN_RIGHT);
}
void footer(const char *left, int a, const char *right, int b, int leftWidth = 128) {
    button(pageRoot, left, 12, 198, leftWidth, 40, a, false, &fd_sans_bold_16);
    button(pageRoot, right, 20 + leftWidth, 198, 288 - leftWidth, 40, b, true, &fd_sans_bold_16);
}
void go(View next) { AudioBoard::stopRingtone(1, AudioBoard::Reminder::Timer); view = next; redraw = true; holding = false; }
bool submit(Action action, const TimerEngine::Settings *s = nullptr, const char *name = nullptr) {
    if (TimerService::act(action, s, name)) { redraw = true; return true; }
    notifyUser("操作未保存，请检查存储", true); return false;
}
void enterDuration() { returnDraft = draft; go(View::Duration); }
void wheelEvent(lv_event_t *e) {
    auto code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) holding = true;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) holding = false;
    if (code == LV_EVENT_VALUE_CHANGED) {
        draft.seconds = lv_roller_get_selected(wheels[0]) * 3600 + lv_roller_get_selected(wheels[1]) * 60 + lv_roller_get_selected(wheels[2]);
        for (unsigned col = 0; col < 3; ++col) {
            unsigned selected = lv_roller_get_selected(wheels[col]), count = col ? 60 : 24;
            for (int row = 0; row < 3; ++row)
                if (wheelDigits[col][row]) lv_label_set_text(wheelDigits[col][row], two((selected + count + row - 1) % count).c_str());
        }
        if (startButton) { if (draft.seconds) lv_obj_clear_state(startButton, LV_STATE_DISABLED); else lv_obj_add_state(startButton, LV_STATE_DISABLED); }
    }
}
void home() {
    header("倒计时", true);
    auto *entry = button(pageRoot, "", 12, 44, 296, 52, SetDuration);
    lv_obj_set_style_bg_color(entry, lv_color_hex(Light), 0);
    static const lv_point_t hourglass[] = {{0, 0}, {18, 0}, {18, 5}, {0, 22}, {0, 27}, {18, 27}, {18, 22}, {0, 5}, {0, 0}};
    auto *icon = lv_line_create(entry); lv_line_set_points(icon, hourglass, 9); lv_obj_set_pos(icon, 16, 12);
    lv_obj_set_style_line_color(icon, lv_color_hex(Green), 0); lv_obj_set_style_line_width(icon, 2, 0);
    text(entry, "设置倒计时", 52, 5, 194, &fd_sans_bold_16);
    text(entry, "选择时、分、秒", 52, 29, 184, &fd_sans_bold_12, Muted);
    static const lv_point_t arrow[] = {{0, 0}, {6, 6}, {0, 12}};
    auto *chevron = lv_line_create(entry); lv_line_set_points(chevron, arrow, 3); lv_obj_set_pos(chevron, 263, 19);
    lv_obj_set_style_line_color(chevron, lv_color_hex(Green), 0); lv_obj_set_style_line_width(chevron, 2, 0);
    text(pageRoot, "常用计时器", 14, 109, 140);
    button(pageRoot, "+ 添加", 236, 100, 72, 30, SetDuration);
    auto *grid = box(pageRoot, 0, 134, 320, 104, Bg, 0);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    unsigned count = 0;
    for (unsigned i = 0; i < TimerEngine::PresetCount; ++i) {
        auto &p = draft.presets[i]; if (!p.seconds) continue;
        unsigned col = count % 2, row = count / 2;
        auto *card = button(grid, "", 12 + int(col) * 152, int(row) * 54, 144, 48, Pick + i);
        lv_obj_set_style_bg_color(card, lv_color_hex(White), 0);
        lv_obj_add_event_cb(card, event, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)(Pick + i));
        lv_obj_add_event_cb(card, event, LV_EVENT_PRESSED, (void *)(intptr_t)(Pick + i));
        text(card, p.name, 12, 3, 120, saved_gift_font_bold_14_0, Muted);
        text(card, duration(p.seconds), 12, 22, 120, &fd_mont_sb_21, Ink);
        ++count;
    }
}
void durationPicker() {
    header("设置倒计时");
    const char *units[] = {"小时", "分钟", "秒钟"};
    unsigned selected[] = {draft.seconds / 3600, draft.seconds / 60 % 60, draft.seconds % 60};
    box(pageRoot, 12, 98, 296, 54, Light, 10);
    for (unsigned i = 0; i < 3; ++i) {
        int x = 16 + int(i) * 100;
        text(pageRoot, units[i], x, 42, 88, saved_gift_font_bold_14_0, Muted, LV_TEXT_ALIGN_CENTER);
        auto *r = lv_roller_create(pageRoot); wheels[i] = r; lv_obj_remove_style_all(r);
        lv_obj_set_pos(r, x, 58);
        lv_obj_set_style_text_font(r, &fd_mont_sb_21, 0); lv_obj_set_style_text_color(r, lv_color_hex(Faint), 0);
        lv_obj_set_style_text_line_space(r, 20, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0); lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_SELECTED);
        lv_obj_set_style_text_font(r, &lv_font_montserrat_48, LV_PART_SELECTED);
        lv_obj_set_style_text_color(r, lv_color_hex(Ink), LV_PART_SELECTED);
        lv_obj_set_style_bg_color(r, lv_color_hex(Light), LV_PART_SELECTED);
        String options; for (unsigned n = 0; n < (i ? 60u : 24u); ++n) { if (n) options += '\n'; options += two(n); }
        lv_roller_set_options(r, options.c_str(), LV_ROLLER_MODE_INFINITE); lv_roller_set_visible_row_count(r, 3);
        lv_obj_set_size(r, 88, 132);
        lv_roller_set_selected(r, selected[i], LV_ANIM_OFF); lv_obj_add_event_cb(r, wheelEvent, LV_EVENT_ALL, nullptr);
        lv_obj_set_style_opa(r, LV_OPA_TRANSP, 0);
        wheelDigits[i][0] = text(pageRoot, two((selected[i] + (i ? 60 : 24) - 1) % (i ? 60 : 24)), x, 68, 88, &fd_mont_sb_21, Faint, LV_TEXT_ALIGN_CENTER);
        wheelDigits[i][1] = text(pageRoot, two(selected[i]), x, 99, 88, &lv_font_montserrat_48, Ink, LV_TEXT_ALIGN_CENTER);
        wheelDigits[i][2] = text(pageRoot, two((selected[i] + 1) % (i ? 60 : 24)), x, 158, 88, &fd_mont_sb_21, Faint, LV_TEXT_ALIGN_CENTER);
        if (i < 2) text(pageRoot, ":", x + 82, 98, 24, &lv_font_montserrat_48, Ink, LV_TEXT_ALIGN_CENTER);
    }
    button(pageRoot, "存为常用", 12, 198, 128, 40, OpenPreset, false, &fd_sans_bold_16);
    startButton = button(pageRoot, "开始倒计时", 148, 198, 160, 40, Start, true, &fd_sans_bold_16);
    lv_obj_set_style_opa(startButton, LV_OPA_50, LV_STATE_DISABLED);
    if (!draft.seconds) lv_obj_add_state(startButton, LV_STATE_DISABLED);
}
void active(const TimerService::Snapshot &s) {
    bool paused = s.engine.stage == Stage::Paused;
    header("倒计时", false, paused ? "已暂停" : "计时中");
    text(pageRoot, s.engine.sessionName, 12, 48, 296, &fd_sans_bold_16, Green, LV_TEXT_ALIGN_CENTER);
    digits = text(pageRoot, "", 12, 79, 296, &lv_font_montserrat_48, Ink, LV_TEXT_ALIGN_CENTER); lastTime = "";
    text(pageRoot, "总时长 " + duration(unsigned(s.engine.total / 1000)), 12, 147, 296, saved_gift_font_bold_14_0, Muted, LV_TEXT_ALIGN_CENTER);
    box(pageRoot, 28, 175, 264, 6, Light, 3); bar = box(pageRoot, 28, 175, 264, 6, Green, 3);
    footer("重置", Reset, paused ? "继续倒计时" : "暂停", paused ? Start : Pause, 96);
}
void finished(const TimerService::Snapshot &s) {
    header("倒计时", false, "已完成");
    text(pageRoot, "倒计时结束", 12, 33, 296, &fd_sans_bold_16, Green, LV_TEXT_ALIGN_CENTER);
    text(pageRoot, "00:00:00", 12, 72, 296, &lv_font_montserrat_48, Ink, LV_TEXT_ALIGN_CENTER);
    text(pageRoot, String(s.engine.sessionName) + " · " + duration(unsigned(s.engine.total / 1000)), 12, 137, 296, saved_gift_font_bold_14_0, Muted, LV_TEXT_ALIGN_CENTER);
    footer("停止提醒", Dismiss, "再来一次", Again, 128);
}
void volumeEvent(lv_event_t *e) {
    draft.volume = uint8_t((lv_slider_get_value(lv_event_get_target(e)) + 2) / 5 * 5);
    lv_slider_set_value(lv_event_get_target(e), draft.volume, LV_ANIM_OFF);
    lv_label_set_text(volumeLabel, (String(draft.volume) + "%").c_str());
}
void sounds() {
    header("提示音设置");
    for (unsigned i = 0; i < 3; ++i) {
        int y = 42 + int(i) * 41; bool chosen = draft.tone == i;
        auto *row = box(pageRoot, 12, y, 296, 36, chosen ? Light : White, 8);
        auto *radio = box(row, 12, 10, 16, 16, chosen ? Green : 0xCDD6C7, 8);
        if (chosen) box(radio, 5, 5, 6, 6, White, 3);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(row, event, LV_EVENT_CLICKED, (void *)(intptr_t)(Tone + i));
        text(row, toneNames[i], 40, 8, 174, saved_gift_font_bold_14_0);
        button(row, "试听", 234, 1, 58, 34, Preview + i);
    }
    text(pageRoot, "音量", 14, 170, 44, saved_gift_font_bold_14_0, Muted);
    volumeLabel = text(pageRoot, String(draft.volume) + "%", 252, 170, 56, saved_gift_font_bold_14_0, Green, LV_TEXT_ALIGN_RIGHT);
    auto *slider = lv_slider_create(pageRoot); lv_obj_set_pos(slider, 72, 177); lv_obj_set_size(slider, 160, 8);
    lv_slider_set_range(slider, 0, 100); lv_slider_set_value(slider, draft.volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(Light), LV_PART_MAIN); lv_obj_set_style_bg_color(slider, lv_color_hex(Green), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(Green), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, volumeEvent, LV_EVENT_VALUE_CHANGED, nullptr);
    footer("取消", Back, "保存", SaveSettings, 96);
}
void nameView() {
    bool existing = editing >= 0 && draft.presets[editing].seconds != 0;
    header(existing ? "编辑常用计时器" : "添加常用计时器");
    nameField = lv_textarea_create(pageRoot); lv_obj_set_pos(nameField, 12, 40); lv_obj_set_size(nameField, 296, 38);
    lv_obj_set_style_text_font(nameField, saved_gift_font_bold_14_0, 0); lv_textarea_set_one_line(nameField, true);
    lv_textarea_set_max_length(nameField, 8);
    lv_textarea_set_text(nameField, existing ? draft.presets[editing].name : "自定义");
    for (int i = 0; i < 3; ++i) button(pageRoot, quickNames[i], 12 + i * 62, 82, 56, 28, QuickName + i);
    if (existing) button(pageRoot, "删除", 238, 82, 70, 28, DeletePreset);
    auto *keyboard = lv_keyboard_create(pageRoot); lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT); lv_obj_set_pos(keyboard, 0, 114); lv_obj_set_size(keyboard, 320, 78);
    lv_obj_set_style_pad_all(keyboard, 2, 0); lv_obj_set_style_pad_row(keyboard, 2, 0); lv_obj_set_style_pad_column(keyboard, 2, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(Light), LV_PART_ITEMS); lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(Ink), LV_PART_ITEMS); lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_keyboard_set_textarea(keyboard, nameField); lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    footer("取消", Back, "保存常用", SavePreset, 96);
}
void draw() {
    if (!pageRoot || !lv_obj_is_valid(pageRoot)) return;
    Ui::cancelAnimations(); lv_obj_clean(pageRoot); digits = bar = nameField = volumeLabel = startButton = nullptr;
    for (auto &w : wheels) w = nullptr;
    memset(wheelDigits, 0, sizeof(wheelDigits));
    lv_obj_set_style_bg_color(pageRoot, lv_color_hex(Bg), 0); lv_obj_set_style_bg_opa(pageRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(pageRoot, LV_OBJ_FLAG_SCROLLABLE);
    auto s = TimerService::snapshot(); drawnRevision = s.engine.revision;
    if (view == View::Home) {
        if (s.engine.stage == Stage::Ready) { draft = s.engine.settings; home(); }
        else if (s.engine.stage == Stage::Finished) finished(s);
        else active(s);
    }
    else if (view == View::Duration) durationPicker();
    else if (view == View::Sounds) sounds();
    else if (view == View::Name) nameView();
    else {
        header(view == View::Delete ? "删除常用计时器" : "重置倒计时");
        text(pageRoot, view == View::Delete ? "确定删除这项常用计时器？" : "确定重置本次倒计时？", 12, 83, 296, &fd_sans_bold_16, Ink, LV_TEXT_ALIGN_CENTER);
        text(pageRoot, view == View::Delete ? String(draft.presets[editing].name) : "当前剩余时间将清除", 12, 127, 296, saved_gift_font_bold_14_0, Muted, LV_TEXT_ALIGN_CENTER);
        footer("取消", Back, "确认", view == View::Delete ? ConfirmDelete : ConfirmReset, 96);
    }
    redraw = false;
}
void beginPresetEdit(int i) {
    returnDraft = draft; editing = i; draft.seconds = draft.presets[i].seconds;
    nameReturnView = View::Home; longPressConsumed = true; go(View::Name);
}
void event(lv_event_t *e) {
    int command = int(intptr_t(lv_event_get_user_data(e)));
    lv_event_code_t code = lv_event_get_code(e);
    if (command >= Pick && command < Pick + int(TimerEngine::PresetCount)) {
        int i = command - Pick;
        if (code == LV_EVENT_PRESSED) { longPressConsumed = false; return; }
        if (code == LV_EVENT_LONG_PRESSED) { beginPresetEdit(i); return; }
        if (code == LV_EVENT_CLICKED) {
            if (longPressConsumed) { longPressConsumed = false; return; }
            if (view != View::Home || !draft.presets[i].seconds) return;
            TimerEngine::Settings proposed = draft; proposed.seconds = proposed.presets[i].seconds;
            if (submit(Action::Start, &proposed, proposed.presets[i].name)) go(View::Home);
        }
        return;
    }
    if (command >= Tone && command < Tone + 3) { draft.tone = command - Tone; redraw = true; AudioBoard::stopRingtone(1, AudioBoard::Reminder::Timer); return; }
    if (command >= Preview && command < Preview + 3) {
        if (!draft.volume) notifyUser("当前已静音", false);
        else if (!AudioBoard::ringtone(command - Preview, draft.volume, 3000, 1, 0, AudioBoard::Reminder::Timer)) notifyUser("暂时无法试听", true);
        return;
    }
    if (command >= QuickName && command < QuickName + 3) { lv_textarea_set_text(nameField, quickNames[command - QuickName]); return; }
    switch (command) {
        case Back:
            if (!back()) { lv_event_t forwarded = *e; forwarded.user_data = (void *)(uintptr_t)Ui::Back; Ui::dispatchAction(&forwarded); }
            break;
        case SetDuration:
            editing = -1; enterDuration(); break;
        case Start: {
            if (!draft.seconds) { notifyUser("请设置大于0秒的时长", false); break; }
            if (submit(Action::Start, &draft, "自定义")) go(View::Home);
            break;
        }
        case Pause: submit(Action::Pause); break;
        case Reset: if (TimerService::snapshot().engine.stage == Stage::Finished) { submit(Action::Reset); go(View::Home); } else go(View::Reset); break;
        case ConfirmReset: if (submit(Action::Reset)) go(View::Home); break;
        case Settings: returnDraft = draft; go(View::Sounds); break;
        case SaveSettings: if (submit(Action::Save, &draft)) go(View::Home); break;
        case OpenPreset: {
            if (!draft.seconds) { notifyUser("请设置大于0秒的时长", false); break; }
            editing = -1;
            for (unsigned i = 0; i < TimerEngine::PresetCount; ++i) if (!draft.presets[i].seconds) { editing = int(i); break; }
            if (editing < 0) { notifyUser("最多8项，长按常用项可编辑或删除", false); break; }
            returnDraft = draft; draft.presets[editing] = TimerEngine::Preset{}; nameReturnView = View::Duration;
            go(View::Name); break;
        }
        case SavePreset: {
            if (editing < 0 || !nameField) break;
            String name = lv_textarea_get_text(nameField); name.trim();
            if (!name.length() || name.length() >= sizeof(draft.presets[0].name)) { notifyUser("名称需为1至8个字", false); break; }
            auto proposed = draft; auto &p = proposed.presets[editing];
            memset(p.name, 0, sizeof(p.name)); memcpy(p.name, name.c_str(), name.length()); p.seconds = draft.seconds;
            if (nameReturnView == View::Home) proposed.seconds = returnDraft.seconds;
            if (submit(Action::Save, &proposed)) { draft = proposed; editing = -1; go(View::Home); }
            break;
        }
        case DeletePreset: go(View::Delete); break;
        case ConfirmDelete: { auto proposed = draft; proposed.seconds = returnDraft.seconds; proposed.presets[editing] = TimerEngine::Preset{}; if (submit(Action::Save, &proposed)) { draft = proposed; editing = -1; go(View::Home); } break; }
        case Dismiss: if (submit(Action::Reset)) go(View::Home); break;
        case Again: if (submit(Action::Replay)) go(View::Home); break;
    }
}
} // namespace
void build(lv_obj_t *parent) {
    TimerService::begin(); pageRoot = parent; draft = TimerService::snapshot().engine.settings;
    view = View::Home; editing = -1; draw(); refresh(true);
}
void refresh(bool force) {
    if (!pageRoot || !lv_obj_is_valid(pageRoot)) return;
    uint32_t now = millis(); if (!force && !redraw && now - refreshAt < 100) return; refreshAt = now;
    auto s = TimerService::snapshot();
    if (s.engine.revision != drawnRevision) redraw = true;
    if (redraw) draw();
    if (digits) {
        auto left = s.engine.remaining(FocusService::monotonic()); String value = duration(unsigned((left + 999) / 1000));
        if (value != lastTime) { lv_label_set_text(digits, value.c_str()); lastTime = value; }
        if (bar) lv_obj_set_width(bar, s.engine.total ? std::max(1, int(264 * left / s.engine.total)) : 1);
    }
}
void poll() {
    auto s = TimerService::snapshot(); bool alarm = FocusService::snapshot().engine.runtime.ringing != 0;
    if (!s.ringing || alarm) {
        if (overlay) { lv_obj_del(overlay); overlay = nullptr; if (!alarm) Display::setBrightness(settings.brightness); }
    } else if (!overlay) {
        overlay = box(lv_layer_top(), 0, 0, 320, 240, Bg, 0); lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        Display::setBrightness(uint8_t(std::max(80, int(settings.brightness))));
        text(overlay, "倒计时结束", 12, 25, 296, &fd_sans_bold_16, Green, LV_TEXT_ALIGN_CENTER);
        text(overlay, "00:00:00", 12, 77, 296, &lv_font_montserrat_48, Ink, LV_TEXT_ALIGN_CENTER);
        text(overlay, String(s.engine.sessionName) + " · " + duration(unsigned(s.engine.total / 1000)), 12, 142, 296, saved_gift_font_bold_14_0, Muted, LV_TEXT_ALIGN_CENTER);
        text(overlay, !s.engine.volume ? "已静音" : !AudioBoard::ready() ? "音频不可用，请检查声音" : String(toneNames[s.engine.tone]), 12, 168, 296, &fd_sans_bold_12, Muted, LV_TEXT_ALIGN_CENTER);
        button(overlay, "停止提醒", 12, 198, 128, 40, Dismiss, false, &fd_sans_bold_16);
        button(overlay, "再来一次", 148, 198, 160, 40, Again, true, &fd_sans_bold_16);
    }
    refresh();
}
void cancel() { AudioBoard::stopRingtone(1, AudioBoard::Reminder::Timer); pageRoot = digits = bar = nameField = volumeLabel = startButton = nullptr; for (auto &w : wheels) w = nullptr; memset(wheelDigits, 0, sizeof(wheelDigits)); redraw = holding = longPressConsumed = false; }
bool canBack() { return pageRoot && view != View::Home; }
bool back() {
    if (!canBack()) return false;
    if (view == View::Delete) go(View::Name);
    else if (view == View::Duration) { draft = TimerService::snapshot().engine.settings; go(View::Home); }
    else if (view == View::Name) { draft = returnDraft; editing = -1; go(nameReturnView); }
    else if (view == View::Sounds) { draft = returnDraft; go(View::Home); }
    else go(View::Home);
    return true;
}
bool inputActive() { return holding || (pageRoot && view == View::Name); }
} // namespace TimerPage
