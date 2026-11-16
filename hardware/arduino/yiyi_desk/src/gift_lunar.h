#pragma once
#include <stdint.h>
namespace GiftLunar {
struct Date {
    uint8_t month = 0;
    uint8_t day = 0;
    bool leap = false;
    bool valid = false;
};
// Solar date -> lunar date. Covers 1900-01-31 .. 2099-12-31; earlier solar
// dates (before the 1900 lunar new year) return an invalid Date.
Date fromSolar(int year, int month, int day);
// "九月十九" / "闰六月初一". `out` is always NUL terminated.
void format(const Date &date, char *out, unsigned size);
// Month length table verified day by day against lunar-javascript 1.7.7.
bool selfTest();
}
