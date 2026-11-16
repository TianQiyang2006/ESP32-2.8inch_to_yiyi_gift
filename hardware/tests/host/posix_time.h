#pragma once
#include <time.h>
#ifdef _WIN32
// Firmware uses the POSIX, caller-owned tm interface; Windows has localtime_s.
inline struct tm *localtime_r(const time_t *value,struct tm *result) {
 return localtime_s(result,value)==0?result:nullptr;
}
#endif
