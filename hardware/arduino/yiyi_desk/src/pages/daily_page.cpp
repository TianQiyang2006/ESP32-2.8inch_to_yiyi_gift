// Page 4 - 每日页（心情 / 木鱼 / 喝水）+ 心情编辑页 + 喝水详情页 + 木鱼详情页.
//
// Hand-written LVGL. Visual geometry/colours are taken from the locked design
// daily-v1 (`build/design/daily/make_daily_redesign.py`, the generator of the
// locked PNGs); interaction rules follow UI实现规格.md §5 (7 days clickable,
// future disabled, tap a mood = save + return, back = discard, card +1 with
// sound, undo only on the detail pages).  All text uses the bold designer fonts.
//
// Views are module-internal state; `debugVariant` pins one for the host renderer.
#include "page_api.h"
#include "../ui_widgets.h"
#include "../ui_styles.h"
#include "../saved_design/saved_resources.h"
#include "../gift_assets/gift_fonts.h"
#include "../gift_data.h"
#include "../audio_board.h"
#include "../clock_source.h"
#include "../app_state.h"
#include "../touch.h"
#include "../text_util.h"
#include "../../desk_logic.h"
#include "assets/daily_assets.h"
#include <Arduino.h>
#include <stdio.h>

namespace DailyPage {
namespace {

// ---- palette (locked design) ---------------------------------------------
constexpr uint32_t Cream = 0xE7EFD7;      // page background
constexpr uint32_t Panel = 0xFBF6E0;      // mood / editor card
constexpr uint32_t EditorCard = 0xFDF9EB; // mood editor cards
constexpr uint32_t Ink = 0x775845;        // brown text
constexpr uint32_t Accent = 0xFFDF6C;     // mood ring
constexpr uint32_t Gold = 0xD5AD59;       // editor card / selection border
constexpr uint32_t Underline = 0xD6A44C;  // today underline
constexpr uint32_t PlusInk = 0xA0826A;    // empty-mood "+"
constexpr uint32_t WoodTop = 0xF3743A;    // wood card gradient top
constexpr uint32_t WoodBottom = 0xCE4023; // wood card gradient bottom
constexpr uint32_t WoodText = 0xFFF1C7;   // wood card cream text
constexpr uint32_t WoodShadow = 0xA8401F; // wood card text drop shadow
constexpr uint32_t WaterBg = 0xACD5FC;    // water card background
constexpr uint32_t WaterDeep = 0x2B8FCB;  // progress fill / buttons label
constexpr uint32_t DarkBlue = 0x1D6FA5;   // water numbers / title
constexpr uint32_t GoalInk = 0x3C78A0;    // 目标 1000 ml
constexpr uint32_t NavGreen = 0xA4CCAC;   // editor / detail nav bar
constexpr uint32_t NavInk = 0x344F1D;     // nav bar text / chevron
constexpr uint32_t HintInk = 0x8C6E5A;    // editor bottom hint
constexpr uint32_t ErrorInk = 0xB14C34;

// ---- bold fonts (all from the designer font family; every glyph used here is
// covered, see the note in the completion report) --------------------------
const lv_font_t *const F7 = saved_gift_font_bold_7_0;
const lv_font_t *const F8 = saved_gift_font_bold_8_0;
const lv_font_t *const F9 = saved_gift_font_bold_9_0;
const lv_font_t *const F10 = saved_gift_font_bold_10_0;
const lv_font_t *const F11 = saved_gift_font_bold_11_0;
const lv_font_t *const F13 = saved_gift_font_bold_13_0;
const lv_font_t *const F14 = saved_gift_font_bold_14_0;
const lv_font_t *const F20 = saved_gift_font_bold_20_0;
const lv_font_t *const F28 = saved_gift_font_bold_28_0;
const lv_font_t *const F32 = saved_gift_font_bold_32_0;
const lv_font_t *const F12 = &fd_sans_bold_12;  // "次"
const lv_font_t *const F22 = &fd_sans_bold_22;  // 3-digit knock count
const lv_font_t *const F26 = &fd_sans_bold_26;  // water millilitres

const char *const WeekNames[7] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
const char *const MoodIds[6] = {"happy", "calm", "neutral", "tired", "anxious", "sad"};
const char *const MoodNames[6] = {"开心", "平静", "一般", "疲惫", "焦虑", "难过"};

enum class View { Main, MoodEditor, WaterDetail, WoodDetail };

// ---- state -----------------------------------------------------------------
View view = View::Main;
bool internalNav = false;   // the next build is a local click, keep the view
int forcedView = 0;         // debugVariant target
bool forcedPending = false;
bool built = false;
String builtToday;
String editorDate;

// main page
lv_obj_t *todayFace = nullptr;
lv_obj_t *todayPlusH = nullptr, *todayPlusV = nullptr;
lv_obj_t *weekFaces[7] = {}, *weekPluses[7] = {}, *weekSlots[7] = {}, *weekUnderline = nullptr;
lv_obj_t *knockValue = nullptr, *knockShadow = nullptr, *knockUnit = nullptr;
lv_obj_t *waterValue = nullptr, *waterUnit = nullptr, *waterFill = nullptr, *waterWave = nullptr;
lv_obj_t *waterGoal = nullptr;
constexpr int knockBaseline = 56;  // card-relative text baseline shared by number + 次
int waterNumberY = 19;             // design: 26px mock is ~28px tall on the locked PNG

// editor
lv_obj_t *moodError = nullptr;

// animations
lv_obj_t *animFish = nullptr, *animWave = nullptr;
lv_coord_t fishBaseY = 38, waveFromY = 62, waveToY = 62;

// ---- small helpers ---------------------------------------------------------
lv_obj_t *decor(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius) {
    auto *o = Ui::panel(parent, x, y, w, h, bg, radius);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);  // panels are clickable by default
    return o;
}

lv_obj_t *hit(lv_obj_t *parent, int x, int y, int w, int h) {
    auto *o = Ui::panel(parent, x, y, w, h, 0, 0);
    lv_obj_add_style(o, &UiStyles::bgTransparent, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width,
                const lv_font_t *font, uint32_t ink) {
    return Ui::label(parent, text, x, y, width, font, ink);
}

lv_obj_t *centered(lv_obj_t *parent, const char *text, int x, int y, int width,
                   const lv_font_t *font, uint32_t ink) {
    auto *o = Ui::label(parent, text, x, y, width, font, ink);
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
    return o;
}

lv_obj_t *picture(lv_obj_t *parent, const lv_img_dsc_t *src, int x, int y) {
    auto *o = lv_img_create(parent);
    lv_img_set_src(o, src);
    lv_obj_set_pos(o, x, y);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

void onClick(lv_obj_t *o, lv_event_cb_t cb, void *user = nullptr) {
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user);
}

void setText(lv_obj_t *o, const String &value) {
    if (o && String(lv_label_get_text(o)) != value) lv_label_set_text(o, value.c_str());
}

void setFont(lv_obj_t *o, const lv_font_t *font) {
    if (o && lv_obj_get_style_text_font(o, 0) != font) lv_obj_set_style_text_font(o, font, 0);
}

void setSrc(lv_obj_t *o, const lv_img_dsc_t *src) {
    if (o && lv_img_get_src(o) != static_cast<const void *>(src)) lv_img_set_src(o, src);
}

void setVisible(lv_obj_t *o, bool visible) {
    if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != visible) return;
    if (visible) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void place(lv_obj_t *o, int x, int y) {
    if (o && (lv_obj_get_x(o) != x || lv_obj_get_y(o) != y)) lv_obj_set_pos(o, x, y);
}

int textWidth(const char *text, const lv_font_t *font) {
    return lv_txt_get_width(text, strlen(text), font, 0, LV_TEXT_FLAG_NONE);
}

const lv_font_t *knockFont(int knocks) {
    if (knocks >= 1000) return F20;
    if (knocks >= 100) return F22;
    return F32;
}

// Baseline of a label = y + line_height - base_line (see lv_draw_label.c).
int baselineFromTop(const lv_font_t *font) { return font->line_height - font->base_line; }

int dayOfWeek(DeskLogic::Civil d) {
    int64_t n = DeskLogic::civilDays(d.year, d.month, d.day);
    return static_cast<int>((n + 4) % 7 + 7) % 7;  // 0 = Sunday
}

String dateKey(DeskLogic::Civil d) {
    char value[11];
    snprintf(value, sizeof(value), "%04d-%02d-%02d", d.year, d.month, d.day);
    return value;
}

String editorDateLabel() {
    if (editorDate.length() >= 10) return editorDate.substring(5, 7) + "/" + editorDate.substring(8, 10);
    return "--/--";
}

const lv_img_dsc_t *bigMood(const String &id) {
    if (id == "happy") return saved_gift_mood_happy;
    if (id == "calm") return saved_gift_mood_calm;
    if (id == "neutral") return saved_gift_mood_neutral;
    if (id == "tired") return saved_gift_mood_tired;
    if (id == "anxious") return saved_gift_mood_anxious;
    if (id == "sad") return saved_gift_mood_sad;
    return nullptr;
}

const lv_img_dsc_t *weekMood(const String &id) {
    if (id == "happy") return &daily_mood_week_happy;
    if (id == "calm") return &daily_mood_week_calm;
    if (id == "neutral") return &daily_mood_week_neutral;
    if (id == "tired") return &daily_mood_week_tired;
    if (id == "anxious") return &daily_mood_week_anxious;
    if (id == "sad") return &daily_mood_week_sad;
    return nullptr;
}

int waterTotal(JsonVariantConst record) {
    int total = 0;
    for (JsonVariantConst item : record["water"].as<JsonArrayConst>()) total += item["amount"] | 0;
    return total;
}

void cancelAnim(lv_obj_t *&object) {
    if (object) lv_anim_del(object, nullptr);
    object = nullptr;
}

// ---- animations ------------------------------------------------------------
void fishExec(void *var, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t *>(var), fishBaseY - 4 + (4 * value) / 100);
}

// Wooden-fish reaction: the cut-out bounces up 4px and eases back.  Only the
// image is invalidated, and the animation is cancelled before the page dies.
void bounceFish() {
    if (!animFish) return;
    lv_anim_del(animFish, nullptr);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, animFish);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 180);
    lv_anim_set_path_cb(&a, Ui::easeInOut());
    lv_anim_set_exec_cb(&a, fishExec);
    lv_anim_start(&a);
}

