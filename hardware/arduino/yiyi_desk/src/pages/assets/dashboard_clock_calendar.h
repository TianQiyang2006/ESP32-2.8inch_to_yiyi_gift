#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint16_t date; uint16_t label; } DashboardClockCalendarEvent;
extern const char *const dashboard_clock_event_names[];
extern const uint16_t dashboard_clock_year_offsets[201];
extern const uint16_t dashboard_clock_lichun[200];
extern const DashboardClockCalendarEvent dashboard_clock_calendar_events[];
#ifdef __cplusplus
}
#endif
