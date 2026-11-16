#pragma once
#include "../../desk_logic.h"
#include <stdio.h>

// Natural-date arithmetic is independent of time_t and device clock validity.
// Callers pass GiftData::today(), which is empty until Beijing time is trusted.
namespace DaysLogic {
constexpr unsigned MaxItems = 20;
constexpr unsigned MaxName = 24;
struct Date { int year=0, month=0, day=0; };
struct Result {
    bool valid=false;
    int days=0;
    int year=0, month=0, day=0;
    char target[11]{};
};
inline bool parseDate(const char *s, Date &out) {
    if (!s || strlen(s)!=10 || s[4]!='-' || s[7]!='-') return false;
    for (unsigned i=0; i<10; ++i)
        if (i!=4 && i!=7 && (s[i]<'0' || s[i]>'9')) return false;
    Date value;
    value.year=(s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+s[3]-'0';
    value.month=(s[5]-'0')*10+s[6]-'0';
    value.day=(s[8]-'0')*10+s[9]-'0';
    if (!DeskLogic::validDate(value.year,value.month,value.day)) return false;
    out=value;
    return true;
}
// Countdown uses target - today; countup uses today - start. The start date is
// day 0, so a future start is negative until the event begins.
inline Result evaluate(const char *date, bool yearly, const char *today, bool countup=false) {
    Result result;
    Date original, current;
    if (!parseDate(date,original) || !parseDate(today,current) || (countup && yearly)) return result;
    Date target=original;
    const int64_t currentDay=DeskLogic::civilDays(current.year,current.month,current.day);
    if (yearly) {
        target.year=current.year>original.year?current.year:original.year;
        target.day=original.day;
        if (target.month==2 && target.day==29 && !DeskLogic::leap(target.year)) target.day=28;
        if (DeskLogic::civilDays(target.year,target.month,target.day)<currentDay) {
            ++target.year;
            target.day=original.day;
            if (target.month==2 && target.day==29 && !DeskLogic::leap(target.year)) target.day=28;
        }
    }
    result.valid=true;
    result.year=target.year;result.month=target.month;result.day=target.day;
    result.days=(int)(DeskLogic::civilDays(target.year,target.month,target.day)-currentDay);
    if (countup) result.days=-result.days;
    snprintf(result.target,sizeof(result.target),"%04d-%02d-%02d",target.year,target.month,target.day);
    return result;
}
} // namespace DaysLogic