void waveExec(void *var, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t *>(var),
                 waveFromY + static_cast<lv_coord_t>(((int32_t)(waveToY - waveFromY) * value) / 1000));
}

// Water level: the decorative ellipse slice rises a little as the goal is
// approached (the locked mock shows it at y=62 for 0 ml).
void raiseWater(lv_coord_t target, bool animate) {
    if (!animWave) return;
    if (!animate) {
        lv_anim_del(animWave, nullptr);
        lv_obj_set_y(animWave, target);
        return;
    }
    if (lv_obj_get_y(animWave) == target) return;
    lv_anim_del(animWave, nullptr);
    waveFromY = lv_obj_get_y(animWave);
    waveToY = target;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, animWave);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, Ui::easeInOut());
    lv_anim_set_exec_cb(&a, waveExec);
    lv_anim_start(&a);
}

// ---- forward declarations --------------------------------------------------
void buildMain(lv_obj_t *parent);
void buildMoodEditor(lv_obj_t *parent);
void buildWaterDetail(lv_obj_t *parent);
void buildWoodDetail(lv_obj_t *parent);
void updateMain();
void applyKnocks(int knocks);
void applyWater(int total, int goal, bool animate);
void goMain();
void requestRebuild();
void openEditor(const String &date);

// ---- callbacks -------------------------------------------------------------
void dayClick(lv_event_t *e) {
    if (Touch::swiped()) return;  // a page-swipe must never also open the editor
    const int index = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    int64_t now = ClockSource::now();
    auto today = DeskLogic::beijingDate(now);
    const int todayIndex = (dayOfWeek(today) + 6) % 7;
    if (index > todayIndex) return;  // future days never fire (no callback is attached)
    openEditor(dateKey(DeskLogic::beijingDate(now + static_cast<int64_t>(index - todayIndex) * 86400)));
}

