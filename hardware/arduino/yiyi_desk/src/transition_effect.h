#pragma once
#include <stdint.h>

namespace TransitionEffect {
// Edge is the soft gradient width of the moving reveal band. A 24 px edge was
// measured against 16 px at 40 MHz SPI: transitionFrames stayed the same while
// the per-band blend cost grew, so the original 16 px edge is kept.
constexpr int Width=320,Edge=16,End=Width+Edge;
// Smoothstep has zero velocity at both ends. Only the moving reveal band is
// transferred over SPI; a full-screen LVGL slide would exceed this panel's bus.
inline int progress(uint32_t elapsed,uint32_t duration){
    if(elapsed>=duration||!duration)return End;
    int64_t t=uint64_t(elapsed)*1024/duration;
    return int(t*t*(3072-2*t)*End/(1024LL*1024*1024));
}
// display.cpp drives the reveal from an lv_anim timeline: the animation value
// is the elapsed time in milliseconds (0..duration), so the same smoothstep
// mapping and the same band widths are kept as the previous millis() polling.
inline int timelineProgress(int32_t elapsed,uint32_t duration){
    return progress(elapsed<0?0u:(uint32_t)elapsed,duration);
}
inline uint8_t opacity(int progress,int x,int direction){
    int distance=progress-(direction<0?Width-1-x:x);
    return distance<=0?0:distance>=Edge?255:uint8_t(distance*255/Edge);
}
inline uint16_t blend(uint16_t oldColor,uint16_t newColor,uint8_t opacity){
    if(!opacity)return oldColor;if(opacity==255)return newColor;
    int inverse=255-opacity;
    int r=(((oldColor>>11)*inverse+(newColor>>11)*opacity)+127)/255;
    int g=((((oldColor>>5)&63)*inverse+((newColor>>5)&63)*opacity)+127)/255;
    int b=(((oldColor&31)*inverse+(newColor&31)*opacity)+127)/255;
    return uint16_t((r<<11)|(g<<5)|b);
}
}
