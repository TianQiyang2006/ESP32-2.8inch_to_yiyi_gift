#include "mini_apps.h"
#include "snake_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

namespace SnakePage {
namespace {
using namespace SnakeEngine;
constexpr uint32_t Bg = 0xF7F7EE, Ink = 0x213F32, Muted = 0x60755C;
constexpr uint32_t Green = 0x315C43, Light = 0xE3ECD9, White = 0xFFFFFF;
constexpr int CellSize = 16, BoardY = 32;
const lv_font_t *F14 = &fd_sans_bold_14_ui, *F16 = &fd_sans_bold_16_ui;
enum Command { Back = 1, Pause, Resume, Restart, Start, Slow = 10, Standard, Fast };
Game game;
lv_obj_t *pageRoot = nullptr, *board = nullptr, *overlay = nullptr;
lv_obj_t *scoreLabel = nullptr, *bestLabel = nullptr;
lv_obj_t *speedKeys[3] = {};
lv_timer_t *timer = nullptr;
int overlayKind = -1;
bool loaded = false, dragging = false, swipeUsed = false;
uint32_t storedBest = 0;
lv_point_t pressPoint{};

const char *stateName(State state) {
    switch (state) {
        case State::Ready: return "ready";
        case State::Playing: return "playing";
        case State::Paused: return "paused";
        case State::Won: return "won";
        default: return "gameover";
    }
}
void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius = 8) {
    auto *obj = lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0); lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
lv_obj_t *text(lv_obj_t *parent, const String &value, int x, int y, int w,
               const lv_font_t *font = nullptr, uint32_t color = Ink, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    if (!font) font = F14;
    auto *obj = lv_label_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, font->line_height + 2);
    lv_obj_set_style_text_font(obj, font, 0); lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, align, 0); lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, value.c_str()); lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}
