#include "mini_apps.h"
#include "jump_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

namespace JumpPage {
namespace {
using namespace JumpEngine;
constexpr uint32_t Bg = 0xF5F4EA, Ink = 0x273F3D, Muted = 0x71837A;
constexpr uint32_t Green = 0x365E50, Light = 0xE5ECDD, White = 0xFFFFFF;
const lv_font_t *F14 = &fd_sans_bold_14_ui, *F16 = &fd_sans_bold_16_ui;
enum Command { Back = 1, Pause, Resume, Restart, Start };
Game game;
lv_obj_t *pageRoot = nullptr, *stage = nullptr, *overlay = nullptr;
lv_obj_t *scoreLabel = nullptr, *bestLabel = nullptr;
lv_timer_t *timer = nullptr;
int overlayKind = -1;
bool prepared = false, loaded = false, blockUntilRelease = false, resumeRebuild = false;
uint32_t storedBest = 0, shownScore = UINT32_MAX, shownBest = UINT32_MAX;

const char *stateName(State state) {
    switch (state) {
        case State::Ready: return "ready";
        case State::Playing: return "playing";
        case State::Charging: return "charging";
        case State::Jumping: return "jumping";
        case State::Settling: return "settling";
        case State::Falling: return "falling";
        case State::Paused: return "paused";
        default: return "gameover";
    }
}
bool pointerDown() {
    // LVGL 8 keeps the sampled state on the input processor. This also works
    // during a page rebuild, when no input device is currently dispatching.
    for (auto *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        if (input->driver->type == LV_INDEV_TYPE_POINTER && input->proc.state == LV_INDEV_STATE_PR) return true;
    return false;
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
    if (!prefs.begin("jump", false)) return;
    if (!prefs.isKey("best")) loaded = true;
    else {
        const uint32_t value = prefs.getUInt("best", UINT32_MAX);
        if (value != UINT32_MAX) { storedBest = value; loaded = true; }
    }
    prefs.end();
    if (loaded && storedBest > game.best) game.best = storedBest;
}
void persistBest() {
    loadBest();
    if (!loaded || game.best <= storedBest) return;
    Preferences prefs;
    if (!prefs.begin("jump", false)) return;
    if (prefs.putUInt("best", game.best) == sizeof(uint32_t)) storedBest = game.best;
    prefs.end();
}
void rect(lv_draw_ctx_t *ctx, int x, int y, int w, int h, uint32_t color, int radius = 0) {
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER; dsc.bg_color = lv_color_hex(color); dsc.radius = radius;
    lv_area_t a = {(lv_coord_t)x, (lv_coord_t)y, (lv_coord_t)(x+w-1), (lv_coord_t)(y+h-1)};
    lv_draw_rect(ctx, &dsc, &a);
}
void polygon(lv_draw_ctx_t *ctx, const lv_point_t *points, uint32_t color) {
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER; dsc.bg_color = lv_color_hex(color);
    lv_draw_polygon(ctx, &dsc, points, 4);
}
void diamond(lv_draw_ctx_t *ctx, int x, int y, int r, uint32_t color) {
    lv_point_t p[] = {{(lv_coord_t)x,(lv_coord_t)(y-r)}, {(lv_coord_t)(x+2*r),(lv_coord_t)y},
                     {(lv_coord_t)x,(lv_coord_t)(y+r)}, {(lv_coord_t)(x-2*r),(lv_coord_t)y}};
    polygon(ctx, p, color);
}
void platform(lv_draw_ctx_t *ctx, int x, int y, int r, unsigned style, bool target) {
    static const uint32_t tops[] = {0xBFD3BD, 0xE9C2AB, 0xBBCFD8, 0xE8D49F};
    static const uint32_t lefts[] = {0x8BA992, 0xC5917C, 0x89A7B5, 0xBAA56F};
    static const uint32_t rights[] = {0x6F9482, 0xAD7D6E, 0x7191A0, 0xA18E5E};
    style %= 4;
    rect(ctx, x-2*r+7, y+r+14, 4*r-14, 8, 0xE4E5D9, 6);
    lv_point_t left[] = {{(lv_coord_t)(x-2*r),(lv_coord_t)y}, {(lv_coord_t)x,(lv_coord_t)(y+r)},
                        {(lv_coord_t)x,(lv_coord_t)(y+r+18)}, {(lv_coord_t)(x-2*r),(lv_coord_t)(y+18)}};
    lv_point_t right[] = {{(lv_coord_t)x,(lv_coord_t)(y+r)}, {(lv_coord_t)(x+2*r),(lv_coord_t)y},
                         {(lv_coord_t)(x+2*r),(lv_coord_t)(y+18)}, {(lv_coord_t)x,(lv_coord_t)(y+r+18)}};
    polygon(ctx, left, lefts[style]); polygon(ctx, right, rights[style]);
    diamond(ctx, x, y, r, tops[style]);
    if (target) { diamond(ctx, x, y, 7, White); diamond(ctx, x, y, 3, rights[style]); }
}
void drawText(lv_draw_ctx_t *ctx, const char *value, int x, int y, int w, uint32_t color = Muted) {
    lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
    d.font = F14; d.color = lv_color_hex(color); d.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t a = {(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+w-1),(lv_coord_t)(y+F14->line_height+1)};
    lv_draw_label(ctx, &d, &a, value, nullptr);
}
float bounded(float value) { return value < 0 ? 0 : value > 1 ? 1 : value; }
void pawn(lv_draw_ctx_t *ctx, int x, int y, float squash) {
    const int h = 17 - (int)(squash*6), w = 14 + (int)(squash*6);
    rect(ctx, x-w/2, y-h, w, h, Ink, 5);
    rect(ctx, x-6, y-h-11, 12, 12, Ink, 6);
    rect(ctx, x+2, y-h-8, 2, 2, 0xE9EFD9, 1);
    rect(ctx, x-w/2+3, y-h+3, 3, h-5, 0x45655B, 2);
}
void drawStage(lv_event_t *e) {
    auto *ctx = lv_event_get_draw_ctx(e); lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    const uint32_t now = millis();
    const State phase = game.state == State::Paused ? game.pausedState : game.state;
    float cx = game.direction > 0 ? 90.0f : 230.0f, cy = 133.0f;
    float tx = cx + game.direction*game.targetDistance, ty = cy-game.targetDistance*0.5f;
    float px = cx, py = cy, height = 0;
    if (phase == State::Jumping || phase == State::Settling || phase == State::Falling || phase == State::GameOver) {
        const float t = game.flightProgress(now);
        px += game.direction*game.jumpDistance*t; py -= game.jumpDistance*t*0.5f;
        height = game.height(now);
    }
    if (phase == State::Settling) {
        const float t = bounded((float)game.elapsed(now)/SettleDurationMs), ease = t*t*(3-2*t);
        if (game.landedTarget) {
            const float nx = game.nextDirection > 0 ? 90.0f : 230.0f;
            const float dx = (nx-tx)*ease, dy = (133.0f-ty)*ease;
            // The camera follows the landed platform; the pawn moves gently
            // from its actual contact point to the centre during this phase.
            px += dx + (tx-px)*ease; py += dy + (ty-py)*ease;
            cx += dx; cy += dy; tx += dx; ty += dy;
        } else { px += (cx-px)*ease; py += (cy-py)*ease; }
    }
    if (phase == State::Falling || phase == State::GameOver) {
        const float t = phase == State::GameOver ? 1 : bounded((float)game.elapsed(now)/FallDurationMs);
        height = -65*t*t;
    }
    const int ox = a.x1, oy = a.y1;
    // Quiet background marks help judge travel without obscuring the platforms.
    for (int y = 25; y < 175; y += 30)
        for (int x = 20+(y/30%2)*15; x < 315; x += 30) rect(ctx, ox+x, oy+y, 2, 2, 0xE3E7DA, 1);
    // Scoring updates on contact, before the camera finishes moving. Keep the
    // old scene's palette until that move completes so platforms retain colour.
    const unsigned sceneJumps = game.jumps - (phase == State::Settling && game.landedTarget ? 1u : 0u);
    platform(ctx, ox+(int)tx, oy+(int)ty, game.targetRadius, sceneJumps+1, true);
    platform(ctx, ox+(int)cx, oy+(int)cy, game.currentRadius, sceneJumps, false);
    rect(ctx, ox+(int)px-10, oy+(int)py-3, 20, 6, 0x7F9B87, 5);
    pawn(ctx, ox+(int)px, oy+(int)(py-height), phase == State::Charging ? game.chargeProgress(now) : 0);
    if (phase == State::Charging) {
        drawText(ctx, "松手起跳", ox+20, oy+169, 280, Green);
        rect(ctx, ox+56, oy+192, 208, 7, 0xDFE4D5, 4);
        rect(ctx, ox+56, oy+192, (int)(208*game.chargeProgress(now)), 7, Green, 4);
    } else if (phase == State::Settling && game.landedTarget) {
        drawText(ctx, game.perfect ? "中心 +2" : "+1", ox+70, oy+10, 180, Green);
    } else if (phase == State::Playing || phase == State::Ready) {
        drawText(ctx, "按住蓄力，松手起跳", ox+20, oy+184, 280);
    }
}
void drawIcon(lv_event_t *e) {
    auto *ctx = lv_event_get_draw_ctx(e); lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    rect(ctx, a.x1, a.y1, 56, 56, 0xE7E9D6, 14);
    diamond(ctx, a.x1+39, a.y1+21, 7, 0xCDA58F);
    rect(ctx, a.x1+25, a.y1+19, 2, 2, Muted, 1);
    rect(ctx, a.x1+28, a.y1+14, 2, 2, Muted, 1);
    diamond(ctx, a.x1+18, a.y1+42, 9, 0x89AB91);
    pawn(ctx, a.x1+18, a.y1+39, 0);
}
void closeOverlay() {
    Ui::cancelAnimations();
    if (overlay) lv_obj_del(overlay);
    overlay = nullptr; overlayKind = -1;
    blockUntilRelease = pointerDown();
}
void openOverlay(int kind) {
    closeOverlay();
    overlay = box(pageRoot, 0, 0, 320, 240, Ink, 0);
    lv_obj_set_style_bg_opa(overlay, 130, 0); lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    auto *card = box(overlay, 28, 36, 264, 168, White, 14);
    if (kind == (int)State::Ready) {
        text(card, "跳一跳", 12, 8, 240, F16, Green, LV_TEXT_ALIGN_CENTER);
        button(card, "返回", 204, 4, 48, 28, Back);
        text(card, "按住蓄力，松手起跳", 8, 34, 248, F14, Ink, LV_TEXT_ALIGN_CENTER);
        text(card, "跳到下一块平台得分", 8, 54, 248, F14, Muted, LV_TEXT_ALIGN_CENTER);
        text(card, "落在中心获得双倍分数", 8, 73, 248, F14, Muted, LV_TEXT_ALIGN_CENTER);
        button(card, "开始游戏", 16, 128, 232, 32, Start, true);
    } else {
        const bool paused = kind == (int)State::Paused;
        text(card, paused ? "已暂停" : "游戏结束", 12, 16, 240, F16, Green, LV_TEXT_ALIGN_CENTER);
        text(card, String("得分 ")+String((unsigned long)game.score)+"   最高 "+String((unsigned long)game.best),
             8, 48, 248, F16, Ink, LV_TEXT_ALIGN_CENTER);
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
bool modal() { return game.state == State::Ready || game.state == State::Paused || game.state == State::GameOver; }
void apply() {
    if (!pageRoot) return;
    if (shownScore != game.score) {
        lv_label_set_text_fmt(scoreLabel, "得分 %lu", (unsigned long)game.score); shownScore = game.score;
    }
    if (shownBest != game.best) {
        lv_label_set_text_fmt(bestLabel, "最高 %lu", (unsigned long)game.best); shownBest = game.best;
    }
    if (modal()) { if (overlayKind != (int)game.state) openOverlay((int)game.state); }
    else if (overlay) closeOverlay();
    if (stage) lv_obj_invalidate(stage);
}
void event(lv_event_t *e) {
    const int command = (int)(intptr_t)lv_event_get_user_data(e);
    lastInputAt = millis();
    switch (command) {
        case Back: {
            game.pause(millis()); persistBest();
            lv_event_t forwarded = *e; forwarded.user_data = (void *)(uintptr_t)Ui::Back;
            Ui::dispatchAction(&forwarded); return;
        }
        case Pause: game.pause(millis()); persistBest(); break;
        case Resume: game.resume(millis()); break;
        case Start: game.start(millis(), millis()*2654435761u+0x9E3779B9u); break;
        case Restart: persistBest(); game.restart(millis()); break;
        default: return;
    }
    blockUntilRelease = pointerDown();
    apply();
}
void touchStage(lv_event_t *e) {
    const auto code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    auto *input = lv_indev_get_act(); if (!input) return;
    if (code == LV_EVENT_PRESSED) {
        if (!blockUntilRelease && game.press(millis())) { lastInputAt = millis(); apply(); }
    } else if (code == LV_EVENT_PRESS_LOST) {
        if (game.cancelCharge()) apply();
    } else {
        lv_point_t p; lv_indev_get_point(input, &p); lv_area_t area; lv_obj_get_coords(stage, &area);
        const bool inside = p.x >= area.x1 && p.x <= area.x2 && p.y >= area.y1 && p.y <= area.y2;
        const bool changed = blockUntilRelease || !inside ? game.cancelCharge() : game.release(millis());
        blockUntilRelease = false;
        if (changed) { lastInputAt = millis(); apply(); }
    }
}
void onTick(lv_timer_t *) {
    if (!pageRoot) return;
    if (blockUntilRelease && !pointerDown()) blockUntilRelease = false;
    const bool changed = game.tick(millis());
    if (changed && game.state == State::GameOver) persistBest();
    const bool animated = game.state == State::Jumping || game.state == State::Settling || game.state == State::Falling;
    if (changed || animated) apply();
}
} // namespace

void createIcon(lv_obj_t *parent, int x, int y) {
    auto *obj = lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, 56, 56);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, drawIcon, LV_EVENT_DRAW_MAIN, nullptr);
}
void build(lv_obj_t *parent) {
    loadBest();
    if (!prepared) { game.prepare(millis()*2654435761u+0x9E3779B9u); prepared = true; }
    if (resumeRebuild) { game.resume(millis()); resumeRebuild = false; }
    pageRoot = parent; stage = overlay = scoreLabel = bestLabel = nullptr; overlayKind = -1;
    shownScore = shownBest = UINT32_MAX; blockUntilRelease = pointerDown();
    lv_obj_set_style_bg_color(parent, lv_color_hex(Bg), 0); lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    button(parent, "返回", 0, 0, 40, 32, Back);
    text(parent, "跳一跳", 45, 6, 62, F16);
    scoreLabel = text(parent, "", 112, 8, 63, F14, Green);
    bestLabel = text(parent, "", 178, 8, 77, F14, Muted);
    button(parent, "暂停", 260, 0, 60, 32, Pause);
    stage = box(parent, 0, 32, 320, 208, Bg, 0);
    lv_obj_add_flag(stage, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(stage, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(stage, drawStage, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(stage, touchStage, LV_EVENT_ALL, nullptr);
    if (timer) lv_timer_del(timer);
    timer = lv_timer_create(onTick, 20, nullptr);
    apply();
}
void refresh(bool force) { if (force) apply(); }
void cancel() {
    if (timer) { lv_timer_del(timer); timer = nullptr; }
    if (pageRoot) {
        // Same-view rebuilds consume the old contact and preserve flight time.
        // Leaving the app keeps a paused run for its next entry.
        game.cancelCharge();
        const bool pausedNow = game.pause(millis());
        resumeRebuild = app == App::Jump && pausedNow;
        persistBest();
    } else if (app != App::Jump) resumeRebuild = false;
    pageRoot = stage = overlay = scoreLabel = bestLabel = nullptr; overlayKind = -1;
    blockUntilRelease = pointerDown();
}
bool back() { return false; }
bool canBack() { return false; }
bool inputActive() { return game.state == State::Charging || blockUntilRelease; }
Status status() {
    const auto charge = game.chargeElapsed(millis());
    const auto active = game.state == State::Paused ? game.pausedState : game.state;
    const uint16_t flight = active == State::Jumping ? (uint16_t)game.elapsed(millis()) : 0;
    return {stateName(game.state), game.score, game.best, game.jumps, game.combo,
            charge, game.targetDistance, flight, (uint8_t)(charge*100u/ChargeLimitMs)};
}
uint32_t bestScore() { loadBest(); return game.best > storedBest ? game.best : storedBest; }

#ifdef GIFT_HOST_TEST
void debugVariant(int which) {
    const auto best = game.best; game = Game{}; game.best = best;
    game.prepare(20261112u); prepared = true; resumeRebuild = false; loadBest();
    if (which) {
        game.start(millis(), 20261112u); game.score = 8; game.jumps = 5; game.combo = 2;
        if (game.best < game.score) game.best = game.score;
        if (which == 2) game.pause(millis());
        else if (which == 3) game.state = State::GameOver;
        else if (which == 4) { game.press(millis()-300); game.tick(millis()); }
        else if (which == 5 || which == 6) {
            game.press(millis()-600); game.release(millis());
            if (which == 5) game.phaseAt -= 160;
            else { game.jumpDistance = 210; game.tick(millis()+FlightDurationMs); game.phaseAt = millis()-120; }
        }
    }
    blockUntilRelease = pointerDown(); apply();
}
#endif
} // namespace JumpPage
