#pragma once
#include <stdint.h>

struct ScheduleCourse {
  const char *name;
  const char *teacher;
  const char *room;
  uint32_t color;
  uint8_t day;
  uint8_t start;
  uint8_t end;
  uint32_t weeks;
};

extern const ScheduleCourse kScheduleCourses[];
extern const int kScheduleCourseCount;
extern const char *kScheduleTermStart;
extern const char *kScheduleFallbackDate;
extern const uint8_t kScheduleTotalWeeks;
extern const char *kSchedulePeriodStart[12];
extern const char *kSchedulePeriodEnd[12];
