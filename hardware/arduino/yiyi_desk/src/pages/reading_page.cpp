// Page 3 - 名言 / 古诗页 + 阅读详情页. Hand-written LVGL page.
//
// Layout is taken from the locked reading-v1 design and from the exact numbers
// used to generate its mock-ups (build/design/reading/make_reading_design.py and
// make_quotes_buttons.py).  Interaction / hard rules follow UI实现规格.md §6:
//   * main page: white quote card (6,5) 309x131 r14 with feather, centred date
//     (Bold20 #A6A6A6), circular icon refresh buttons (24x24, 32x32 hit area),
//     auto-shrinking body 16/14/12 with at most 4 lines, centred author, and the
//     red poem card (4,141) 311x90 r13 with two white body lines and the author
//     bottom-right.
//   * reading detail: green 32px header with a drawn chevron and the shared
//     Ui::statusCluster(), a white 320x208 body panel with a 320x176 scroll
//     viewport that stops above the fixed bottom-right author, and a 2px custom
//     scroll bar whose thumb tracks the viewport.
//   * refresh buttons call GiftPages::fire(RefreshQuote/RefreshPoem) and the old
//     content stays visible while the request runs.
#include "page_api.h"
#include "../ui_widgets.h"
#include "../saved_design/saved_resources.h"
#include "../gift_assets/gift_fonts.h"
#include "../gift_data.h"
#include "../app_state.h"
#include "../touch.h"
#include "../clock_source.h"
#include "../chinese_layout.h"
#include "../../desk_logic.h"
#include "assets/reading_refresh_icons.h"
#include <Arduino.h>
#include <stdio.h>

