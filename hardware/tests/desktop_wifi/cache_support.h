#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
inline void* heap_caps_malloc(size_t bytes, int) { return malloc(bytes); }
inline void heap_caps_free(void* buffer) { free(buffer); }
