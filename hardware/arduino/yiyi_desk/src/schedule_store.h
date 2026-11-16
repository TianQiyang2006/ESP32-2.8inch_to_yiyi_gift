#pragma once
#include <stdint.h>

// Runtime timetable model. Defaults to the baked data in schedule_data.cpp;
// a compact text file imported from the phone config page replaces it.
namespace ScheduleStore {
struct Course {
    char name[48];
    char teacher[24];
    char room[48];
    uint32_t color;
    uint8_t day;
    uint8_t start;
    uint8_t end;
    uint32_t weeks;
};

void begin();
bool load();
int count();
const Course &course(int index);
const char *termStart();
uint8_t totalWeeks();
const char *periodStart(int node);
const char *periodEnd(int node);
// Parses the compact text produced by the config page, writes /schedule.txt and
// reloads. On any validation error the previous timetable is kept.
bool saveText(const char *text, unsigned length);
bool clear();
const char *lastError();
bool imported();
}