namespace ReadingPage {
namespace {

// ---- palette (locked reading-v1) -----------------------------------------
constexpr uint32_t BgColor = 0xA4CCAC;         // page green
constexpr uint32_t CardColor = 0xFFFFFF;       // quote card
constexpr uint32_t DateColor = 0xA6A6A6;       // date grey
constexpr uint32_t InkColor = 0x292929;        // body / author
constexpr uint32_t PoemColor = 0xC6322A;       // red poem card
constexpr uint32_t HeaderInk = 0x3F5B46;       // detail header ink
constexpr uint32_t ScrollTrack = 0xE8EEE6;     // detail scroll track
constexpr uint32_t ScrollKnob = 0xB4C6B2;      // detail scroll thumb
constexpr uint32_t ButtonCircle = 0xF1F2F1;    // quote refresh circle
constexpr uint32_t ButtonCircleBusy = 0xE6E6E6;
constexpr uint32_t ButtonRing = 0xE8C4BE;      // poem refresh outline

// ---- fonts -----------------------------------------------------------------
// All faces are NotoSansSC-Bold.  The sizes are the ones the layout was designed
// with; the smaller ones mirror the old saved-design reading page.
const lv_font_t *const FontDate = saved_gift_font_bold_20_0;
const lv_font_t *const FontBody16 = saved_gift_yahei_b_16_1;
const lv_font_t *const FontBody14 = saved_gift_font_bold_14_0;
const lv_font_t *const FontBody12 = &fd_sans_bold_12;
const lv_font_t *const FontAuthor12 = &fd_sans_bold_12;
const lv_font_t *const FontAuthor14 = saved_gift_font_bold_14_0;
const lv_font_t *const FontTitle = saved_gift_font_bold_14_0;

// ---- geometry --------------------------------------------------------------
constexpr int ScreenWidth = 320;
constexpr int ScreenHeight = 240;
constexpr int DetailBarHeight = 32;
constexpr int DetailPanelHeight = ScreenHeight - DetailBarHeight;  // 208
// The fixed author label keeps the locked design's parent y=208 (see the mock in
// build/design/reading/make_reading_design.py), i.e. panel-relative 176.  The body
// scroll viewport ends there so text can never slide under the author line; the
// remaining 32 px of the white panel are the author row.
constexpr int DetailViewportHeight = DetailPanelHeight - 32;  // 176

// Which view the module shows: 0 = quotes page, 1 = quote detail, 2 = poem detail.
int view = 0;
// True while the next build comes from a module-internal view switch; forcedView
// marks a host-renderer debugVariant selection.  Any other rebuild (page switch,
// idle return home) is external and must fall back to the quotes page.
bool internalNav = false;
bool forcedView = false;

#ifdef GIFT_HOST_TEST
// The host GiftData store is empty, so its poem is only two lines long.  The
// off-screen renderer's poem-detail frame injects a longer poem (8 couplets) so
// the scroll viewport / fixed author row can be checked for overlap (B2/B5);
// device code never compiles this.
bool hostLongPoem = false;
const char *const HostPoemSample =
    "极浦无高树，苍茫只远空。\n潮来江水黑，日出海门红。\n"
    "两岸东西浙，千帆来去风。\n中原山色外，残梦逐归鸿。\n"
    "孤帆远影碧空尽，唯见长江天际流。\n晴川历历汉阳树，芳草萋萋鹦鹉洲。\n"
    "日暮乡关何处是，烟波江上使人愁。\n山重水复疑无路，柳暗花明又一村。";
#endif

// Main page caches.
lv_obj_t *quoteDate = nullptr;
lv_obj_t *quoteBody = nullptr;
lv_obj_t *quoteAuthor = nullptr;
lv_obj_t *poemBody = nullptr;
lv_obj_t *poemAuthor = nullptr;
lv_obj_t *quoteButton = nullptr;   // 32x32 hit area
lv_obj_t *poemButton = nullptr;
lv_obj_t *quoteCircle = nullptr;   // 22x22 coloured disc
lv_obj_t *poemCircle = nullptr;
lv_obj_t *quoteIcon = nullptr;     // 24x24 rotating arrow
lv_obj_t *poemIcon = nullptr;

// Detail page caches.
lv_obj_t *detailPanel = nullptr;     // full-height white background
lv_obj_t *detailViewport = nullptr;  // transparent scroll viewport above the author row
lv_obj_t *detailBody = nullptr;
lv_obj_t *detailAuthor = nullptr;
lv_obj_t *detailEmpty = nullptr;
lv_obj_t *scrollBarTrack = nullptr;
lv_obj_t *scrollBarKnob = nullptr;
int detailContentHeight = 0;

// Refresh feedback: the buttons fade and their arrow rotates while the
// background quote/poem job is in flight.  `readingPending` is the desk job
// pipeline's real pending flag (app_state.h), so the spinner stops exactly when
// the request lands; a 15 s safety timeout covers a job that never reports.
bool quoteBusy = false;
bool poemBusy = false;
uint32_t quoteBusyStart = 0;
uint32_t poemBusyStart = 0;
uint32_t seenRevision = 0;

bool built = false;

// ---- small widget helpers --------------------------------------------------
void setText(lv_obj_t *object, const String &value) {
    if (!object) return;
    if (String(lv_label_get_text(object)) == value) return;
    lv_label_set_text(object, value.c_str());
}

lv_obj_t *makePanel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius) {
    auto *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    if (color) {
        lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

lv_obj_t *makeLabel(lv_obj_t *parent, int x, int y, int w, const lv_font_t *font,
                    uint32_t color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    auto *object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, w);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(object, align, 0);
    lv_obj_set_style_text_line_space(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    return object;
}

lv_obj_t *makeHitArea(lv_obj_t *parent, int x, int y, int w, int h) {
    auto *object = makePanel(parent, x, y, w, h, 0, 0);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

// The host Arduino String shim only implements the subset the firmware uses, so
// keep to it (no endsWith / remove / setCharAt).
bool endsWith(const String &text, const char *suffix) {
    const size_t length = strlen(suffix);
    return text.length() >= length && strcmp(text.c_str() + text.length() - length, suffix) == 0;
}

// ---- text helpers ----------------------------------------------------------
String cleanText(const String &value) {
    String result = value;
    result.replace("\r", "");
    result.trim();
    return result;
}

String readingValue(const char *kind, const char *field) {
    auto options = GiftData::get("reading");
    auto cache = GiftData::get("readingCache")[kind];
    String key = String(kind) + field;
    bool manual = options[String(kind) + "Mode"] == "manual";
    char lowered[2];
    lowered[0] = (char)tolower((unsigned char)field[0]);
    lowered[1] = '\0';
    String cacheKey = String(lowered) + String(field + 1);
    if (manual || cache.isNull()) return String(options[key] | "");
    return String(cache[cacheKey.c_str()] | "");
}

String quoteText() { return cleanText(readingValue("quote", "Text")); }
String quoteAuthorText() { return cleanText(readingValue("quote", "Author")); }
String poemText() { return cleanText(readingValue("poem", "Text")); }

String poemAuthorText() {
    String dynasty = cleanText(readingValue("poem", "Dynasty"));
    String author = cleanText(readingValue("poem", "Author"));
    if (author.isEmpty()) return String();
    if (dynasty.isEmpty()) return author;
    if (endsWith(dynasty, "代")) return dynasty + " · " + author;
    return dynasty + "代 · " + author;
}

int lineCount(const String &text) {
    int lines = 1;
    for (uint32_t i = 0; i < (uint32_t)text.length(); ++i)
        if (text[i] == '\n') ++lines;
    return lines;
}

// First `wanted` visual lines of a '\n'-separated block, plus whether more exist.
void firstLines(const String &text, int wanted, String *out, bool *more) {
    int line = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i <= (uint32_t)text.length(); ++i) {
        if (i != (uint32_t)text.length() && text[i] != '\n') continue;
        if (line < wanted) out[line] = text.substring(start, i);
        ++line;
        start = i + 1;
        if (line > wanted) break;
    }
    *more = line > wanted;
}

// Rebuild `text` with line breaks after the given CJK punctuation.  Existing
// newlines are dropped; punctuation stays at the end of its line.
String breakAfterPunctuation(const String &text, bool breakAtComma) {
    String out;
    out.reserve(text.length() + text.length() / 8);
    const char *raw = text.c_str();
    uint32_t index = 0;
    while (index < (uint32_t)text.length()) {
        uint32_t start = index;
        uint32_t codepoint = _lv_txt_encoded_next(raw, &index);
        if (codepoint == '\n' || codepoint == '\r') continue;
        out.concat(raw + start, index - start);
        if (codepoint == 0x3002 || codepoint == 0xFF01 || codepoint == 0xFF1F) out += '\n';  // 。！？
        else if (breakAtComma && (codepoint == 0xFF0C || codepoint == 0x3001)) out += '\n';  // ，、
    }
    while (out.length() && out[out.length() - 1] == '\n') out = out.substring(0, out.length() - 1);
    return out;
}

// Truncate one line with an ellipsis so it fits `maxWidth` in `font`.
String ellipsize(const String &text, const lv_font_t *font, int maxWidth, bool force = false) {
    if (text.isEmpty()) return text;
    const char *dots = "…";
    const int dotsWidth = lv_txt_get_width(dots, strlen(dots), font, 0, LV_TEXT_FLAG_NONE);
    const int width = lv_txt_get_width(text.c_str(), text.length(), font, 0, LV_TEXT_FLAG_NONE);
    if (!force && width <= maxWidth) return text;
    String out;
    int used = 0;
    const int limit = maxWidth > dotsWidth ? maxWidth - dotsWidth : 0;
    uint32_t index = 0;
    while (index < (uint32_t)text.length()) {
        uint32_t start = index;
        _lv_txt_encoded_next(text.c_str(), &index);
        const int glyph = lv_txt_get_width(text.c_str() + start, index - start, font, 0, LV_TEXT_FLAG_NONE);
        if (used + glyph > limit) break;
        used += glyph;
        out.concat(text.c_str() + start, index - start);
    }
    out += dots;
    return out;
}

// ---- main page: quote body auto-fit ---------------------------------------
struct FittedBody {
    const lv_font_t *font;
    String text;
    int lineSpace;
    int lines;
    int blockHeight;
};

constexpr int QuoteBodyWidth = 271;

// Lines at 16px decide the tier: <=2 -> Bold16, 3 -> Bold14, >=4 -> Bold12 with
// at most four visible lines (LVGL's DOT mode adds the ellipsis on overflow).
// For >=4 lines at 16 px the 14 px wrap is tried first: texts of roughly 46..57
// CJK characters need 4 lines at 16 px but only 3 at 14 px and belong in the
// Bold14/3-line tier.
FittedBody fitQuoteBody(const String &raw) {
    const String wrapped16 = ChineseLayout::wrap(raw, FontBody16, QuoteBodyWidth);
    const int count16 = lineCount(wrapped16);
    FittedBody fit = {FontBody16, wrapped16, 11, count16 > 2 ? 2 : count16, 0};
    int maxLines = 2;
    if (count16 == 3) {
        fit.font = FontBody14;
        fit.text = ChineseLayout::wrap(raw, FontBody14, QuoteBodyWidth);
        fit.lineSpace = 6;  // spec: 3 lines -> Bold14 line space 6
        maxLines = 3;
    } else if (count16 >= 4) {
        const String wrapped14 = ChineseLayout::wrap(raw, FontBody14, QuoteBodyWidth);
        if (lineCount(wrapped14) <= 3) {
            fit.font = FontBody14;
            fit.text = wrapped14;
            fit.lineSpace = 6;  // spec: 3 lines -> Bold14 line space 6
            maxLines = 3;
        } else {
            fit.font = FontBody12;
            fit.text = ChineseLayout::wrap(raw, FontBody12, QuoteBodyWidth);
            fit.lineSpace = 3;  // mock line advance 18px - Noto 12 line height 15px
            maxLines = 4;
        }
    }
    int lines = lineCount(fit.text);
    if (lines > maxLines) lines = maxLines;
    fit.lines = lines;
    fit.blockHeight = lines * fit.font->line_height + (lines - 1) * fit.lineSpace;
    return fit;
}

void buildQuoteBody(lv_obj_t *card) {
    const String raw = quoteText();
    // The text block is vertically centred on the card band y=42..104; the mock
    // positions it as 73 - lines * lineAdvance / 2 (in card-relative terms that
    // is 68 - lines * (line_height + line_space) / 2).
    constexpr int CenterY = 68;
    constexpr int LeftX = 15;  // absolute x=21
    if (raw.isEmpty()) {
        quoteBody = makeLabel(card, LeftX, CenterY - 8, QuoteBodyWidth, FontBody16, InkColor,
                              LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(quoteBody, "暂无内容");
        return;
    }
    const FittedBody fit = fitQuoteBody(raw);
    const int advance = fit.font->line_height + fit.lineSpace;
    const int top = CenterY - fit.lines * advance / 2;
    quoteBody = makeLabel(card, LeftX, top, QuoteBodyWidth, fit.font, InkColor, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(quoteBody, LV_LABEL_LONG_DOT);
    lv_obj_set_height(quoteBody, fit.blockHeight);
    lv_obj_set_style_text_line_space(quoteBody, fit.lineSpace, 0);
    lv_label_set_text(quoteBody, fit.text.c_str());
}

// ---- main page: poem body --------------------------------------------------
String poemCardBody() {
    const String raw = poemText();
    if (raw.isEmpty()) return String();
    const String broken = breakAfterPunctuation(raw, true);
    String lines[3];
    bool more = false;
    firstLines(broken, 2, lines, &more);
    lines[0] = ellipsize(lines[0], FontBody16, 273);
    if (lines[1].isEmpty()) return lines[0];
    lines[1] = ellipsize(lines[1], FontBody16, 273, more);
    return lines[0] + "\n" + lines[1];
}

void buildPoemCard(lv_obj_t *card) {
    String body = poemCardBody();
    poemBody = makeLabel(card, 21, 22, 273, FontBody16, 0xFFFFFF, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(poemBody, LV_LABEL_LONG_CLIP);
    lv_obj_set_height(poemBody, 2 * FontBody16->line_height + 4);
    lv_obj_set_style_text_line_space(poemBody, 4, 0);  // mock: lines at y163 and y186
    lv_label_set_text(poemBody, body.isEmpty() ? "暂无内容" : body.c_str());
    const String author = poemAuthorText();
    if (!author.isEmpty()) {
        poemAuthor = makeLabel(card, 94, 64, 200, FontAuthor14, 0xFFFFFF, LV_TEXT_ALIGN_RIGHT);
        lv_label_set_text(poemAuthor, author.c_str());
    }
}

// ---- refresh buttons -------------------------------------------------------
void spinExec(void *var, int32_t value) {
    if (var) lv_img_set_angle(static_cast<lv_obj_t *>(var), (int16_t)value);
}

void startSpin(lv_obj_t *icon) {
    if (!icon) return;
    lv_anim_del(icon, nullptr);
    lv_img_set_angle(icon, 0);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, icon);
    lv_anim_set_values(&anim, 0, 3600);
    lv_anim_set_time(&anim, 900);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_exec_cb(&anim, spinExec);
    lv_anim_start(&anim);
}

void setBusy(bool quote, bool busy) {
    lv_obj_t *circle = quote ? quoteCircle : poemCircle;
    lv_obj_t *icon = quote ? quoteIcon : poemIcon;
    if (quote) quoteBusy = busy;
    else poemBusy = busy;
    if (circle) {
        uint32_t color = busy ? (quote ? ButtonCircleBusy : 0xFFFFFF) : (quote ? ButtonCircle : 0xFFFFFF);
        lv_obj_set_style_bg_color(circle, lv_color_hex(color), 0);
    }
    if (icon) {
        lv_obj_set_style_img_opa(icon, busy ? (quote ? 110 : 130) : 255, 0);
        if (busy) startSpin(icon);
        else {
            lv_anim_del(icon, nullptr);
            lv_img_set_angle(icon, 0);
        }
    }
}

// Returns true when a fetch will actually run (manual text needs no network).
bool refreshAllowed(const char *kind) {
    String mode = String(kind) + "Mode";
    return String(GiftData::get("reading")[mode.c_str()] | "api") == "api";
}

void refreshClicked(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (Touch::swiped()) return;  // a page-swipe must never also refresh
    const bool quote = (uintptr_t)lv_event_get_user_data(event) == 0;
    if (quote ? quoteBusy : poemBusy) return;  // de-duplicate repeated taps
    lastInputAt = millis();
    if (!refreshAllowed(quote ? "quote" : "poem")) return;
    if (readingPending) return;  // the job pipeline accepts one reading fetch at a time
    GiftPages::fire(quote ? GiftPages::RefreshQuote : GiftPages::RefreshPoem);
    // requestReading() sets the pending flag synchronously when a job is queued.
    if (!readingPending) return;
    if (quote) {
        quoteBusyStart = millis();
        setBusy(true, true);
    } else {
        poemBusyStart = millis();
        setBusy(false, true);
    }
}

void buildRefreshButton(lv_obj_t *parent, bool quote) {
    const int x = quote ? 276 : 279;  // 32x32 hit area
    const int y = quote ? 3 : 139;
    auto *button = makeHitArea(parent, x, y, 32, 32);
    if (quote) quoteButton = button;
    else poemButton = button;
    auto *circle = makePanel(button, 5, 5, 22, 22, quote ? ButtonCircle : 0xFFFFFF, LV_RADIUS_CIRCLE);
    if (!quote) {
        lv_obj_set_style_border_width(circle, 1, 0);
        lv_obj_set_style_border_color(circle, lv_color_hex(ButtonRing), 0);
    }
    if (quote) quoteCircle = circle;
    else poemCircle = circle;
    auto *icon = lv_img_create(button);
    lv_img_set_src(icon, quote ? &reading_refresh_dark : &reading_refresh_red);
    lv_obj_set_pos(icon, 4, 4);
    lv_img_set_pivot(icon, 12, 12);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    if (quote) quoteIcon = icon;
    else poemIcon = icon;
    lv_obj_add_event_cb(button, refreshClicked, LV_EVENT_CLICKED, (void *)(uintptr_t)(quote ? 0 : 1));
    // A rebuilt page while a request is still running must keep the spinner.
    const bool busy = quote ? quoteBusy : poemBusy;
    const uint32_t started = quote ? quoteBusyStart : poemBusyStart;
    if (busy && readingPending && millis() - started < 15000) setBusy(quote, true);
    else if (busy) setBusy(quote, false);
}

// ---- main page -------------------------------------------------------------
void openDetail(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (Touch::swiped()) return;  // a page-swipe must never open the detail view
    internalNav = true;
    view = (int)(uintptr_t)lv_event_get_user_data(event);
    lastInputAt = millis();
    renderPending = true;
}

String dateText() {
    const int64_t now = ClockSource::now();
    if (!DeskLogic::timeValid(now)) return String("--/--");
    const auto date = DeskLogic::beijingDate(now);
    char text[8];
    snprintf(text, sizeof(text), "%02d/%02d", (int)date.month, (int)date.day);
    return String(text);
}

void buildMain(lv_obj_t *parent) {
    // ---- white quote card --------------------------------------------------
    auto *quoteCard = makePanel(parent, 6, 5, 309, 131, CardColor, 14);
    // Feather: 24x24 at (18,10); the asset is 15x15, so zoom 24/15.
    auto *feather = lv_img_create(quoteCard);
    lv_img_set_src(feather, saved_gift_quote_feather);
    lv_obj_set_pos(feather, 12, 5);  // absolute (18,10)
    lv_img_set_pivot(feather, 0, 0);
    lv_img_set_zoom(feather, 410);
    lv_obj_clear_flag(feather, LV_OBJ_FLAG_CLICKABLE);
    // Date: centred on the card (mock ink starts at y≈11 -> label y=8).
    quoteDate = makeLabel(quoteCard, 0, 3, 309, FontDate, DateColor, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(quoteDate, dateText().c_str());
    buildQuoteBody(quoteCard);
    const String author = quoteAuthorText();
    if (!author.isEmpty()) {
        quoteAuthor = makeLabel(quoteCard, 0, 107, 309, FontAuthor12, InkColor, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(quoteAuthor, author.c_str());
    }
    // Whole card opens the quote detail.
    auto *quoteHit = makeHitArea(parent, 6, 5, 309, 131);
    lv_obj_add_event_cb(quoteHit, openDetail, LV_EVENT_CLICKED, (void *)(uintptr_t)1);
    buildRefreshButton(parent, true);

    // ---- red poem card -----------------------------------------------------
    auto *poemCard = makePanel(parent, 4, 141, 311, 90, PoemColor, 13);
    buildPoemCard(poemCard);
    auto *poemHit = makeHitArea(parent, 4, 141, 311, 90);
    lv_obj_add_event_cb(poemHit, openDetail, LV_EVENT_CLICKED, (void *)(uintptr_t)2);
    buildRefreshButton(parent, false);
}

// ---- detail page -----------------------------------------------------------
void backClicked(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (Touch::swiped()) return;  // a page-swipe must never also fire the back button
    lastInputAt = millis();
    if (view != 0) {
        internalNav = true;
        view = 0;
        renderPending = true;
    }
}

// Poem body for the detail page: keep the source line structure (the API already
// returns one couplet per line); a manual/cached text without line breaks gets
// one after each full couplet so lines never split a couplet in half.
String detailPoemBody() {
    String raw = poemText();
    if (raw.isEmpty()) return raw;
    if (raw.indexOf('\n') < 0) return breakAfterPunctuation(raw, false);
    return raw;
}

String detailQuoteBody() {
    String raw = quoteText();
    if (raw.isEmpty()) return raw;
    return ChineseLayout::wrap(raw, FontBody16, 288);
}

void updateScrollBar() {
    if (!scrollBarTrack || !scrollBarKnob || !detailViewport) return;
    constexpr int trackTop = 44, trackHeight = 153;
    const int travel = detailContentHeight - DetailViewportHeight;
    if (travel <= 8) {
        lv_obj_add_flag(scrollBarTrack, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scrollBarKnob, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(scrollBarTrack, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(scrollBarKnob, LV_OBJ_FLAG_HIDDEN);
    int knobHeight = trackHeight * DetailViewportHeight / detailContentHeight;
    if (knobHeight < 24) knobHeight = 24;
    if (knobHeight > trackHeight) knobHeight = trackHeight;
    int offset = lv_obj_get_scroll_y(detailViewport);
    if (offset < 0) offset = 0;
    if (offset > travel) offset = travel;
    const int maxY = trackHeight - knobHeight;
    const int knobY = trackTop + (maxY > 0 ? offset * maxY / travel : 0);
    lv_obj_set_height(scrollBarKnob, knobHeight);
    lv_obj_set_y(scrollBarKnob, knobY);
}

// Keep the custom thumb in sync while the finger drags the body, not only on the
// 1 Hz refresh tick.  Only the thumb geometry/visibility is touched here.
void scrollChanged(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_SCROLL) return;
    updateScrollBar();
}

void buildDetail(lv_obj_t *parent, int kind) {
    const bool poem = kind == 2;
    // Green header with a drawn chevron (the old font arrow was clipped).
    makePanel(parent, 0, 0, ScreenWidth, DetailBarHeight, BgColor, 0);
    static const lv_point_t chevronPoints[] = {{17, 11}, {10, 18}, {17, 25}};
    auto *chevron = lv_line_create(parent);
    lv_line_set_points(chevron, chevronPoints, 3);
    lv_obj_set_pos(chevron, 0, 0);
    lv_obj_set_style_line_width(chevron, 3, 0);
    lv_obj_set_style_line_color(chevron, lv_color_hex(HeaderInk), 0);
    lv_obj_set_style_line_rounded(chevron, true, 0);
    lv_obj_clear_flag(chevron, LV_OBJ_FLAG_CLICKABLE);
    auto *title = makeLabel(parent, 54, 9, 200, FontTitle, HeaderInk, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(title, poem ? "诗词全文" : "每日名言");
    // Shared status cluster: same geometry as every other titled page.
    Ui::statusCluster(false);
    // Back hit area (the design has no text button).
    auto *backHit = makeHitArea(parent, 0, 0, 48, DetailBarHeight);
    lv_obj_add_event_cb(backHit, backClicked, LV_EVENT_CLICKED, nullptr);

    // White body panel: full-height background only (0..DetailPanelHeight).  The
    // scroll viewport is a separate transparent child so the fixed author row at
    // the bottom of the panel is never scrolled over.
    detailPanel = makePanel(parent, 0, DetailBarHeight, ScreenWidth, DetailPanelHeight, CardColor, 0);
    const String bodyBase = poem ? detailPoemBody() : detailQuoteBody();
    const String authorBase = poem ? poemAuthorText() : quoteAuthorText();
    String body = bodyBase;
    String author = authorBase;
#ifdef GIFT_HOST_TEST
    if (poem && hostLongPoem) {
        body = String(HostPoemSample);
        author = "唐代 · 杨万里";
    }
#endif
    if (body.isEmpty()) {
        detailEmpty = makeLabel(parent, 0, DetailBarHeight + (DetailPanelHeight - 20) / 2, ScreenWidth,
                               FontBody14, InkColor, LV_TEXT_ALIGN_CENTER);
        lv_label_set_text(detailEmpty, "暂无内容");
    } else {
        // Scroll viewport: same width as the panel, ending above the author row.
        detailViewport = makePanel(detailPanel, 0, 0, ScreenWidth, DetailViewportHeight, 0, 0);
        lv_obj_add_flag(detailViewport, LV_OBJ_FLAG_SCROLLABLE);
        // A scrollable object must stay clickable or LVGL's input search never
        // finds it (labels are not clickable), which made long detail text
        // impossible to scroll by touch.
        lv_obj_add_flag(detailViewport, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_scroll_dir(detailViewport, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(detailViewport, LV_SCROLLBAR_MODE_OFF);
        detailBody = makeLabel(detailViewport, 16, 16, 288, FontBody16, InkColor, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(detailBody, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(detailBody, LV_SIZE_CONTENT);
        lv_obj_set_style_text_line_space(detailBody, 11, 0);  // mock line advance 30px
        lv_label_set_text(detailBody, body.c_str());
        lv_obj_update_layout(detailViewport);
        detailContentHeight = 16 + lv_obj_get_height(detailBody);  // real scroll extent
        lv_obj_add_event_cb(detailViewport, scrollChanged, LV_EVENT_SCROLL, nullptr);
    }
    // Fixed bottom-right author (hidden when the source has none).
    if (!author.isEmpty()) {
        detailAuthor = makeLabel(parent, 104, 208, 200, FontAuthor14, InkColor, LV_TEXT_ALIGN_RIGHT);
        lv_label_set_text(detailAuthor, author.c_str());
    }
    // Custom thin scroll bar (right edge, only for scrollable content).  Its
    // track geometry follows the locked mock (x=313, y=44..196) and is unchanged;
    // the thumb size/travel below are computed from the shorter viewport.
    if (detailContentHeight > DetailViewportHeight) {
        scrollBarTrack = makePanel(parent, 313, 44, 2, 153, ScrollTrack, 1);
        scrollBarKnob = makePanel(parent, 313, 44, 2, 24, ScrollKnob, 1);
        updateScrollBar();
    }
}

}  // namespace

// ---- public API ------------------------------------------------------------
void build(lv_obj_t *parent) {
    // Rebuild every cache; the target view survives a rebuild (detail pages and
    // the host renderer's debugVariant both rely on that).
    quoteDate = quoteBody = quoteAuthor = nullptr;
    poemBody = poemAuthor = nullptr;
    quoteButton = poemButton = quoteCircle = poemCircle = nullptr;
    quoteIcon = poemIcon = nullptr;
    detailPanel = detailViewport = detailBody = detailAuthor = detailEmpty = nullptr;
    scrollBarTrack = scrollBarKnob = nullptr;
    detailContentHeight = 0;
    built = false;

    // Only a module-internal view switch (openDetail/backClicked) or a host
    // debugVariant selection keeps a non-default view across this rebuild; an
    // external rebuild (page switch, idle return home) shows the quotes page.
    if (!internalNav && !forcedView) view = 0;
    internalNav = forcedView = false;

    lv_obj_set_style_bg_color(parent, lv_color_hex(BgColor), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    seenRevision = GiftData::revision();
    if (view == 0) buildMain(parent);
    else buildDetail(parent, view);
    built = true;
}

void refresh(bool force) {
    (void)force;
    if (!built) return;
    const uint32_t now = millis();
    if (view == 0) {
        // Date + spinner bookkeeping.  Data changes rebuild the page (font tier
        // and vertical centring depend on the content), which never happens at
        // more than the daily/manual refresh cadence.
        if (quoteDate) setText(quoteDate, dateText());
        if (quoteBusy && (!readingPending || now - quoteBusyStart > 15000)) setBusy(true, false);
        if (poemBusy && (!readingPending || now - poemBusyStart > 15000)) setBusy(false, false);
        const uint32_t revision = GiftData::revision();
        if (revision != seenRevision) {
            seenRevision = revision;
            renderPending = true;
        }
    } else {
        updateScrollBar();
    }
}

bool ready() {
    if (!built) return false;
    if (view == 0) return quoteDate != nullptr && quoteBody != nullptr;
    return detailPanel != nullptr &&
           ((detailBody != nullptr && detailViewport != nullptr) || detailEmpty != nullptr);
}

void cancel() {
    if (quoteIcon) lv_anim_del(quoteIcon, nullptr);
    if (poemIcon) lv_anim_del(poemIcon, nullptr);
    quoteIcon = poemIcon = nullptr;
    quoteDate = quoteBody = quoteAuthor = nullptr;
    poemBody = poemAuthor = nullptr;
    quoteButton = poemButton = quoteCircle = poemCircle = nullptr;
    detailPanel = detailViewport = detailBody = detailAuthor = detailEmpty = nullptr;
    scrollBarTrack = scrollBarKnob = nullptr;
    detailContentHeight = 0;
    built = false;
}

bool back() {
    if (view == 0) return false;
    view = 0;
    lastInputAt = millis();
    renderPending = true;
    return true;
}

// Pure query used by the swipe router before the gesture is executed; it must never
// change the view (back() above performs the transition).
bool canBack() { return view != 0; }

void debugVariant(int which) {
    // Host renderer hook: 0 main page, 1 quote detail, 2 poem detail.
    if (which < 0 || which > 2) which = 0;
    forcedView = true;
    view = which;
#ifdef GIFT_HOST_TEST
    hostLongPoem = which == 2;
#endif
}

}  // namespace ReadingPage
