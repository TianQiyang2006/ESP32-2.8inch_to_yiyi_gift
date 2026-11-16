// Definitions for the state declared in app_state.h. Deliberately outside any
// anonymous namespace so every translation unit can reach them.
#include "app_state.h"
#include <cmath>

Settings settings;
Weather weather;

uint8_t page = 0;
App app = App::None;
App phoneReturn = App::Settings;
uint8_t phoneReturnPage = 4;

bool weatherPending = false;
bool readingPending = false;

bool renderPending = false;
uint32_t lastInputAt = 0;

lv_obj_t *root = nullptr, *clockLabel = nullptr, *dateLabel = nullptr, *weatherLabel = nullptr;
lv_obj_t *countLabel = nullptr, *countDetail = nullptr;
lv_obj_t *touchLabel = nullptr;
lv_obj_t *calendar = nullptr, *calendarTitle = nullptr;
lv_calendar_date_t selectedDate = {}, marks[2] = {};
uint16_t calendarYear = 2026;
uint8_t calendarMonth = 10;
bool calendarConfirm = false;

String apName, portalPassword;
bool apSuspended=false;
