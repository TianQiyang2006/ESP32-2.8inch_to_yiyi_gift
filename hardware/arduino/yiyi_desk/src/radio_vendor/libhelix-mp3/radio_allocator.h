#pragma once
#if defined(ESP32)
#include <esp_heap_caps.h>
#define malloc(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define calloc(n,s) heap_caps_calloc((n), (s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif
