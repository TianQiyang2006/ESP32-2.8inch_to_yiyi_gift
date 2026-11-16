#include "mini_apps.h"
#include "page_api.h"
#include "tetris_engine.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <Preferences.h>

// Production widgets for the Tetris mini app, drawn entirely with LVGL objects
// and two custom draw callbacks (board + next-piece preview). The native review
// runner renders this same implementation offscreen; screenshot scenarios only
// touch the public engine fields, exactly like focus_engine.h does for the alarm
// and pomodoro runners.
namespace TetrisPage {
namespace {
using namespace TetrisEngine;
enum Command { Back = 1, Pause, Resume, Restart, Left = 10, Right, Rotate, SoftDrop, HardDrop };
constexpr uint32_t Bg = 0xF7F7EE, Ink = 0x213F32, Muted = 0x51685A, Light = 0xE7EEDD, Green = 0x315C43, White = 0xFFFFFF;
constexpr uint32_t BoardBorder = 0xDCE5D4, BoardLine = 0xEEF2E7, GhostFill = 0xEDF1E6, Scrim = 0x1E3329;
constexpr uint32_t Card = 0xFBFCF7, CardLine = 0xE3EAD9, ChipLine = 0xCFDCC4;
const lv_font_t *F14 = &fd_sans_bold_14_ui;
const lv_font_t *F16 = &fd_sans_bold_16_ui;
const lv_font_t *FNum = &fd_mont_26;
// Game screen first: the 10x16 board occupies roughly the left half of the
// screen and the blocks are the largest the 240 px height allows (17x15 cells,
// drawn 15x13 so they stay close to square). The right column is a narrow HUD
// plus five touch keys, no empty decoration.
constexpr int CellW = 16, CellH = 15;
constexpr int BoardX = 2, BoardY = 0, BoardW = Columns * CellW, BoardH = Rows * CellH;
constexpr int PanelX = 166, PanelW = 150;
constexpr int ButtonW = (PanelW - 4) / 2, ButtonH = 38;
constexpr int ButtonCol2 = PanelX + ButtonW + 4;
constexpr int ButtonRow1 = 116, ButtonRow2 = 156, ButtonRow3 = 196;
constexpr int Preview = 48, PreviewX = 260;

Game game;
lv_obj_t *pageRoot = nullptr, *board = nullptr, *preview = nullptr, *overlay = nullptr;
lv_obj_t *scoreLabel = nullptr, *levelLabel = nullptr;
lv_timer_t *timer = nullptr;
int overlayKind = 0;  // 0 = none, 1 = paused, 2 = game over
bool bestLoaded = false;
uint32_t storedBest = 0;

const uint32_t piecePalette[8] = {0xFFFFFF, 0x7FA79B, 0xC9CC8F, 0x5F8F70, 0xA9C6B2, 0xD3B379, 0x8CA0B5, 0xBE8F72};

const char *stateName(State value) {
    switch (value) {
        case State::Ready: return "ready";
        case State::Playing: return "playing";
        case State::Paused: return "paused";
        default: return "gameover";
    }
}

void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius = 10) {
    auto *o = lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0); lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
lv_obj_t *text(lv_obj_t *parent, const String &value, int x, int y, int w, const lv_font_t *font = nullptr,
               uint32_t color = Ink, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    if (!font) font = F14;
    auto *o = lv_label_create(parent); lv_obj_remove_style_all(o); lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, font->line_height + 2);
    lv_obj_set_style_text_font(o, font, 0); lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, align, 0); lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    lv_label_set_text(o, value.c_str()); lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}
