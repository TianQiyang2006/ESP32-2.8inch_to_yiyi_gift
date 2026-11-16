#pragma once
#include <stdint.h>

namespace RequestDeadline {
inline bool expired(uint32_t now,uint32_t deadline){
    return static_cast<int32_t>(now-deadline)>=0;
}
}
