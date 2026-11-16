#include "days_page.h"
#include "days_logic.h"
#include "page_api.h"
#include "../app_state.h"
#include "../gift_data.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <algorithm>

namespace DaysPage {
namespace {
constexpr uint32_t Paper = 0xFAF7EF, Ink = 0x304C40, Muted = 0x7A897B, Sage = 0x52745D;
enum class Style { Classic, Laurel, Ink, Heart, Dots };
struct Palette { uint32_t top, bottom, ink, muted, accent, page; bool dark; };
constexpr Palette softColors[] = {
    {0xDCEFFB,0xF6FBFF,0x294F6A,0x6A879A,0x75A2BA,0xF6F9FC,false},
    {0xDCEEE5,0xF5FBF7,0x355E52,0x718E82,0x80A895,0xF6FAF7,false},
    {0xF4E9D3,0xFFFCF3,0x786044,0x99866A,0xBEA16C,0xFCFAF5,false},
    {0x3B424E,0x262C35,0xF7F9FC,0xBAC5D2,0x9DADBF,0x1C222A,true}
};
constexpr Palette heartColors[] = {
    {0xFF7156,0xFF4F91,0xFFFFFF,0xFFF0F5,0xFFFFFF,0xFFF6F9,false},
    {0xFFA518,0xFFD063,0xFFFFFF,0xFFF6E7,0xFFFFFF,0xFFFAF1,false},
    {0x8A3FFC,0xB567FF,0xFFFFFF,0xF7F0FF,0xFFFFFF,0xF9F6FD,false},
    {0xFFCB99,0xFF9A6A,0x824A32,0x925C44,0xFFFFFF,0xFFF8F4,false}
};
enum Command { Back = 1, Phone, Previous, Next, Open = 100 };
lv_obj_t *pageRoot = nullptr;
lv_timer_t *timer = nullptr;
uint32_t selectedId = 0, shownRevision = 0, refreshAt = 0;
uint32_t orderedIds[20]{};
unsigned sheet = 0, itemCount = 0;
String shownDay;
bool touching = false, rebuild = false;

void event(lv_event_t *e);
lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius = 12) {
    auto *o = lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0); lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
lv_obj_t *text(lv_obj_t *parent, const String &value, int x, int y, int w,
               const lv_font_t *font = &fd_sans_bold_14_ui, uint32_t color = Ink,
               lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    auto *o = lv_label_create(parent); lv_obj_remove_style_all(o); lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, font->line_height + 2); lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0); lv_obj_set_style_text_align(o, align, 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT); lv_label_set_text(o, value.c_str());
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE); return o;
}
lv_obj_t *button(lv_obj_t *parent, const String &caption, int x, int y, int w, int h,
                 int command, uint32_t background = 0xE8EEDC, uint32_t ink = Sage) {
    auto *o = box(parent, x, y, w, h, background, 10); lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, event, LV_EVENT_ALL, (void *)(intptr_t)command); Ui::attachPressFeedback(o);
    if(caption.length()) text(o, caption, 2, (h - fd_sans_bold_14_ui.line_height) / 2, w - 4,
                              &fd_sans_bold_14_ui, ink, LV_TEXT_ALIGN_CENTER);
    return o;
}
void calendarArt(lv_obj_t *parent, int x, int y, int size, uint32_t card, uint32_t ink) {
    auto *o = box(parent, x, y, size, size, card, size / 4);
    const int scale = size / 14;
    auto *page = box(o, scale * 3, scale * 3, size - scale * 6, size - scale * 6, 0xFFFEF9, scale);
    box(page, 0, 0, size - scale * 6, scale * 2, ink, scale);
    box(page, 0, scale, size - scale * 6, scale, ink, 0);
    box(o, scale * 5, scale * 2, scale, scale * 3, ink, scale);
    box(o, size - scale * 6, scale * 2, scale, scale * 3, ink, scale);
    for(int row = 0; row < 2; ++row) for(int col = 0; col < 3; ++col)
        box(page, scale * (1 + col * 2), scale * (3 + row * 2), scale, scale, ink, scale / 2);
}
JsonVariantConst find(uint32_t id) {
    for(JsonVariantConst item : GiftData::get("countdownDays")["items"].as<JsonArrayConst>())
        if((item["id"] | 0u) == id) return item;
    return JsonVariantConst();
}
String dotted(const char *date) { String value = date; value.replace('-', '.'); return value; }
bool countup(JsonVariantConst item) { return item["mode"] == "countup"; }
enum class Category { General, Birthday, Love };
Category category(JsonVariantConst item) {
    if(item["category"] == "birthday") return Category::Birthday;
    if(item["category"] == "love") return Category::Love;
    return Category::General;
}
Style style(JsonVariantConst item) {
    if(item["style"] == "classic") return Style::Classic;
    if(item["style"] == "laurel") return Style::Laurel;
    if(item["style"] == "ink") return Style::Ink;
    if(item["style"] == "heart") return Style::Heart;
    if(item["style"] == "dots") return Style::Dots;
    return category(item) == Category::Birthday ? Style::Laurel :
           category(item) == Category::Love ? Style::Heart : Style::Classic;
}
Palette defaultPalette(JsonVariantConst item) {
    const unsigned color = (item["color"] | 0u) % 4;
    if(style(item) == Style::Heart) return heartColors[color];
    if(style(item) == Style::Dots && color != 3)
        return {0xFFFFFF,0xFAFBFC,0x353D48,0x89939F,0xAEB8C4,0xF3F5F7,false};
    return softColors[color];
}
Palette palette(JsonVariantConst item) {
    auto p = defaultPalette(item);
    const char *value = item["textColor"] | "";
    if(strlen(value) != 7 || value[0] != '#') return p;
    uint32_t custom = 0;
    for(unsigned i = 1; i < 7; ++i) {
        const char c = value[i];
        const int digit = c >= '0' && c <= '9' ? c-'0' :
                          c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
        if(digit < 0) return p;
        custom = (custom << 4) | (uint32_t)digit;
    }
    p.ink = p.muted = custom;
    return p;
}
void gradient(lv_obj_t *obj, const Palette &p, int radius = 23) {
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(p.top), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(p.bottom), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_dither_mode(obj, LV_DITHER_ORDERED, 0);
    lv_obj_set_style_bg_main_stop(obj, 0, 0); lv_obj_set_style_bg_grad_stop(obj, 255, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(p.dark ? 0x637083 : 0xFFFFFF), 0);
    lv_obj_set_style_border_opa(obj, p.dark ? LV_OPA_20 : LV_OPA_70, 0);
}
void cakeArt(lv_obj_t *parent, int x, int y, int unit = 1) {
    const auto part = [&](int px, int py, int w, int h, uint32_t color, int radius = 1) {
        return box(parent, x + px * unit, y + py * unit, w * unit, h * unit, color, radius * unit);
    };
    part(0, 18, 20, 2, 0xC8A163);
    part(2, 10, 16, 8, 0xD99B80, 2);
    part(2, 9, 16, 3, 0xFFFDF6, 2);
    for(int px : {3, 8, 13}) part(px, 11, 3, 3, 0xFFFDF6, 1);
    for(int px : {5, 9, 13}) {
        part(px, 4, 2, 5, 0xB68C6C);
        part(px, 1, 2, 3, 0xE4B65F, 1);
    }
}
void heartArt(lv_obj_t *parent, int x, int y, uint32_t color, bool large = false) {
    static const lv_point_t small[] = {{6,11},{1,6},{0,4},{1,2},{3,1},{5,2},{6,3},
        {7,2},{9,1},{11,2},{12,4},{11,6},{6,11}};
    static const lv_point_t big[] = {{20,37},{4,22},{1,16},{2,9},{6,4},{12,3},{17,6},
        {20,11},{23,6},{28,3},{34,4},{38,9},{39,16},{36,22},{20,37}};
    auto *line = lv_line_create(parent); lv_obj_remove_style_all(line);
    lv_line_set_points(line, large ? big : small, large ? 15 : 13); lv_obj_set_pos(line, x, y);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(line, large ? 3 : 2, 0); lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
}
void drawStroke(lv_draw_ctx_t *ctx, int x1, int y1, int x2, int y2, lv_color_t color,
                int width = 1, lv_opa_t opacity = LV_OPA_COVER) {
    const lv_point_t a = {(lv_coord_t)x1,(lv_coord_t)y1}, b = {(lv_coord_t)x2,(lv_coord_t)y2};
    lv_draw_line_dsc_t dsc; lv_draw_line_dsc_init(&dsc);
    dsc.color = color; dsc.width = width; dsc.opa = opacity; dsc.round_start = dsc.round_end = true;
    lv_draw_line(ctx, &dsc, &a, &b);
}
void drawPolygon(lv_draw_ctx_t *ctx, const lv_point_t *points, unsigned count,
                 lv_color_t color, lv_opa_t opacity = LV_OPA_COVER) {
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color; dsc.bg_opa = opacity; lv_draw_polygon(ctx, &dsc, points, count);
}
void drawDot(lv_draw_ctx_t *ctx, int x, int y, int size, lv_color_t color, lv_opa_t opacity) {
    const lv_area_t area = {(lv_coord_t)x,(lv_coord_t)y,(lv_coord_t)(x+size-1),(lv_coord_t)(y+size-1)};
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc); dsc.bg_color = color;
    dsc.bg_opa = opacity; dsc.radius = LV_RADIUS_CIRCLE; lv_draw_rect(ctx, &dsc, &area);
}
void decoration(lv_event_t *e) {
    auto *obj = lv_event_get_target(e); auto *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area; lv_obj_get_coords(obj, &area); const int x = area.x1, y = area.y1;
    const auto kind = (Style)(intptr_t)lv_event_get_user_data(e);
    const lv_color_t accent = lv_obj_get_style_line_color(obj, 0);
    if(kind == Style::Heart) {
        // Triangles share a center inside the heart, giving one translucent
        // vector silhouette without an image buffer or overlapping lobes.
        static const lv_point_t path[] = {{148,60},{139,47},{125,40},{108,39},{92,44},{81,54},
            {75,68},{75,82},{80,97},{92,112},{110,130},{129,148},{148,164},
            {167,148},{186,130},{204,112},{216,97},{221,82},{221,68},{215,54},
            {204,44},{188,39},{171,40},{157,47}};
        for(unsigned i = 0; i < sizeof(path)/sizeof(path[0]); ++i) {
            const auto &a = path[i], &b = path[(i+1) % (sizeof(path)/sizeof(path[0]))];
            const lv_point_t tri[] = {{(lv_coord_t)(x+148),(lv_coord_t)(y+99)},
                {(lv_coord_t)(x+a.x),(lv_coord_t)(y+a.y)}, {(lv_coord_t)(x+b.x),(lv_coord_t)(y+b.y)}};
            drawPolygon(ctx, tri, 3, lv_color_hex(0xFFFFFF), LV_OPA_20);
        }
        drawDot(ctx, x+28, y+102, 4, accent, LV_OPA_30);
        drawDot(ctx, x+261, y+71, 3, accent, LV_OPA_30);
    } else if(kind == Style::Laurel) {
        // The wreath stays outside the widest five-digit number.
        for(int side = 0; side < 2; ++side) {
            const auto mirror = [side](int px) { return side ? 296-px : px; };
            const lv_point_t branch[] = {{43,136},{33,125},{29,112},{29,98},{33,83}};
            for(unsigned i = 1; i < 5; ++i)
                drawStroke(ctx, x+mirror(branch[i-1].x),y+branch[i-1].y,
                           x+mirror(branch[i].x),y+branch[i].y,accent,1,LV_OPA_80);
            for(int i = 0; i < 4; ++i) {
                const int cy = 88+i*12, base = i == 0 ? 32 : i == 3 ? 33 : 29;
                const int reach = i%2 ? -10 : 10;
                const lv_point_t leaf[] = {
                    {(lv_coord_t)(x+mirror(base)),(lv_coord_t)(y+cy+6)},
                    {(lv_coord_t)(x+mirror(base+reach)),(lv_coord_t)(y+cy-2)},
                    {(lv_coord_t)(x+mirror(base+reach)),(lv_coord_t)(y+cy+5)},
                    {(lv_coord_t)(x+mirror(base+2)),(lv_coord_t)(y+cy+10)}};
                drawPolygon(ctx, leaf, 4, accent, LV_OPA_80);
            }
        }
    } else if(kind == Style::Ink) {
        drawStroke(ctx,x+65,y+136,x+220,y+132,accent,10,LV_OPA_30);
        drawStroke(ctx,x+57,y+138,x+235,y+135,accent,6,LV_OPA_40);
        drawStroke(ctx,x+76,y+140,x+223,y+139,accent,3,LV_OPA_20);
    }
    if(kind == Style::Dots) {
        for(int i = 0; i < 9; ++i) {
            drawDot(ctx,x+53+i*7,y+148,2,accent,LV_OPA_70);
            drawDot(ctx,x+185+i*7,y+148,2,accent,LV_OPA_70);
        }
    } else {
        drawStroke(ctx,x+81,y+148,x+126,y+148,accent,1,LV_OPA_60);
        drawStroke(ctx,x+170,y+148,x+215,y+148,accent,1,LV_OPA_60);
    }
}
DaysLogic::Result evaluate(JsonVariantConst item) {
    return DaysLogic::evaluate(item["date"] | "", item["yearly"] | false, shownDay.c_str(), countup(item));
}
void order() {
    itemCount = 0;
    for(JsonVariantConst item : GiftData::get("countdownDays")["items"].as<JsonArrayConst>())
        if(itemCount < 20) orderedIds[itemCount++] = item["id"] | 0u;
    std::stable_sort(orderedIds, orderedIds + itemCount, [](uint32_t a, uint32_t b) {
        const auto leftItem = find(a), rightItem = find(b);
        auto left = evaluate(leftItem), right = evaluate(rightItem);
        if(left.valid != right.valid) return left.valid;
        if(!left.valid) return false;
        // Keep upcoming plans first, then days being counted, future starts,
        // and past plans. Stable ties retain the user's saved order.
        const auto group = [](JsonVariantConst item, const DaysLogic::Result &value) {
            return countup(item) ? (value.days >= 0 ? 1 : 2) : (value.days >= 0 ? 0 : 3);
        };
        const int leftGroup = group(leftItem, left), rightGroup = group(rightItem, right);
        if(leftGroup != rightGroup) return leftGroup < rightGroup;
        return leftGroup <= 1 ? left.days < right.days : left.days > right.days;
    });
    const unsigned last = itemCount ? (itemCount - 1) / 3 : 0;
    if(sheet > last) sheet = last;
}
void header(bool empty = false, bool counting = false, const char *caption = nullptr,
            uint32_t background = Paper, uint32_t ink = Ink, bool dark = false) {
    auto *back = button(pageRoot, "", 5, 3, 36, 34, Back, background);
    static const lv_point_t arrow[] = {{7,0},{0,7},{7,14}};
    auto *line = lv_line_create(back); lv_line_set_points(line, arrow, 3); lv_obj_set_pos(line, 13, 10);
    lv_obj_set_style_line_color(line, lv_color_hex(ink), 0); lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    text(pageRoot, caption ? caption : counting ? "计时日" : "倒数日", 45, 9, 120, &fd_sans_bold_16_ui, ink);
    if(!empty) button(pageRoot, "手机设置", 218, 3, 92, 34, Phone,
                      dark ? 0x333D4B : 0xEAF0F2, dark ? 0xDCE5EF : 0x64798B);
}
void empty() {
    header(true);
    text(pageRoot, "DAYS TO REMEMBER", 25, 41, 270, &fd_mont_9, Muted, LV_TEXT_ALIGN_CENTER);
    // The loose little marks keep the calendar illustration light on the paper.
    box(pageRoot, 93, 90, 7, 7, 0xD4DEC8, LV_RADIUS_CIRCLE);
    box(pageRoot, 221, 69, 5, 5, 0xE8C9AF, LV_RADIUS_CIRCLE);
    box(pageRoot, 217, 120, 8, 8, 0xDFD5E4, LV_RADIUS_CIRCLE);
    calendarArt(pageRoot, 124, 62, 72, 0xE8EEDC, Sage);
    text(pageRoot, "把期待，装进日子里", 12, 146, 296, &fd_sans_bold_16_ui, Ink, LV_TEXT_ALIGN_CENTER);
    text(pageRoot, "生日、纪念日和相伴时光", 12, 173, 296, &fd_sans_12, Muted, LV_TEXT_ALIGN_CENTER);
    button(pageRoot, "用手机添加日子", 78, 201, 164, 34, Phone, Sage, 0xFFFEF9);
}
void list() {
    if(!itemCount) { empty(); return; }
    header();
    text(pageRoot, shownDay.length() ? "共 " + String(itemCount) + " 个日子  ·  北京时间" : "联网校时后显示准确天数",
         15, 41, 292, &fd_sans_12, Muted);
    for(unsigned row = 0; row < 3; ++row) {
        unsigned index = sheet * 3 + row; if(index >= itemCount) break;
        auto item = find(orderedIds[index]); const auto p = palette(item);
        const uint32_t decorationInk = defaultPalette(item).ink;
        const auto kind = category(item);
        auto result = evaluate(item);
        auto *card = button(pageRoot, "", 12, 61 + row * 51, 296, 47, Open + index, p.top, p.ink);
        gradient(card, p, 13);
        box(card, 0, 12, 4, 23, p.accent, 2);
        const bool themed = kind != Category::General;
        if(kind == Category::Birthday) cakeArt(card, 12, 2);
        else if(kind == Category::Love) heartArt(card, 15, 7, decorationInk);
        else if(style(item) == Style::Dots) {
            for(int i=0;i<3;++i) box(card, 14+i*5, 13, 2, 2, p.accent, LV_RADIUS_CIRCLE);
        } else if(style(item) == Style::Heart) heartArt(card, 15, 7, decorationInk);
        else box(card, 16, 8, 12, 12, p.accent, style(item) == Style::Laurel ? 6 : 3);
        text(card, item["name"] | "", 38, 4, 155, &fd_sans_bold_16_ui, p.ink);
        const bool counting = countup(item);
        String date = dotted(item["date"] | "");
        if(themed) {
            text(card, date, 12, 27, 91, &fd_sans_12, p.ink);
            auto *badge = box(card, 106, 26, 42, 17, decorationInk, 7);
            lv_obj_set_style_bg_opa(badge, LV_OPA_10, 0);
            text(badge, kind == Category::Birthday ? "生日" : "相恋", 2, 1, 38,
                 &fd_sans_12, p.ink, LV_TEXT_ALIGN_CENTER);
        } else {
            if(counting) date += "  计时";
            else if(item["yearly"] | false) date += "  每年";
            text(card, date, 12, 27, 178, &fd_sans_12, p.ink);
        }
        text(card, result.valid ? String(result.days < 0 ? -result.days : result.days) : "--",
             195, 0, 90, &fd_mont_26, p.ink, LV_TEXT_ALIGN_RIGHT);
        const char *state = !result.valid ? "等待校时" : counting ?
            (result.days == 0 ? "开始当天" : result.days > 0 ? "天 · 已过" : "天后开始") :
            (result.days == 0 ? "就是今天" : result.days > 0 ? "天后" : "天前");
        text(card, state,
             201, 29, 84, &fd_sans_12, p.ink, LV_TEXT_ALIGN_RIGHT);
    }
    const unsigned pages = (itemCount + 2) / 3;
    if(pages > 1) {
        auto *previous = button(pageRoot, "上一页", 12, 215, 68, 25, Previous, Paper);
        auto *next = button(pageRoot, "下一页", 240, 215, 68, 25, Next, Paper);
        if(!sheet) lv_obj_add_state(previous, LV_STATE_DISABLED);
        if(sheet + 1 >= pages) lv_obj_add_state(next, LV_STATE_DISABLED);
        text(pageRoot, String(sheet + 1) + " / " + String(pages), 90, 220, 140, &fd_mont_11, Muted, LV_TEXT_ALIGN_CENTER);
    } else text(pageRoot, "轻点卡片，看看记住的日子", 12, 220, 296, &fd_sans_12, Muted, LV_TEXT_ALIGN_CENTER);
}
const char *detailState(JsonVariantConst item, const DaysLogic::Result &result) {
    if(!result.valid) return "等待设备联网校时";
    const bool counting = countup(item);
    if(category(item) == Category::Birthday) return counting ?
        (result.days == 0 ? "今天的生日，从 0 天开始" : result.days > 0 ? "从这个生日开始，已经" : "距离生日开始还有") :
        (result.days == 0 ? "生日快乐，愿你闪闪发光" : result.days > 0 ? "距离这次生日还有" : "这个生日已经过去");
    if(category(item) == Category::Love) return counting ?
        (result.days == 0 ? "今天相恋，故事从这里开始" : result.days > 0 ? "一路相伴，已经" : "距离相恋开始还有") :
        (result.days == 0 ? "今天，赴一场心动的约定" : result.days > 0 ? "距离我们的约定还有" : "心动的这一天已过去");
    return counting ?
        (result.days == 0 ? "今天是开始的日子" : result.days > 0 ? "从这一天起，已经" : "距离开始还有") :
        (result.days == 0 ? "今天就是这一天" : result.days > 0 ? "距离这一天还有" : "这一天已经过去");
}
void detail(JsonVariantConst item) {
    const bool counting = countup(item); const auto kind = category(item); const auto design = style(item);
    const auto result = evaluate(item); const auto p = palette(item);
    lv_obj_set_style_bg_color(pageRoot, lv_color_hex(p.page), 0);
    const char *caption = kind == Category::Birthday ? "生日" : kind == Category::Love ? "相恋" : nullptr;
    const uint32_t navInk = p.dark ? 0xE0E7EF : 0x536575;
    header(false, counting, caption, p.page, navInk, p.dark);
    if(kind == Category::Birthday) cakeArt(pageRoot, 88, 7);
    else if(kind == Category::Love) heartArt(pageRoot, 87, 13, design == Style::Heart ? p.top : p.accent);
    auto *card = box(pageRoot, 12, 43, 296, 185, p.top, 23); gradient(card, p);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x22364A), 0);
    lv_obj_set_style_shadow_width(card, 12, 0); lv_obj_set_style_shadow_ofs_y(card, 3, 0);
    lv_obj_set_style_shadow_opa(card, p.dark ? LV_OPA_10 : 12, 0);
    auto *art = box(card, 0, 0, 296, 185, p.top, 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_TRANSP, 0);
    lv_obj_set_style_line_color(art, lv_color_hex(p.accent), 0);
    lv_obj_add_event_cb(art, decoration, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)design);
    auto *name = text(card, item["name"] | "", 16, 11, 264, &fd_sans_bold_16_ui, p.ink, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP); lv_obj_set_height(name, 41);
    lv_obj_set_style_text_line_space(name, 2, 0);
    text(card, detailState(item, result), 16, 56, 264, &fd_sans_12, p.muted, LV_TEXT_ALIGN_CENTER);
    const String digits = result.valid ? String(result.days < 0 ? -result.days : result.days) : "--";
    text(card, digits, 45, 75, 206, &fd_mont_bold_57, p.ink, LV_TEXT_ALIGN_CENTER);
    text(card, "天", 130, 140, 36, &fd_sans_12, p.ink, LV_TEXT_ALIGN_CENTER);
    const char *mode = counting ? "计时日" : (item["yearly"] | false) ? "每年重复" : "只此一次";
    const String date = dotted(result.valid ? result.target : (item["date"] | ""));
    text(card, date + "  ·  " + mode, 16, 165, 264, &fd_sans_12, p.muted, LV_TEXT_ALIGN_CENTER);
}
void draw() {
    shownDay = GiftData::today(); shownRevision = GiftData::revision(); order();
    // Rebuild only our child tree and defer callbacks until after the click.
    Ui::cancelAnimations(); lv_obj_clean(pageRoot);
    lv_obj_set_style_bg_color(pageRoot, lv_color_hex(Paper), 0);
    if(selectedId) {
        auto item = find(selectedId); if(!item.isNull()) { detail(item); return; }
        selectedId = 0;
    }
    list();
}
void event(lv_event_t *e) {
    const auto code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) touching = true;
    if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) touching = false;
    if(code != LV_EVENT_CLICKED) return;
    lastInputAt = millis(); const int command = (int)(intptr_t)lv_event_get_user_data(e);
    if(command == Phone) { GiftPages::openApp((int)App::Phone); return; }
    if(command == Back) {
        if(!back()) { Ui::dispatchAction(e); }
        return;
    }
    if(command == Previous && sheet) --sheet;
    else if(command == Next && (sheet + 1) * 3 < itemCount) ++sheet;
    else if(command >= Open && command < Open + (int)itemCount) selectedId = orderedIds[command - Open];
    rebuild = true;
}
} // namespace

void createIcon(lv_obj_t *parent, int x, int y) { calendarArt(parent, x, y, 56, 0xE8EEDC, Sage); }
void build(lv_obj_t *parent) {
    pageRoot = parent; touching = false; rebuild = false; refreshAt = millis();
    lv_obj_set_style_bg_color(parent, lv_color_hex(Paper), 0); lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE); draw();
    if(timer) lv_timer_del(timer);
    timer = lv_timer_create([](lv_timer_t *) { refresh(); }, 50, nullptr);
}
void refresh(bool force) {
    if(!pageRoot || touching) return;
    if(!force && !rebuild && millis() - refreshAt < 1000) return;
    refreshAt = millis();
    if(force || rebuild || shownRevision != GiftData::revision() || shownDay != GiftData::today()) {
        rebuild = false; draw();
    }
}
void cancel() {
    if(timer) { lv_timer_del(timer); timer = nullptr; }
    pageRoot = nullptr; touching = false; rebuild = false;
    if(app != App::Days) { selectedId = 0; sheet = 0; }
}
bool back() { if(!selectedId) return false; selectedId = 0; rebuild = true; lastInputAt = millis(); return true; }
bool canBack() { return selectedId != 0; }
bool inputActive() { return touching; }
} // namespace DaysPage