void bind(lv_obj_t *o, int command) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, event, LV_EVENT_CLICKED, (void *)(intptr_t)command);
}
lv_obj_t *button(lv_obj_t *parent, const String &value, int x, int y, int w, int h, int command, bool primary = false) {
    auto *o = box(parent, x, y, w, h, primary ? Green : Light, 8); bind(o, command); Ui::attachPressFeedback(o);
    text(o, value, 4, (h - F16->line_height) / 2, w - 8, F16, primary ? White : Ink, LV_TEXT_ALIGN_CENTER);
    return o;
}
// Movement buttons repeat while held so the pad keeps moving without extra taps.
void repeat(lv_obj_t *o, int command) {
    lv_obj_add_event_cb(o, event, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)command);
}
void hit(lv_obj_t *parent, int x, int y, int w, int h, int command) {
    auto *o = box(parent, x, y, w, h, Bg, 0); lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0); bind(o, command);
}
void header() {
    static const lv_point_t points[] = {{7, 0}, {0, 7}, {7, 14}};
    auto *cheveron = lv_line_create(pageRoot); lv_line_set_points(cheveron, points, 3);
    lv_obj_set_pos(cheveron, PanelX + 4, 9); lv_obj_set_style_line_color(cheveron, lv_color_hex(Ink), 0);
    lv_obj_set_style_line_width(cheveron, 3, 0); lv_obj_set_style_line_rounded(cheveron, true, 0);
    hit(pageRoot, PanelX, 0, 24, 32, Back);
    text(pageRoot, "俄罗斯方块", PanelX + 26, 3, 80, F16);
    auto *pause = box(pageRoot, 320 - 6 - 38, 2, 38, 24, Light, 8);
    bind(pause, Pause); Ui::attachPressFeedback(pause);
    lv_obj_set_style_border_width(pause, 1, 0); lv_obj_set_style_border_color(pause, lv_color_hex(ChipLine), 0);
    lv_obj_set_style_border_opa(pause, LV_OPA_COVER, 0);
    text(pause, "暂停", 3, 2, 32, F14, Ink, LV_TEXT_ALIGN_CENTER);
    box(pageRoot, PanelX, 29, PanelW, 1, CardLine, 0);
}
void drawCell(lv_draw_ctx_t *ctx, int x, int y, uint8_t type, bool ghost) {
    // One pixel inset keeps the cells and grid inside the board's rounded border.
    lv_area_t area = {(lv_coord_t)(x + 1), (lv_coord_t)(y + 1), (lv_coord_t)(x + CellW - 2), (lv_coord_t)(y + CellH - 2)};
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 2;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = lv_color_hex(ghost ? GhostFill : piecePalette[type]);
    dsc.border_width = 1;
    dsc.border_opa = ghost ? 140 : 90;
    dsc.border_color = lv_color_hex(piecePalette[type]);
    lv_draw_rect(ctx, &dsc, &area);
}
void drawPiece(lv_draw_ctx_t *ctx, const lv_area_t &origin, uint8_t type, uint8_t rotation, int pieceX, int pieceY, bool ghost) {
    const int n = pieceBox(type);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            if (!shapeCell(type, rotation, r, c)) continue;
            const int bx = pieceX + c, by = pieceY + r;
            if (bx < 0 || bx >= Columns || by < 0 || by >= Rows) continue;
            drawCell(ctx, origin.x1 + bx * CellW, origin.y1 + by * CellH, type, ghost);
        }
}
void drawBoard(lv_event_t *e) {
    auto *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area; lv_obj_get_coords(obj, &area);
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 6; dsc.bg_opa = LV_OPA_COVER; dsc.bg_color = lv_color_hex(White);
    dsc.border_width = 1; dsc.border_opa = LV_OPA_COVER; dsc.border_color = lv_color_hex(BoardBorder);
    lv_draw_rect(ctx, &dsc, &area);
    lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(BoardLine); line.width = 1; line.opa = LV_OPA_COVER;
    for (int col = 1; col < Columns; ++col) {
        lv_point_t p1 = {(lv_coord_t)(area.x1 + 1 + col * CellW), (lv_coord_t)(area.y1 + 1)};
        lv_point_t p2 = {(lv_coord_t)(area.x1 + 1 + col * CellW), (lv_coord_t)(area.y2 - 1)};
        lv_draw_line(ctx, &line, &p1, &p2);
    }
    for (int row = 1; row < Rows; ++row) {
        lv_point_t p1 = {(lv_coord_t)(area.x1 + 1), (lv_coord_t)(area.y1 + 1 + row * CellH)};
        lv_point_t p2 = {(lv_coord_t)(area.x2 - 1), (lv_coord_t)(area.y1 + 1 + row * CellH)};
        lv_draw_line(ctx, &line, &p1, &p2);
    }
    if (game.state == State::Playing || game.state == State::Paused) {
        const int ghost = game.ghostY();
        if (ghost != game.y) drawPiece(ctx, area, game.piece, game.rotation, game.x, ghost, true);
    }
    for (int row = 0; row < Rows; ++row)
        for (int col = 0; col < Columns; ++col)
            if (game.board[row][col]) drawCell(ctx, area.x1 + col * CellW, area.y1 + row * CellH, game.board[row][col], false);
    if (game.state == State::Playing || game.state == State::Paused)
        drawPiece(ctx, area, game.piece, game.rotation, game.x, game.y, false);
}
void drawPreview(lv_event_t *e) {
    auto *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area; lv_obj_get_coords(obj, &area);
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 8; dsc.bg_opa = LV_OPA_COVER; dsc.bg_color = lv_color_hex(Light);
    lv_draw_rect(ctx, &dsc, &area);
    const uint8_t type = game.next;
    if (type < 1 || type > PieceTypes) return;
    int minRow = 4, maxRow = -1, minCol = 4, maxCol = -1;
    const int n = pieceBox(type);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            if (shapeCell(type, 0, r, c)) {
                if (r < minRow) minRow = r; if (r > maxRow) maxRow = r;
                if (c < minCol) minCol = c; if (c > maxCol) maxCol = c;
            }
    if (maxRow < 0) return;
    const int boxW = area.x2 - area.x1 + 1, boxH = area.y2 - area.y1 + 1;
    const int cellW = 12, step = 13;
    const int totalW = (maxCol - minCol + 1) * step - 1, totalH = (maxRow - minRow + 1) * step - 1;
    const int ox = area.x1 + (boxW - totalW) / 2 - minCol * step;
    const int oy = area.y1 + (boxH - totalH) / 2 - minRow * step;
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            if (!shapeCell(type, 0, r, c)) continue;
            lv_area_t cell = {(lv_coord_t)(ox + c * step), (lv_coord_t)(oy + r * step),
                              (lv_coord_t)(ox + c * step + cellW - 1), (lv_coord_t)(oy + r * step + cellW - 1)};
            lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
            d.radius = 2; d.bg_opa = LV_OPA_COVER; d.bg_color = lv_color_hex(piecePalette[type]);
            d.border_width = 1; d.border_opa = 90; d.border_color = lv_color_hex(piecePalette[type]);
            lv_draw_rect(ctx, &d, &cell);
        }
}
void redraw() {
    if (board && lv_obj_is_valid(board)) lv_obj_invalidate(board);
    if (preview && lv_obj_is_valid(preview)) lv_obj_invalidate(preview);
}
void syncLabels() {
    if (!scoreLabel || !lv_obj_is_valid(scoreLabel) || !levelLabel || !lv_obj_is_valid(levelLabel)) return;
    lv_label_set_text_fmt(scoreLabel, "%lu", (unsigned long)game.score);
    lv_label_set_text_fmt(levelLabel, "等级%u 行%u", (unsigned)game.level, (unsigned)game.lines);
}
void loadBest() {
    if (bestLoaded) return;
    Preferences prefs;
    // Open read-write: this also creates a missing namespace on first use, while a
    // read-only begin fails for a missing namespace and would leave bestLoaded
    // false forever (so the very first best score was never persisted).
    if (!prefs.begin("tetris", false)) return;
    if (!prefs.isKey("best")) {
        bestLoaded = true;  // first run: no record to protect yet
    } else {
        // The record is written with putUInt(), so it must be read with getUInt():
        // getBytes() only reads blob-typed entries and returned 0 here, which left
        // the stored best score unreadable. 0xFFFFFFFF is not a reachable score.
        const uint32_t value = prefs.getUInt("best", 0xFFFFFFFFu);
        if (value != 0xFFFFFFFFu) {
            storedBest = value;
            bestLoaded = true;
        }
        // A present-but-unreadable record stays unloaded: persistBest() must never
        // overwrite a best score that could not be read this boot.
    }
    prefs.end();
    if (bestLoaded && storedBest > game.best) game.best = storedBest;
}
void persistBest() {
    loadBest();
    if (!bestLoaded) return;
    if (game.best <= storedBest) return;
    Preferences prefs;
    if (!prefs.begin("tetris", false)) return;
    if (prefs.putUInt("best", game.best) == sizeof(uint32_t)) storedBest = game.best;
    prefs.end();
}
void closeOverlay() {
    if (overlay && lv_obj_is_valid(overlay)) lv_obj_del(overlay);
    overlay = nullptr; overlayKind = 0;
    // A button of the just-deleted overlay may still be tracked by the shared
    // press-feedback animation. Clear the handle after the delete (the shared
    // stop path only compares the pointer for deleted objects).
    Ui::cancelAnimations();
}
void openOverlay(int kind) {
    // The pause chip that opened this overlay may still be inside its 90/140 ms
    // press-feedback animation. Restore the base colour before the scrim is
    // composited so the frozen frame matches the production render.
    Ui::cancelAnimations();
    if (overlay && lv_obj_is_valid(overlay)) lv_obj_del(overlay);
    overlay = lv_obj_create(pageRoot); lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, 0, 0); lv_obj_set_size(overlay, 320, 240);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(Scrim), 0); lv_obj_set_style_bg_opa(overlay, 150, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    const bool over = kind == 2;
    auto *card = box(overlay, 40, over ? 52 : 56, 240, 136, White, 14);
    text(card, over ? "游戏结束" : "已暂停", 16, 14, 208, F16, Ink, LV_TEXT_ALIGN_CENTER);
    text(card, String("得分 ") + String((unsigned long)game.score) + " · 最高 " + String((unsigned long)game.best),
         16, 44, 208, F14, Muted, LV_TEXT_ALIGN_CENTER);
    if (over) {
        button(card, "重新开始", 16, 78, 100, 38, Restart, true);
        button(card, "返回", 124, 78, 100, 38, Back);
    } else {
        button(card, "继续", 16, 78, 60, 38, Resume, true);
        button(card, "重新开始", 80, 78, 76, 38, Restart);
        button(card, "返回", 160, 78, 52, 38, Back);
    }
    overlayKind = kind;
    // A new best must survive the player leaving from the pause or game-over
    // overlay, not only a natural game over.
    persistBest();
}
void syncOverlay() {
    if (game.state == State::Paused && overlayKind != 1) openOverlay(1);
    else if (game.state == State::GameOver && overlayKind != 2) { openOverlay(2); persistBest(); }
    else if (game.state != State::Paused && game.state != State::GameOver && overlayKind) closeOverlay();
}
void apply() { syncLabels(); redraw(); syncOverlay(); }
void event(lv_event_t *e) {
    const int command = (int)(intptr_t)lv_event_get_user_data(e);
    switch (command) {
        case Back: {
            lv_event_t forwarded = *e;
            forwarded.user_data = (void *)(uintptr_t)Ui::Back;
            Ui::dispatchAction(&forwarded);
            break;
        }
        case Pause: game.pause(); apply(); break;
        case Resume: game.resume(millis()); apply(); break;
        case Restart: game.restart(millis()); apply(); break;
        case Left: if (game.moveLeft()) redraw(); break;
        case Right: if (game.moveRight()) redraw(); break;
        case Rotate: if (game.rotate()) redraw(); break;
        case SoftDrop: if (game.softDrop()) apply(); break;
        case HardDrop: game.hardDrop(); apply(); break;
        default: break;
    }
}
void onTick(lv_timer_t *) {
    if (!pageRoot || !lv_obj_is_valid(pageRoot)) return;
    const bool wasOver = game.state == State::GameOver;
    if (game.tick(millis())) { redraw(); syncLabels(); }
    if (!wasOver && game.state == State::GameOver) syncOverlay();
}
}  // namespace

