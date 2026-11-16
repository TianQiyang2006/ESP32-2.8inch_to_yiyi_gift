#pragma once
#include <stdlib.h>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#endif
inline void *miniAppAlloc(size_t bytes) {
#ifdef ARDUINO_ARCH_ESP32
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(bytes);
#endif
}
