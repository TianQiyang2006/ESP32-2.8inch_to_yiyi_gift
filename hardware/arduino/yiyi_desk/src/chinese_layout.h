#pragma once
#include <Arduino.h>
#include <lvgl.h>

namespace ChineseLayout {
inline bool closing(uint32_t cp) {
    switch (cp) {
        case 0x3001: case 0x3002: case 0xff0c: case 0xff0e:
        case 0xff01: case 0xff1f: case 0xff1a: case 0xff1b:
        case 0x201d: case 0x2019: case 0x3009: case 0x300b:
        case 0x300d: case 0x300f: case 0x3011: case 0xff09:
        case ',': case '.': case '!': case '?': case ':': case ';':
        case ')': case ']': return true;
        default: return false;
    }
}
inline bool opening(uint32_t cp) {
    switch (cp) {
        case 0x201c: case 0x2018: case 0x3008: case 0x300a:
        case 0x300c: case 0x300e: case 0x3010: case 0xff08:
        case '(': case '[': return true;
        default: return false;
    }
}
// Keep Chinese punctuation with its neighbouring text using actual glyph widths.
// Explicit paragraph breaks and the stored source text remain unchanged.
inline String wrap(const String &value, const lv_font_t *font, int width) {
    String result; result.reserve(value.length()+value.length()/12);
    uint32_t offset=0;
    while (offset<value.length()) {
        const char *line=value.c_str()+offset;
        uint32_t count=_lv_txt_get_next_line(line,font,0,width,nullptr,LV_TEXT_FLAG_NONE);
        if (!count) { uint32_t next=0; _lv_txt_encoded_next(line,&next); count=next?next:1; }
        bool explicitBreak=line[count-1]=='\n'||line[count-1]=='\r';
        if (!explicitBreak && offset+count<value.length()) {
            while (count) {
                uint32_t prev=count;
                uint32_t last=_lv_txt_encoded_prev(line,&prev);
                uint32_t next=_lv_txt_encoded_next(line+count,nullptr);
                if (!prev || (!closing(next)&&!opening(last))) break;
                count=prev;
            }
        }
        result+=value.substring(offset,offset+count); offset+=count;
        if (!explicitBreak && offset<value.length()) result+='\n';
    }
    return result;
}
}