void build(lv_obj_t *parent) {
    loadBest();
    pageRoot = parent; board = preview = overlay = scoreLabel = levelLabel = nullptr; overlayKind = 0;
    lv_obj_set_style_bg_color(pageRoot, lv_color_hex(Bg), 0); lv_obj_set_style_bg_opa(pageRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(pageRoot, LV_OBJ_FLAG_SCROLLABLE);
    header();
    board = lv_obj_create(pageRoot); lv_obj_remove_style_all(board);
    lv_obj_set_pos(board, BoardX, BoardY); lv_obj_set_size(board, BoardW, BoardH);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board, drawBoard, LV_EVENT_DRAW_MAIN, nullptr);
    box(pageRoot, PanelX, 34, 84, 76, Card, 10);
    text(pageRoot, "得分", PanelX + 6, 39, 50, F14, Muted);
    scoreLabel = text(pageRoot, "", PanelX + 6, 54, 72, FNum, Green);
    levelLabel = text(pageRoot, "", PanelX + 6, 87, 72, F14, Muted);
    box(pageRoot, PreviewX - 2, 34, 60, 76, Card, 10);
    text(pageRoot, "下一个", PreviewX + 2, 39, 52, F14, Muted);
    preview = lv_obj_create(pageRoot); lv_obj_remove_style_all(preview);
    lv_obj_set_pos(preview, PreviewX + 6, 52); lv_obj_set_size(preview, Preview, Preview);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(preview, drawPreview, LV_EVENT_DRAW_MAIN, nullptr);
    repeat(button(pageRoot, "左移", PanelX, ButtonRow1, ButtonW, ButtonH, Left), Left);
    repeat(button(pageRoot, "右移", ButtonCol2, ButtonRow1, ButtonW, ButtonH, Right), Right);
    button(pageRoot, "旋转", PanelX, ButtonRow2, ButtonW, ButtonH, Rotate);
    repeat(button(pageRoot, "下移", ButtonCol2, ButtonRow2, ButtonW, ButtonH, SoftDrop), SoftDrop);
    button(pageRoot, "落底", PanelX, ButtonRow3, PanelW, ButtonH, HardDrop, true);
    if (timer) { lv_timer_del(timer); timer = nullptr; }
    timer = lv_timer_create(onTick, 20, nullptr);
    if (game.state == State::Ready) game.start(millis(), millis() * 2654435761u + 0x9E3779B9u);
    apply();
}