void openToday(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also open the editor
    String today = GiftData::today();
    if (today.isEmpty()) {
        notifyUser("请先联网校时", true);
        return;
    }
    openEditor(today);
}

void moodClick(lv_event_t *e) {
    if (Touch::swiped()) return;  // a page-swipe must never also save a mood
    const int index = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (editorDate.isEmpty()) return;
    auto record = GiftData::record(editorDate);
    String error;
    if (GiftData::saveMoodAt(editorDate, MoodIds[index], record["note"] | "", error)) {
        goMain();
    } else if (moodError) {
        setText(moodError, error);
        setVisible(moodError, true);
    }
}

void strikeClick(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also strike
    if (GiftData::today().isEmpty()) {
        notifyUser("请先联网校时", true);
        return;
    }
    auto options = GiftData::get("daily");
    GiftData::strike();
    if (options["sound"] | true) AudioBoard::knock();
    bounceFish();
    applyKnocks(GiftData::record(GiftData::today())["knocks"] | 0);
    lastInputAt = millis();
}

void openWoodDetail(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also open the detail
    internalNav = true;
    view = View::WoodDetail;
    requestRebuild();
}

void openWaterDetail(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also open the detail
    internalNav = true;
    view = View::WaterDetail;
    requestRebuild();
}

void addWaterClick(lv_event_t *e) {
    if (Touch::swiped()) return;  // a page-swipe must never also add water
    if (GiftData::today().isEmpty()) {
        notifyUser("请先联网校时", true);
        return;
    }
    const int amount = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    GiftData::addWater(amount);
    lastInputAt = millis();
    if (view == View::WaterDetail) {
        requestRebuild();
        return;
    }
    const int goal = GiftData::get("daily")["waterGoal"] | 1000;
    applyWater(waterTotal(GiftData::record(GiftData::today())), goal, true);
}

