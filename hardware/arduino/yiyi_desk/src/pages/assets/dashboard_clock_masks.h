#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t offset; uint16_t length; } DashboardClockMaskGlyph;
typedef struct { uint16_t width,height; DashboardClockMaskGlyph glyphs[11]; } DashboardClockMaskFont;
extern const uint8_t dashboard_clock_mask_data[];
// 0..8 = Nunito 100..900; 9 = upstream condensed ClockDigits.
extern const DashboardClockMaskFont dashboard_clock_mask_fonts[10];
#ifdef __cplusplus
}
#endif
