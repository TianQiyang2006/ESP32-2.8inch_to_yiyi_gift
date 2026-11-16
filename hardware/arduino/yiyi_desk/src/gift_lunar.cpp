#include "gift_lunar.h"
#include "../desk_logic.h"
#include "gift_assets/spring_dates.h"
#include <stdio.h>
#include <string.h>

namespace GiftLunar {
namespace {
constexpr int FirstYear = 1900;
constexpr int LastYear = 2099;
// Packed lunar year: bits 0..3 leap month (0 = none), bits 4..15 the length of
// months 1..12 (1 = 30 days), bit 16 the leap month length (1 = 30 days).
const uint32_t kYear[] = {
0x04BD8u,0x04AE0u,0x0A570u,0x054D5u,0x0D260u,0x0D950u,0x16554u,0x056A0u,0x09AD0u,0x055D2u,
    0x04AE0u,0x0A5B6u,0x0A4D0u,0x0D250u,0x1D255u,0x0B540u,0x0D6A0u,0x0ADA2u,0x095B0u,0x14977u,
    0x04970u,0x0A4B0u,0x0B4B5u,0x06A50u,0x06D40u,0x1AB54u,0x02B60u,0x09570u,0x052F2u,0x04970u,
    0x06566u,0x0D4A0u,0x0EA50u,0x06E95u,0x05AD0u,0x02B60u,0x186E3u,0x092E0u,0x1C8D7u,0x0C950u,
    0x0D4A0u,0x1D8A6u,0x0B550u,0x056A0u,0x1A5B4u,0x025D0u,0x092D0u,0x0D2B2u,0x0A950u,0x0B557u,
    0x06CA0u,0x0B550u,0x15355u,0x04DA0u,0x0A5D0u,0x14573u,0x052B0u,0x0A9A8u,0x0E950u,0x06AA0u,
    0x0AEA6u,0x0AB50u,0x04B60u,0x0AAE4u,0x0A570u,0x05260u,0x0F263u,0x0D950u,0x05B57u,0x056A0u,
    0x096D0u,0x04DD5u,0x04AD0u,0x0A4D0u,0x0D4D4u,0x0D250u,0x0D558u,0x0B540u,0x0B5A0u,0x195A6u,
    0x095B0u,0x049B0u,0x0A974u,0x0A4B0u,0x0B27Au,0x06A50u,0x06D40u,0x0AF46u,0x0AB60u,0x09570u,
    0x04AF5u,0x04970u,0x064B0u,0x074A3u,0x0EA50u,0x06B58u,0x05AC0u,0x0AB60u,0x096D5u,0x092E0u,
    0x0C960u,0x0D954u,0x0D4A0u,0x0DA50u,0x07552u,0x056A0u,0x0ABB7u,0x025D0u,0x092D0u,0x0CAB5u,
    0x0A950u,0x0B4A0u,0x0BAA4u,0x0AD50u,0x055D9u,0x04BA0u,0x0A5B0u,0x15176u,0x052B0u,0x0A930u,
    0x07954u,0x06AA0u,0x0AD50u,0x05B52u,0x04B60u,0x0A6E6u,0x0A4E0u,0x0D260u,0x0EA65u,0x0D530u,
    0x05AA0u,0x076A3u,0x096D0u,0x04AFBu,0x04AD0u,0x0A4D0u,0x1D0B6u,0x0D250u,0x0D520u,0x0DD45u,
    0x0B5A0u,0x056D0u,0x055B2u,0x049B0u,0x0A577u,0x0A4B0u,0x0AA50u,0x1B255u,0x06D20u,0x0ADA0u,
    0x14B63u,0x09370u,0x049F8u,0x04970u,0x064B0u,0x168A6u,0x0EA50u,0x06AA0u,0x1A6C4u,0x0AAE0u,
    0x092E0u,0x0D2E3u,0x0C960u,0x0D557u,0x0D4A0u,0x0DA50u,0x05D55u,0x056A0u,0x0A6D0u,0x055D4u,
    0x052D0u,0x0A9B8u,0x0A950u,0x0B4A0u,0x0B6A6u,0x0AD50u,0x055A0u,0x0ABA4u,0x0A5B0u,0x052B0u,
    0x0B273u,0x06930u,0x07337u,0x06AA0u,0x0AD50u,0x14B55u,0x04B60u,0x0A570u,0x054E4u,0x0D160u,
    0x0E968u,0x0D520u,0x0DAA0u,0x16AA6u,0x056D0u,0x04AE0u,0x0A9D4u,0x0A2D0u,0x0D150u,0x0F252u,
};

const char *kMonths[] = {"", "正月", "二月", "三月", "四月", "五月", "六月",
                        "七月", "八月", "九月", "十月", "冬月", "腊月"};
const char *kDays[] = {"", "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
                       "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
                       "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"};
}

Date fromSolar(int year, int month, int day) {
    Date result;
    if (!DeskLogic::validDate(year, month, day)) return result;
    if (year < FirstYear || year > LastYear) return result;
    int lunarYear = year;
    uint16_t spring = GiftSpringDates[year - FirstYear];
    int64_t start = DeskLogic::civilDays(year, spring / 100, spring % 100);
    int64_t solar = DeskLogic::civilDays(year, month, day);
    if (solar < start) {
        lunarYear--;
        if (lunarYear < FirstYear) return result;
        spring = GiftSpringDates[lunarYear - FirstYear];
        start = DeskLogic::civilDays(lunarYear, spring / 100, spring % 100);
    }
    int offset = (int)(solar - start);
    uint32_t info = kYear[lunarYear - FirstYear];
    int leapMonth = info & 0xF;
    int lunarMonth = 1;
    bool leap = false;
    for (; lunarMonth <= 12; ++lunarMonth) {
        int length = (info & (0x10000u >> lunarMonth)) ? 30 : 29;
        if (offset < length) break;
        offset -= length;
        if (lunarMonth == leapMonth) {
            length = (info & 0x10000u) ? 30 : 29;
            if (offset < length) { leap = true; break; }
            offset -= length;
        }
    }
    if (lunarMonth > 12) return result;
    result.month = (uint8_t)lunarMonth;
    result.day = (uint8_t)(offset + 1);
    result.leap = leap;
    result.valid = true;
    return result;
}

void format(const Date &date, char *out, unsigned size) {
    if (!out || !size) return;
    if (!date.valid || date.month < 1 || date.month > 12 || date.day < 1 || date.day > 30) {
        snprintf(out, size, "--");
        return;
    }
    snprintf(out, size, "%s%s%s", date.leap ? "闰" : "", kMonths[date.month], kDays[date.day]);
}

bool selfTest() {
    Date sample = fromSolar(2025, 11, 8);
    char text[32];
    format(sample, text, sizeof(text));
    bool ok = sample.valid && sample.month == 9 && sample.day == 19 && !sample.leap && !strcmp(text, "九月十九");
    Date newYear = fromSolar(2026, 2, 17);
    ok &= newYear.valid && newYear.month == 1 && newYear.day == 1 && !newYear.leap;
    Date autumn = fromSolar(2026, 10, 19);
    ok &= autumn.valid && autumn.month == 9 && autumn.day == 10;
    Date leap = fromSolar(2025, 7, 25);
    ok &= leap.valid && leap.month == 6 && leap.leap;
    ok &= !fromSolar(1900, 1, 1).valid;
    Date end = fromSolar(2099, 12, 31);
    ok &= end.valid && end.month == 11 && end.day == 20;
    return ok;
}
}