lv_obj_t *button(lv_obj_t *parent, const char *value, int x, int y, int w, int h, int command, bool primary = false) {
    auto *obj = box(parent, x, y, w, h, primary ? Green : Light);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, event, LV_EVENT_CLICKED, (void *)(intptr_t)command);
    Ui::attachPressFeedback(obj);
    text(obj, value, 2, (h - F16->line_height) / 2, w - 4, F16, primary ? White : Ink, LV_TEXT_ALIGN_CENTER);
    return obj;
}
void loadBest() {
    if (loaded) return;
    Preferences prefs;
    if (!prefs.begin("snake", false)) return;
    if (!prefs.isKey("best")) loaded = true;
    else {
        const uint32_t value = prefs.getUInt("best", 0xFFFFFFFFu);
        // An unreadable existing record must never be overwritten.
        if (value != 0xFFFFFFFFu) { storedBest = value; loaded = true; }
    }
    prefs.end();
    if (loaded && storedBest > game.best) game.best = storedBest;
}
void persistBest() {
    loadBest();
    if (!loaded || game.best <= storedBest) return;
    Preferences prefs;
    if (!prefs.begin("snake", false)) return;
    if (prefs.putUInt("best", game.best) == sizeof(uint32_t)) storedBest = game.best;
    prefs.end();
}
void rect(lv_draw_ctx_t *ctx, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER; dsc.bg_color = lv_color_hex(color); dsc.radius = radius;
    lv_area_t area = {(lv_coord_t)x, (lv_coord_t)y, (lv_coord_t)(x + w - 1), (lv_coord_t)(y + h - 1)};
    lv_draw_rect(ctx, &dsc, &area);
}
void drawBoard(lv_event_t *e) {
    auto *ctx = lv_event_get_draw_ctx(e); lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(e), &area);
    for (int y = 0; y < Rows; ++y)
        for (int x = 0; x < Columns; ++x)
            rect(ctx, area.x1 + x * CellSize, area.y1 + y * CellSize, CellSize, CellSize,
                 (x + y) % 2 ? 0xEAF0DF : 0xF1F5E8, 0);
    if (game.food.x >= 0 && game.food.y >= 0) {
        int fx = area.x1 + game.food.x * CellSize, fy = area.y1 + game.food.y * CellSize;
        rect(ctx, fx + 3, fy + 4, 11, 10, 0xCF7861, 4);
        rect(ctx, fx + 8, fy + 1, 5, 4, 0x5D8553, 2);
        rect(ctx, fx + 5, fy + 6, 2, 3, 0xF7C3A1, 1);
    }
    for (int i = game.length - 1; i >= 0; --i) {
        const Cell cell = game.body[i];
        int x = area.x1 + cell.x * CellSize, y = area.y1 + cell.y * CellSize;
        const uint32_t color = i == 0 ? Green : (i % 2 ? 0x779C64 : 0x86AA70);
        rect(ctx, x + 1, y + 1, 14, 14, color, i == 0 || i == game.length - 1 ? 5 : 3);
        if (i > 0) {
            const Cell previous = game.body[i - 1];
            if (previous.x == cell.x + 1) rect(ctx, x + 10, y + 3, 7, 10, color, 0);
            else if (previous.x == cell.x - 1) rect(ctx, x - 1, y + 3, 7, 10, color, 0);
            else if (previous.y == cell.y + 1) rect(ctx, x + 3, y + 10, 10, 7, color, 0);
            else if (previous.y == cell.y - 1) rect(ctx, x + 3, y - 1, 10, 7, color, 0);
        }
    }
    if (game.length) {
        int x = area.x1 + game.body[0].x * CellSize, y = area.y1 + game.body[0].y * CellSize;
        bool vertical = game.direction == Direction::Up || game.direction == Direction::Down;
        int ahead = game.direction == Direction::Up || game.direction == Direction::Left ? 4 : 10;
        for (int offset = 4; offset <= 10; offset += 6) {
            rect(ctx, x + (vertical ? offset : ahead), y + (vertical ? ahead : offset), 3, 3, White, 1);
            rect(ctx, x + (vertical ? offset + 1 : ahead + (game.direction == Direction::Left ? 0 : 1)),
                 y + (vertical ? ahead + (game.direction == Direction::Up ? 0 : 1) : offset + 1), 1, 1, Ink, 0);
        }
    }
}
void drawIcon(lv_event_t *e) {
    auto *ctx = lv_event_get_draw_ctx(e); lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    rect(ctx, a.x1, a.y1, 56, 56, 0xDBE8CA, 14);
    rect(ctx, a.x1 + 9, a.y1 + 33, 29, 11, 0x7F9F65, 5);
    rect(ctx, a.x1 + 9, a.y1 + 17, 11, 27, 0x7F9F65, 5);
    rect(ctx, a.x1 + 9, a.y1 + 16, 30, 12, 0x7F9F65, 5);
    rect(ctx, a.x1 + 28, a.y1 + 12, 17, 20, Green, 7);
    rect(ctx, a.x1 + 38, a.y1 + 16, 4, 4, White, 2);
    rect(ctx, a.x1 + 40, a.y1 + 17, 2, 2, Ink, 1);
    rect(ctx, a.x1 + 41, a.y1 + 38, 7, 8, 0xCF7861, 3);
    rect(ctx, a.x1 + 45, a.y1 + 35, 4, 4, 0x5D8553, 2);
}
void syncLabels() {
    if (!pageRoot) return;
    lv_label_set_text_fmt(scoreLabel, "得分 %lu", (unsigned long)game.score);
    lv_label_set_text_fmt(bestLabel, "最高 %lu", (unsigned long)game.best);
}
void closeOverlay() {
    Ui::cancelAnimations();
    if (overlay) lv_obj_del(overlay);
    overlay = nullptr; overlayKind = -1; dragging = swipeUsed = false;
}
void openOverlay(int kind) {
    closeOverlay();
    overlay = box(pageRoot, 0, 0, 320, 240, 0x1E3329, 0);
    lv_obj_set_style_bg_opa(overlay, 145, 0); lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    auto *card = box(overlay, 28, 36, 264, 168, White, 14);
    if (kind == (int)State::Ready) {
        text(card, "贪吃蛇", 12, 8, 240, F16, Green, LV_TEXT_ALIGN_CENTER);
        button(card, "返回", 204, 4, 48, 28, Back);
        text(card, "在棋盘上滑动，控制方向", 8, 34, 248, F14, Ink, LV_TEXT_ALIGN_CENTER);
        text(card, "吃到果实加10分", 8, 54, 248, F14, Muted, LV_TEXT_ALIGN_CENTER);
        text(card, "碰墙或碰到自己结束", 8, 73, 248, F14, Muted, LV_TEXT_ALIGN_CENTER);
        const char *speeds[] = {"慢速", "标准", "快速"};
        for (int i = 0; i < 3; ++i)
            speedKeys[i] = button(card, speeds[i], 16 + i * 80, 96, 72, 28, Slow + i, game.speed == i);
        button(card, "开始游戏", 16, 128, 232, 32, Start, true);
    } else {
        const bool paused = kind == (int)State::Paused, won = kind == (int)State::Won;
        text(card, paused ? "已暂停" : won ? "填满棋盘" : "游戏结束", 12, 16, 240, F16, Green, LV_TEXT_ALIGN_CENTER);
        text(card, String("得分 ") + String((unsigned long)game.score) + "   最高 " + String((unsigned long)game.best),
             8, 48, 248, F16, Ink, LV_TEXT_ALIGN_CENTER);
        const char *speeds[] = {"慢速", "标准", "快速"};
        for (int i = 0; i < 3; ++i)
            speedKeys[i] = button(card, speeds[i], 16 + i * 80, 78, 72, 28, Slow + i, game.speed == i);
        if (paused) {
            button(card, "继续", 12, 112, 64, 40, Resume, true);
            button(card, "重新开始", 80, 112, 100, 40, Restart);
            button(card, "返回", 184, 112, 68, 40, Back);
        } else {
            button(card, "再来一局", 16, 112, 136, 40, Restart, true);
            button(card, "返回", 160, 112, 88, 40, Back);
        }
    }
    overlayKind = kind;
}
void apply() {
    syncLabels();
    if (board) lv_obj_invalidate(board);
    if (game.state == State::Playing) { if (overlay) closeOverlay(); }
    else if (overlayKind != (int)game.state) openOverlay((int)game.state);
}
void event(lv_event_t *e) {
    const int command = (int)(intptr_t)lv_event_get_user_data(e);
    lastInputAt = millis();
    switch (command) {
        case Back: {
            persistBest();
            lv_event_t forwarded = *e; forwarded.user_data = (void *)(uintptr_t)Ui::Back;
            Ui::dispatchAction(&forwarded); return;
        }
        case Pause: game.pause(); persistBest(); break;
        case Resume: game.resume(millis()); break;
        case Start: game.start(millis(), millis() * 2654435761u + 0x9E3779B9u); break;
        case Restart: persistBest(); game.restart(millis()); break;
        case Slow: case Standard: case Fast:
            if (game.state != State::Playing) {
                game.speed = (uint8_t)(command - Slow);
                // Recolor the existing speed controls without deleting the
                // currently dispatched button (its press animation may run).
                Ui::cancelAnimations();
                for (unsigned i = 0; i < 3; ++i) {
                    auto *key = speedKeys[i];
                    if (!key) continue;
                    const bool active = i == game.speed;
                    lv_obj_set_style_bg_color(key, lv_color_hex(active ? Green : Light), 0);
                    lv_obj_set_style_text_color(lv_obj_get_child(key, 0), lv_color_hex(active ? White : Ink), 0);
                }
            }
            break;
        default: return;
    }
    apply();
}
void touchBoard(lv_event_t *e) {
    auto *input = lv_indev_get_act(); if (!input) return;
    const auto code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(input, &pressPoint); dragging = true; swipeUsed = false;
    } else if (code == LV_EVENT_PRESSING && dragging && !swipeUsed && game.state == State::Playing) {
        lv_point_t p; lv_indev_get_point(input, &p);
        int dx = p.x - pressPoint.x, dy = p.y - pressPoint.y;
        if (abs(dx) < 12 && abs(dy) < 12) return;
        game.turn(abs(dx) > abs(dy) ? (dx > 0 ? Direction::Right : Direction::Left) : (dy > 0 ? Direction::Down : Direction::Up));
        swipeUsed = true; lastInputAt = millis();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) dragging = swipeUsed = false;
}
void onTick(lv_timer_t *) {
    if (!pageRoot) return;
    if (game.tick(millis())) {
        if (game.state == State::GameOver || game.state == State::Won) persistBest();
        apply();
    }
}
}  // namespace

