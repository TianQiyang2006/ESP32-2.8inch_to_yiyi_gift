#pragma once
#include <stdint.h>

namespace RetryPolicy {
inline uint32_t exponential(uint32_t baseMs,uint32_t capMs,uint8_t failures){
    if(!baseMs||!capMs)return 0;
    uint32_t delay=baseMs<capMs?baseMs:capMs;
    while(failures>1&&delay<capMs){
        delay=delay>capMs/2?capMs:delay*2;
        --failures;
    }
    return delay;
}
}
