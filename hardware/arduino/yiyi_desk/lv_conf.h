#pragma once
#define LV_CONF_H
#include <stdint.h>
#include <stdlib.h>
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0
// Compile ordered gradient dithering for RGB565 day cards. Individual objects
// opt in; the default LV_DITHER_NONE remains in effect on every other page.
#define LV_DITHER_GRADIENT 1
#define LV_DITHER_ERROR_DIFFUSION 0
// LVGL only honours screen transparency when `disp->driver->screen_transp` is set;
// this project never sets it, so the previous value was a no-op that suggested a
// feature the panel path cannot provide.
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_TICK_CUSTOM 0
// NOTE: LV_DISP_DEF_REFR_PERIOD was tested at 16 ms together with a wider reveal
// edge; status.transitionFrames did not improve (2-5 bands either way) while the
// blend cost grew, so the LVGL default (30 ms) is kept.
#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_48 1
// 24/32/36 px were never referenced and were dropped by --gc-sections; keep only
// the five sizes the firmware actually links (14 is also LV_FONT_DEFAULT).
#define LV_IMG_CACHE_DEF_SIZE 0
// Raw RGB565 images are not decoded, so the image cache only cost RAM.
#define LV_FONT_FMT_TXT_LARGE 1
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_USER_DATA 1
// saved_design.cpp keys widgets by lv_obj user_data; the name index silently
// breaks if this option is ever disabled.
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_CALENDAR 1
#define LV_USE_CALENDAR_HEADER_ARROW 1
#define LV_CALENDAR_WEEK_STARTS_MONDAY 1
#define LV_USE_QRCODE 1
#define LV_USE_PNG 0
#define LV_USE_SJPG 0
// GIF/IME/Pinyin were enabled for assets that were later removed; no lv_gif or
// lv_ime_pinyin object exists in the firmware (verified with nm).
#define LV_USE_GIF 0
#define LV_USE_IME_PINYIN 0
#define LV_IME_PINYIN_USE_DEFAULT_DICT 0
#define LV_IME_PINYIN_USE_K9_MODE 0
#define LV_USE_BMP 0
#define LV_USE_FREETYPE 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0