void waterUndoClick(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also undo
    auto record = GiftData::record(GiftData::today());
    if (record["water"].as<JsonArrayConst>().size() == 0) {
        notifyUser("今天还没有饮水记录", true);
        return;
    }
    GiftData::undoWater();
    lastInputAt = millis();
    requestRebuild();
}

void strikeUndoClick(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also undo
    auto record = GiftData::record(GiftData::today());
    if ((record["knocks"] | 0) <= 0) {
        notifyUser("今天还没有敲击记录", true);
        return;
    }
    GiftData::undoStrike();
    lastInputAt = millis();
    requestRebuild();
}

void strikeResetClick(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also reset
    GiftData::resetKnocks();
    lastInputAt = millis();
    requestRebuild();
}

void soundToggleClick(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also toggle the sound
    JsonDocument patch;
    patch["daily"]["sound"] = !(GiftData::get("daily")["sound"] | true);
    String error;
    if (!GiftData::patch(patch.as<JsonVariantConst>(), error)) notifyUser(error, true);
    lastInputAt = millis();
    requestRebuild();
}

void backClick(lv_event_t *) {
    if (Touch::swiped()) return;  // a page-swipe must never also fire the back button
    goMain();
}

// ---- navigation / rebuild --------------------------------------------------
void requestRebuild() {
    renderPending = true;
    lastInputAt = millis();
}

void goMain() {
    internalNav = true;
    view = View::Main;
    editorDate = "";
    requestRebuild();
}

void openEditor(const String &date) {
    internalNav = true;
    view = View::MoodEditor;
    editorDate = date;
    requestRebuild();
}

void resetPointers() {
    todayFace = todayPlusH = todayPlusV = nullptr;
    for (int i = 0; i < 7; ++i) weekFaces[i] = weekPluses[i] = weekSlots[i] = nullptr;
    weekUnderline = knockValue = knockShadow = knockUnit = nullptr;
    waterValue = waterUnit = waterFill = waterWave = waterGoal = nullptr;
    moodError = nullptr;
}

void navBar(lv_obj_t *parent, const char *title) {
    decor(parent, 0, 0, 320, 30, NavGreen, 0);
    auto *chevron = lv_line_create(parent);
    static const lv_point_t points[] = {{6, 0}, {0, 4}, {6, 8}};
    lv_line_set_points(chevron, points, 3);
    lv_obj_set_pos(chevron, 15, 10);
    lv_obj_set_style_line_width(chevron, 2, 0);
    lv_obj_set_style_line_color(chevron, lv_color_hex(NavInk), 0);
    lv_obj_set_style_line_rounded(chevron, true, 0);
    auto *back = hit(parent, 6, 3, 36, 27);
    onClick(back, backClick);
    label(parent, title, 54, 8, 220, F13, NavInk);
}