void createIcon(lv_obj_t *parent, int x, int y) {
    auto *obj = lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, 56, 56);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, drawIcon, LV_EVENT_DRAW_MAIN, nullptr);
}
void build(lv_obj_t *parent) {
    loadBest();
    pageRoot = parent; board = overlay = scoreLabel = bestLabel = nullptr; overlayKind = -1;
    dragging = swipeUsed = false;
    if (!game.length) game.prepare(millis() * 2654435761u + 0x9E3779B9u);
    lv_obj_set_style_bg_color(parent, lv_color_hex(Bg), 0); lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    button(parent, "返回", 0, 0, 40, 32, Back);
    text(parent, "贪吃蛇", 45, 6, 62, F16);
    scoreLabel = text(parent, "", 112, 8, 63, F14, Green);
    bestLabel = text(parent, "", 178, 8, 77, F14, Muted);
    button(parent, "暂停", 260, 0, 60, 32, Pause);
    board = box(parent, 0, BoardY, Columns * CellSize, Rows * CellSize, Bg, 0);
    lv_obj_add_flag(board, LV_OBJ_FLAG_CLICKABLE);
    // Keep a drag captured by its original board even if the finger reaches
    // the header; it must never activate a navigation or pause control.
    lv_obj_add_flag(board, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(board, drawBoard, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board, touchBoard, LV_EVENT_ALL, nullptr);
    if (timer) lv_timer_del(timer);
    timer = lv_timer_create(onTick, 20, nullptr);
    // Background redraws start a fresh movement interval, avoiding a hidden
    // elapsed tick immediately after the new widgets are created.
    if (game.state == State::Playing) game.lastStepAt = millis();
    apply();
}
void refresh(bool force) {
    if (!pageRoot) return;
    if (force) apply();
}
void cancel() {
    if (timer) { lv_timer_del(timer); timer = nullptr; }
    if (pageRoot) persistBest();
    if (game.state == State::Playing && app != App::Snake) game.pause();
    pageRoot = board = overlay = scoreLabel = bestLabel = nullptr;
    overlayKind = -1; dragging = swipeUsed = false;
}
bool back() { return false; }
bool canBack() { return false; }
Status status() {
    return {stateName(game.state), game.score, game.best, game.length, game.intervalMs(), game.speed,
            game.body[0].x, game.body[0].y, (uint8_t)game.direction};
}
uint32_t bestScore() { loadBest(); return game.best > storedBest ? game.best : storedBest; }

#ifdef GIFT_HOST_TEST
void debugVariant(int which) {
    uint32_t best = game.best;
    game = Game{}; game.best = best;
    game.prepare(20261112u); loadBest();
    if (which == 0) { apply(); return; }
    game.start(millis(), 20261112u);
    if (which == 5) {
        for (int i = 0; i < 4; ++i) game.body[i] = {(int8_t)(8 - i), 5};
        game.food = {9, 5};
    } else if (which == 6) {
        for (int i = 0; i < 4; ++i) game.body[i] = {(int8_t)(19 - i), 5};
        game.food = {2, 2};
    } else {
        static const Cell shape[] = {{12,3},{11,3},{10,3},{9,3},{8,3},{7,3},{6,3},{6,4},{6,5},{7,5},{8,5},{9,5},{10,5},{11,5}};
        game.length = 14;
        for (unsigned i = 0; i < game.length; ++i) game.body[i] = shape[i];
        game.score = 100; if (game.best < game.score) game.best = game.score;
        game.food = {13, 3};
        if (which == 2) game.pause();
        else if (which == 3) game.state = State::GameOver;
        else if (which == 4) {
            game.length = Capacity;
            for (int i = 0; i < Capacity; ++i) game.body[i] = {(int8_t)(i / Columns % 2 ? Columns - 1 - i % Columns : i % Columns), (int8_t)(i / Columns)};
            game.score = (Capacity - 4) * 10; game.best = game.score; game.food = {-1, -1}; game.state = State::Won;
        }
    }
    apply();
}
#endif
}  // namespace SnakePage
