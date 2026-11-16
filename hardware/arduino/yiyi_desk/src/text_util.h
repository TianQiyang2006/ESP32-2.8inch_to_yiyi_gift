#pragma once
// Pure text/number formatting helpers.
//
// These used to sit in desk.cpp among the view code, but they hold no screen state
// and are used by the service layer too (URL building, date labels). Keeping them
// here means desk_logic.h can stay free of Arduino types, and the view code shrinks.
#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include "../desk_logic.h"

namespace TextUtil {

// Percent-encodes a query parameter.
inline String urlEncode(const String &str) {
    String out; const char hex[]="0123456789ABCDEF";
    for (size_t i=0;i<str.length();++i) {
        uint8_t c=str[i]; if (isalnum(c) || c=='-' || c=='_' || c=='.') out+=(char)c;
        else { out+='%'; out+=hex[c>>4]; out+=hex[c&15]; }
    }
    return out;
}

// Rounds to a whole number, or renders "--" when the reading is missing. Never
// invents a value: an unavailable field must stay visibly unavailable.
inline String number(double value,const char *unit="") {
    return std::isfinite(value) ? String(value,0)+unit : String("--")+unit;
}

// Formats an epoch in Beijing time. strftime is bounded by the 64-byte buffer.
inline String formatDate(int64_t epoch,const char *format) {
    auto c=DeskLogic::beijingDate(epoch);
    struct tm t={}; t.tm_year=c.year-1900; t.tm_mon=c.month-1; t.tm_mday=c.day;
    t.tm_hour=c.hour; t.tm_min=c.minute; t.tm_sec=c.second;
    char text[64]; strftime(text,sizeof(text),format,&t); return text;
}

inline bool selfTest() {
    bool ok = true;
    ok &= number(24.6,"°") == "25°";
    ok &= number(NAN,"°") == "--°";
    ok &= number(24.6) == "25";
    ok &= urlEncode("a b/c") == "a%20b%2Fc";
    ok &= urlEncode("safe-._") == "safe-._";
    // 1970-01-01 08:00 Beijing is epoch 0.
    ok &= formatDate(0,"%Y-%m-%d %H:%M") == "1970-01-01 08:00";
    // 2099 must still format; it is past the 32-bit time_t limit that the civil
    // date maths in DeskLogic exists to avoid.
    ok &= formatDate(DeskLogic::beijingEpoch(2099,12,31,23,59),"%Y-%m-%d") == "2099-12-31";
    return ok;
}

}
