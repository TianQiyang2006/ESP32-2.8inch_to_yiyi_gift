#pragma once
#include <stdint.h>
#include <math.h>

// Same oscillator/envelope definitions as the approved offline WAV samples.
// Generated in 10ms blocks: no WAV blobs, networking or UI-thread playback.
namespace ToneSynth {
inline int16_t applyMaster(int16_t sample,unsigned percent){return int16_t(int32_t(sample)*int32_t(percent>100?100:percent)/100);}
inline float envelope(float t,float length){if(t<0||t>=length)return 0;float a=t/.008f,b=(length-t)/.015f;return (a<1?a:1)*(b<1?b:1);}
inline float sample(unsigned tone,float t){
    constexpr float Tau=6.28318530718f;
    if(tone==0){float phase=fmodf(t,1.2f);for(unsigned i=0;i<2;++i){float u=phase-i*.26f;if(u>=0&&u<.16f)return envelope(u,.16f)*(sinf(Tau*880*u)+.38f*sinf(Tau*1175*u))/1.38f;}return 0;}
    if(tone==1){float u=fmodf(t,1.6f),attack=u/.008f;if(attack>1)attack=1;return attack*expf(-3.3f*u)*(sinf(Tau*659.25f*u)+.38f*sinf(Tau*988.875f*u)+.19f*sinf(Tau*1318.5f*u))/1.57f;}
    float start=0;unsigned n=0;float cycle=fmodf(t,8.0f);
    while(start<8){float interval=.75f-.5f*(start/8.0f);float u=cycle-start;if(u>=0&&u<.12f)return envelope(u,.12f)*sinf(Tau*(n%2?880:660)*u);if(start>cycle)break;start+=interval;++n;}
    return 0;
}
inline int16_t pcm(unsigned tone,uint64_t index,unsigned volume,unsigned fadeMs=0){
    float gain=float(volume)/100.f;
    if(fadeMs&&index<uint64_t(fadeMs)*16)gain*=.2f+.8f*float(index)/float(uint64_t(fadeMs)*16);
    float value=sample(tone,float(index)/16000.f)*11796.12f*gain;
    return int16_t(lrintf(value>32767?32767:value< -32768?-32768:value));
}
} // namespace ToneSynth