void refresh(bool force) {
    if (!pageRoot || !lv_obj_is_valid(pageRoot)) return;
    syncLabels();
    if (game.state == State::Paused || game.state == State::GameOver) syncOverlay();
    else if (overlayKind) closeOverlay();
    if (force) redraw();
}

void cancel() {
    if (timer) { lv_timer_del(timer); timer = nullptr; }
    // This function runs both when the app is left and when the same page is
    // rebuilt for a background refresh (e.g. a pomodoro toast re-render). Only a
    // real app switch pauses; a same-app rebuild keeps the game running and just
    // recreates the gravity timer in build().
    persistBest();
    if (game.state == State::Playing && app != App::Tetris) game.pause();
    pageRoot = board = preview = overlay = scoreLabel = levelLabel = nullptr;
    overlayKind = 0;
}

bool back() { return false; }
bool canBack() { return false; }

Status status() {
    Status value;
    value.state = stateName(game.state);
    value.score = game.score; value.best = game.best;
    value.lines = game.lines; value.level = game.level;
    return value;
}

uint32_t bestScore() {
    loadBest();
    return game.best > storedBest ? game.best : storedBest;
}

// Host-renderer hook only (never called by device code): freezes a deterministic
// screen scenario so the native review runner can capture it. 0 = fresh game.
void debugVariant(int which) {
    game = Game{};
    loadBest();
    if (which <= 0) { game.start(1000, 20261101u); apply(); return; }
    // A plausible mid-game stack: bottom rows filled, a T piece falling and an I
    // piece queued. Public fields only, mirroring focus engine fixtures.
    static const uint8_t stack[Rows][Columns] = {
        {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0},
        {3, 3, 3, 0, 0, 0, 4, 4, 0, 0},
        {2, 2, 0, 0, 0, 0, 0, 0, 0, 0},
        {2, 2, 5, 5, 0, 0, 0, 0, 0, 0},
        {6, 6, 7, 5, 5, 0, 3, 0, 0, 0},
    };
    memcpy(game.board, stack, sizeof(stack));
    game.piece = (uint8_t)Piece::T; game.rotation = 0;
    game.x = 4; game.y = 4;
    game.next = (uint8_t)Piece::I;
    game.score = 1240; game.lines = 7; game.level = 1;
    game.best = 2860; game.state = State::Playing;
    if (which == 2) game.state = State::Paused;
    if (which == 3) { game.state = State::GameOver; game.score = 1240; }
    apply();
}

}  // namespace TetrisPage