// ---- main page -------------------------------------------------------------
void buildMain(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(Cream), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    // Mood card (8,5) 304x131 r16.
    auto *moodCard = decor(parent, 8, 5, 304, 131, Panel, 16);
    label(moodCard, "今天的心情如何呢？", 12, 9, 170, F10, Ink);

    // Big circle 67x67 at card (28,46): today's mood or "+".
    auto *todayRing = decor(moodCard, 28, 46, 67, 67, Panel, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(todayRing, 4, 0);
    lv_obj_set_style_border_color(todayRing, lv_color_hex(Accent), 0);
    lv_obj_add_flag(todayRing, LV_OBJ_FLAG_CLICKABLE);
    onClick(todayRing, openToday);
    // Child coordinates are relative to the ring's CONTENT area (the 4 px frame
    // is excluded), so the 59x59 face must sit at (0,0) to fill the inside
    // exactly; an offset here shifts the face over the frame and clips it.
    todayFace = picture(todayRing, saved_gift_mood_calm, 0, 0);  // 59x59 inside the ring
    // "+" arms 26x4 / 4x26, radius 2, #A0826A, centred on (33.5,33.5) in the ring.
    // Content-area origin is (4,4), hence the 26/4 px arm boxes are at (16,27)
    // and (27,16) to end up centred in the 67 px ring.
    todayPlusH = decor(todayRing, 16, 27, 26, 4, PlusInk, 2);
    todayPlusV = decor(todayRing, 27, 16, 4, 26, PlusInk, 2);

    // Seven day circles: Mon-Wed on the top row, Thu-Sun on the bottom row.
    // The whole ring + label slot is the click target.
    static const int slotX[7] = {160, 198, 236, 130, 168, 206, 244};
    static const int slotY[7] = {4, 4, 4, 60, 60, 60, 60};
    int64_t now = ClockSource::now();
    auto date = DeskLogic::beijingDate(now);
    const int todayIndex = (dayOfWeek(date) + 6) % 7;
    for (int i = 0; i < 7; ++i) {
        auto *slot = hit(moodCard, slotX[i], slotY[i], 27, 46);
        if (i <= todayIndex) onClick(slot, dayClick, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        else lv_obj_clear_flag(slot, LV_OBJ_FLAG_CLICKABLE);
        weekSlots[i] = slot;

        auto *ring = decor(slot, 0, 0, 27, 27, Panel, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(Accent), 0);
        // Ring border 2 px -> content area is 23x23, exactly the face size; the
        // 20x20 plus is centred at (1,1) in that content area.
        weekFaces[i] = picture(ring, &daily_mood_week_calm, 0, 0);  // 23x23 face
        weekPluses[i] = picture(ring, &daily_plus20, 1, 1);         // 20x20 "+"
        centered(slot, WeekNames[i], 0, 32, 27, F9, Ink);
    }
    // Today underline, 18x2, under the weekday label.  The top row's labels sit
    // at card-relative y 36 and the bottom row's 56 px lower (slotY 4/60), so the
    // underline follows the row of the current day (design y=55 absolute for the
    // top row).
    weekUnderline = decor(moodCard, slotX[todayIndex] + 4, slotY[todayIndex] + 46, 18, 2, Underline, 1);

    // Wooden-fish card (8,140) 100x91 r11, vertical gradient.
    auto *woodCard = Ui::panel(parent, 8, 140, 100, 91, WoodTop, 11);
    lv_obj_set_style_bg_grad_color(woodCard, lv_color_hex(WoodBottom), 0);
    lv_obj_set_style_bg_grad_dir(woodCard, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_clip_corner(woodCard, true, 0);
    onClick(woodCard, strikeClick);
    auto *fish = picture(woodCard, &daily_wood_fish, 7, 38);  // 86x49 cut-out
    animFish = fish;
    fishBaseY = lv_obj_get_y(fish);
    picture(woodCard, &daily_wood_deco, 60, 20);              // mallet decoration 34x31
    label(woodCard, "今日敲击", 9, 8, 70, F9, WoodShadow);     // drop shadow first
    label(woodCard, "今日敲击", 9, 7, 70, F9, WoodText);
    knockShadow = label(woodCard, "0", 10, 27, 80, F32, WoodShadow);
    knockValue = label(woodCard, "0", 10, 26, 80, F32, WoodText);
    knockUnit = label(woodCard, "次", 30, 44, 20, F12, WoodText);
    label(woodCard, "更多 >", 70, 9, 30, F7, WoodText);
    auto *moreHit = hit(woodCard, 64, 4, 36, 22);  // generous tap target, above the card
    onClick(moreHit, openWoodDetail);

    // Cat illustration (112,140) 96x91 (99x94 artwork scaled by 248/256).
    auto *cat = picture(parent, saved_gift_daily_mascot, 112, 140);
    lv_img_set_pivot(cat, 0, 0);
    lv_img_set_zoom(cat, 248);

    // Water card (213,140) 99x91 r12.
    auto *waterCard = decor(parent, 213, 140, 99, 91, WaterBg, 12);
    lv_obj_set_style_clip_corner(waterCard, true, 0);
    waterWave = picture(waterCard, &daily_water_wave, 0, 62);  // 99x29 ellipse slice
    animWave = waterWave;
    label(waterCard, "今日饮水", 9, 6, 74, F8, DarkBlue);
    waterValue = label(waterCard, "0", 9, waterNumberY, 74, F28, DarkBlue);
    waterUnit = label(waterCard, "ml", 26, 30, 20, F11, DarkBlue);
    auto *track = decor(waterCard, 9, 52, 80, 7, 0xFFFFFF, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(track, LV_OPA_70, 0);
    lv_obj_set_style_clip_corner(track, true, 0);
    waterFill = decor(track, 0, 0, 0, 7, WaterDeep, LV_RADIUS_CIRCLE);
    waterGoal = label(waterCard, "目标 1000 ml", 9, 62, 84, F7, GoalInk);
    // Tapping the stats area opens the water log (undo lives there).
    auto *waterHit = hit(waterCard, 0, 0, 76, 52);
    onClick(waterHit, openWaterDetail);
    for (int i = 0; i < 2; ++i) {
        auto *b = Ui::panel(waterCard, 8 + i * 45, 72, 38, 16, 0xFFFFFF, 5);
        onClick(b, addWaterClick, reinterpret_cast<void *>(static_cast<uintptr_t>(i == 0 ? 200 : 500)));
        label(b, i == 0 ? "+200 ml" : "+500 ml", 4, 2, 34, F7, WaterDeep);
    }
}

// ---- refresh helpers -------------------------------------------------------
void applyKnocks(int knocks) {
    if (!knockValue) return;
    const lv_font_t *font = knockFont(knocks);
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", knocks);
    setText(knockValue, buffer);
    setFont(knockValue, font);
    const int y = knockBaseline - baselineFromTop(font);
    place(knockValue, 10, y);
    if (knockShadow) {
        setText(knockShadow, buffer);
        setFont(knockShadow, font);
        place(knockShadow, 10, y + 1);
    }
    if (knockUnit) place(knockUnit, 10 + textWidth(buffer, font) + 2, knockBaseline - baselineFromTop(F12));
}

void applyWater(int total, int goal, bool animate) {
    if (goal < 1) goal = 1000;
    const lv_font_t *font = total >= 10000 ? F20 : F28;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", total);
    if (waterValue) {
        setText(waterValue, buffer);
        setFont(waterValue, font);
        place(waterValue, 9, waterNumberY + (baselineFromTop(F28) - baselineFromTop(font)));
    }
    if (waterUnit) place(waterUnit, 9 + textWidth(buffer, font) + 2, 30);
    if (waterGoal) {
        char goalText[32];
        snprintf(goalText, sizeof(goalText), "目标 %d ml", goal);
        setText(waterGoal, goalText);
    }
    int percent = total * 100 / goal;
    if (percent > 100) percent = 100;
    if (waterFill) {
        int width = 80 * percent / 100;
        if (width < 1) {
            setVisible(waterFill, false);
        } else {
            setVisible(waterFill, true);
            if (lv_obj_get_width(waterFill) != width) lv_obj_set_width(waterFill, width);
        }
    }
    raiseWater(static_cast<lv_coord_t>(62 - percent * 20 / 100), animate);
}

void applyMood() {
    auto today = GiftData::today();
    auto record = GiftData::record(today);
    const String mood = record["mood"] | "";
    const lv_img_dsc_t *face = bigMood(mood);
    if (todayFace) {
        if (face) {
            setSrc(todayFace, face);
            setVisible(todayFace, true);
        } else {
            setVisible(todayFace, false);
        }
    }
    setVisible(todayPlusH, face == nullptr);
    setVisible(todayPlusV, face == nullptr);

    int64_t now = ClockSource::now();
    auto date = DeskLogic::beijingDate(now);
    const int todayIndex = (dayOfWeek(date) + 6) % 7;
    for (int i = 0; i < 7; ++i) {
        auto day = DeskLogic::beijingDate(now + static_cast<int64_t>(i - todayIndex) * 86400);
        const String dayMood = GiftData::record(dateKey(day))["mood"] | "";
        const lv_img_dsc_t *weekFace = weekMood(dayMood);
        if (weekFaces[i]) {
            setSrc(weekFaces[i], weekFace ? weekFace : &daily_mood_week_calm);
            setVisible(weekFaces[i], weekFace != nullptr);
        }
        setVisible(weekPluses[i], weekFace == nullptr);
    }
}

void updateMain() {
    applyMood();
    auto record = GiftData::record(GiftData::today());
    applyKnocks(record["knocks"] | 0);
    const int goal = GiftData::get("daily")["waterGoal"] | 1000;
    applyWater(waterTotal(record), goal, true);
}

// ---- editor ----------------------------------------------------------------
void buildMoodEditor(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(Panel), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    navBar(parent, ("选择心情 · " + editorDateLabel()).c_str());
    auto record = GiftData::record(editorDate);
    const String selected = record["mood"] | "";
    for (int i = 0; i < 6; ++i) {
        const int x = 12 + (i % 3) * 102;
        const int y = 39 + (i / 3) * 88;
        auto *card = Ui::panel(parent, x, y, 92, 82, EditorCard, 12);
        lv_obj_set_style_border_color(card, lv_color_hex(Gold), 0);
        lv_obj_set_style_border_width(card, selected == MoodIds[i] ? 2 : 1, 0);
        onClick(card, moodClick, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        Ui::attachPressFeedback(card);
        picture(card, bigMood(MoodIds[i]), 17, 4);  // 59x59 centred in the 92x82 card
        centered(card, MoodNames[i], 0, 66, 92, F11, Ink);
    }
    label(parent, "点表情 = 保存并返回；未来日期不可修改", 20, 219, 292, F8, HintInk);
    moodError = centered(parent, "", 10, 205, 300, F9, ErrorInk);
    setVisible(moodError, false);
}

// ---- water detail ----------------------------------------------------------
void buildWaterDetail(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(Panel), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    navBar(parent, "喝水记录");

    auto *card = decor(parent, 8, 38, 304, 86, WaterBg, 12);
    label(card, "今日饮水", 16, 8, 90, F11, DarkBlue);
    auto today = GiftData::today();
    auto record = GiftData::record(today);
    const int total = waterTotal(record);
    const int goal = GiftData::get("daily")["waterGoal"] | 1000;
    char number[16];
    snprintf(number, sizeof(number), "%d", total);
    label(card, number, 16, 22, 150, F28, DarkBlue);
    label(card, "ml", 16 + textWidth(number, F28) + 2, 34, 30, F11, DarkBlue);
    char goalText[32];
    snprintf(goalText, sizeof(goalText), "目标 %d ml", goal);
    label(card, goalText, 170, 30, 120, F9, GoalInk);
    auto *track = decor(card, 16, 62, 272, 10, 0xFFFFFF, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(track, LV_OPA_70, 0);
    lv_obj_set_style_clip_corner(track, true, 0);
    int percent = total * 100 / (goal < 1 ? 1000 : goal);
    if (percent > 100) percent = 100;
    int fillWidth = 272 * percent / 100;
    if (fillWidth > 0) decor(track, 0, 0, fillWidth > 10 ? fillWidth : 10, 10, WaterDeep, LV_RADIUS_CIRCLE);

    const char *captions[3] = {"+200 ml", "+500 ml", "撤销上次"};
    int actions[3] = {200, 500, 0};
    for (int i = 0; i < 3; ++i) {
        const int x = 8 + i * 104;
        auto *button = Ui::panel(parent, x, 132, 92, 30, 0xFFFFFF, 8);
        if (actions[i] == 0) onClick(button, waterUndoClick);
        else onClick(button, addWaterClick, reinterpret_cast<void *>(static_cast<uintptr_t>(actions[i])));
        Ui::attachPressFeedback(button);
        centered(button, captions[i], 2, 8, 88, F11, WaterDeep);
    }

    auto *log = Ui::panel(parent, 8, 168, 304, 64, 0xFFFDF3, 10);
    lv_obj_add_flag(log, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(log, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(log, LV_SCROLLBAR_MODE_ACTIVE);
    int row = 0;
    for (JsonVariantConst item : record["water"].as<JsonArrayConst>()) {
        char line[40];
        snprintf(line, sizeof(line), "%s   +%d ml",
                 TextUtil::formatDate((item["at"] | 0LL) / 1000, "%H:%M").c_str(), item["amount"] | 0);
        label(log, line, 12, 8 + row * 20, 280, F11, Ink);
        ++row;
    }
    if (row == 0) centered(log, "今天还没有饮水记录。", 8, 22, 288, F11, HintInk);
}

// ---- wooden-fish detail ----------------------------------------------------
void buildWoodDetail(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(Panel), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    navBar(parent, "敲木鱼");

    auto *card = Ui::panel(parent, 8, 38, 304, 80, WoodTop, 11);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(WoodBottom), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    picture(card, &daily_wood_fish, 204, 16);  // right side, may be static
    label(card, "今日敲击", 16, 10, 90, F9, WoodText);
    auto record = GiftData::record(GiftData::today());
    const int knocks = record["knocks"] | 0;
    const lv_font_t *font = knockFont(knocks);
    char number[16];
    snprintf(number, sizeof(number), "%d", knocks);
    const int baseline = 56;
    label(card, number, 16, baseline - baselineFromTop(font), 150, font, WoodText);
    label(card, "次", 16 + textWidth(number, font) + 2, baseline - baselineFromTop(F12), 24, F12, WoodText);

    const bool sound = GiftData::get("daily")["sound"] | true;
    auto *undo = Ui::panel(parent, 8, 128, 94, 30, 0xFFFFFF, 8);
    onClick(undo, strikeUndoClick);
    centered(undo, "撤销一次", 2, 8, 90, F11, Ink);
    auto *soundButton = Ui::panel(parent, 112, 128, 94, 30, 0xFFFFFF, 8);
    onClick(soundButton, soundToggleClick);
    centered(soundButton, sound ? "声音：开" : "声音：关", 2, 8, 90, F11, Ink);
    auto *reset = Ui::panel(parent, 216, 128, 94, 30, 0xFFFFFF, 8);
    onClick(reset, strikeResetClick);
    centered(reset, "清零", 2, 8, 90, F11, Ink);

    centered(parent, "点击木鱼即可敲击，音量跟随设备设置。", 8, 168, 304, F9, HintInk);
    centered(parent, "撤销一次 = 减掉最近一次敲击；清零 = 今日次数归零。", 8, 190, 304, F8, HintInk);
}

}  // namespace

// ---- module contract -------------------------------------------------------
void build(lv_obj_t *parent) {
    cancel();
    resetPointers();
    if (forcedPending) {
        switch (forcedView) {
            case 1: view = View::MoodEditor; break;
            case 2: view = View::WaterDetail; break;
            case 3: view = View::WoodDetail; break;
            default: view = View::Main; break;
        }
        forcedPending = false;
        internalNav = false;
    } else if (!internalNav) {
        view = View::Main;
    }
    internalNav = false;
    builtToday = GiftData::today();
    if (view == View::MoodEditor && editorDate.isEmpty()) editorDate = builtToday;
    if (view == View::MoodEditor && editorDate.isEmpty()) editorDate = "1970-01-01";
    switch (view) {
        case View::Main: buildMain(parent); break;
        case View::MoodEditor: buildMoodEditor(parent); break;
        case View::WaterDetail: buildWaterDetail(parent); break;
        case View::WoodDetail: buildWoodDetail(parent); break;
    }
    built = true;
    if (view == View::Main) updateMain();
}

void refresh(bool force) {
    (void)force;
    if (view != View::Main) return;
    String today = GiftData::today();
    if (today != builtToday) {
        requestRebuild();  // midnight roll-over (or the first valid time): rebuild with the new week
        return;
    }
    updateMain();
}

bool ready() {
    if (!built) return false;
    if (view != View::Main) return true;
    return todayFace && weekFaces[0] && knockValue && knockUnit && waterValue && waterUnit && waterFill &&
           waterWave && waterGoal;
}

void cancel() {
    cancelAnim(animFish);
    cancelAnim(animWave);
    built = false;
    resetPointers();
}

bool back() {
    if (view == View::Main) return false;
    goMain();
    return true;
}

// Pure query used by the swipe router before the gesture is executed; it must never
// change the view (back() above performs the transition).
bool canBack() { return view != View::Main; }

void debugVariant(int which) {
    forcedView = which;
    forcedPending = true;
}

}  // namespace DailyPage
