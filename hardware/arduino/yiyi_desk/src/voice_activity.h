#pragma once
#include <stdint.h>

namespace VoiceActivity {
enum class Event { None, Start, End };
// PCM RMS after removing DC; time advances only on complete 60ms frames.
// Short clicks do not start a turn, and silence cannot submit an empty turn.
struct Detector {
    static constexpr uint32_t OnsetMs=180, QuietMs=900, MaxTurnMs=30000;
    float noise=120;
    uint32_t voicedMs=0, quietMs=0, turnMs=0;
    bool started=false, ended=false;
    void reset(){voicedMs=quietMs=turnMs=0;started=ended=false;}
    float threshold() const {return noise*2.8f>420?noise*2.8f:420;}
    bool voiced(float rms) const {return rms>=threshold();}
    Event feed(float rms,uint32_t frameMs=60){
        if(ended)return Event::None;
        bool voice=voiced(rms);
        if(!started){
            if(voice)voicedMs+=frameMs;
            else {voicedMs=0;noise=noise*.95f+rms*.05f;}
            if(voicedMs<OnsetMs)return Event::None;
            started=true;turnMs=voicedMs;return Event::Start;
        }
        turnMs+=frameMs;
        quietMs=voice?0:quietMs+frameMs;
        if(quietMs>=QuietMs||turnMs>=MaxTurnMs){ended=true;return Event::End;}
        return Event::None;
    }
};
}
