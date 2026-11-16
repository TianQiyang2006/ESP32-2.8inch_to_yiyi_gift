#pragma once
#include <Arduino.h>
namespace VoiceTranscript {
inline void append(String &answer,const String &sentence) {
    if(sentence.isEmpty())return;
    answer+=sentence;
    // Bound a reply while retaining complete UTF-8 code points.
    if(answer.length()>3600){int start=answer.length()-3300;while(((uint8_t)answer[start]&0xc0)==0x80)++start;answer=String("…")+answer.substring(start);}
}
}
