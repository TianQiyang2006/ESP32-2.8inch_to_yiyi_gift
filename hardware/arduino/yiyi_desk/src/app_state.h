#pragma once
// State shared between the firmware's layers.
//
// The UI views, the service layer and the job pipeline all read or write the same
// small set of variables. They used to live in an anonymous namespace inside
// desk.cpp, which gives them internal linkage and makes them unreachable from any
// other translation unit. Moving the definitions here (and the declarations into
// this header) is what lets the UI layer be split into its own modules.
//
// Only genuinely shared state belongs here. Module-private things stay where they
// are: the web server and the NVS handle are private to desk.cpp, while the job
// queues and the IO mutex are defined in jobs.cpp and declared in jobs.h.
//
// Threading contract: everything below is read/written from the Arduino loop
// task only (UI callbacks, deskLoop, and the WebServer/WifiSetup callbacks that
// also run from loop). The ioWorker never writes these directly: it hands Results
// back through the FreeRTOS queue, so no lock/volatile is needed here.
#include <Arduino.h>
#include <lvgl.h>
#include <stdint.h>

// Which view is on screen. App::None means one of the seven swipe pages.
// Album/Messages keep their historical numeric slots for compatibility with the
// screens seam, but neither has a built-in view any more.
enum class App { None, Album, Messages, Weather, Calendar, Countdown, Settings, Phone, About, Diagnostics, Voice, Wifi, Clock, Alarm, Pomodoro, Tetris, Music, Timer, Snake, Jump, Sheep, Miner, Plane, Breakout, Mines, Pin, Memory, Days, Radio, Games };

struct Settings {
    String ssid, password, city, countdownName;
    double latitude = 0, longitude = 0;
    int64_t target = 0;
    bool hasLocation = false, hasTarget = false, inverted = true, bgr = true, white = false;
    uint8_t brightness = 180, touchMode = 0;
};

struct Weather {
    bool valid = false;
    String query, condition, windText, aqiCategory;
    double temperature = NAN, feels = NAN, humidity = NAN, wind = NAN, low = NAN, high = NAN;
    int code = -1;
    bool day = true;
    int64_t fetchedAt = 0;
    String error;
    double latitude = 0, longitude = 0;
    double pressure=NAN,uv=NAN,visibility=NAN,aqi=NAN;
    String sunrise="--:--",sunset="--:--";
    struct Forecast {String date;double low=NAN,high=NAN;int code=-1;};
    Forecast forecast[7];
};

extern Settings settings;
extern Weather weather;

extern uint8_t page;
extern App app;
extern App phoneReturn;
extern uint8_t phoneReturnPage;

extern bool weatherPending;
// True while the background quote/poem fetch is in flight. Read by the reading
// page to show the refreshing spinner; written by the desk job pipeline.
extern bool readingPending;

extern bool renderPending;
extern uint32_t lastInputAt;

// Widgets the live-update pass writes into. They are cleared at the start of every
// render, so a stale pointer can never be dereferenced after a view is replaced.
extern lv_obj_t *root;
extern lv_obj_t *clockLabel, *dateLabel, *weatherLabel;
extern lv_obj_t *countLabel, *countDetail;
extern lv_obj_t *touchLabel;
extern lv_obj_t *calendar, *calendarTitle;
extern lv_calendar_date_t selectedDate, marks[2];
extern uint16_t calendarYear;
extern uint8_t calendarMonth;
// B1 -> B2: the calendar has picked a date and the Countdown view is showing
// its confirmation page rather than the legacy free-form editor.
extern bool calendarConfirm;

// Access point credentials, shown by the phone-configuration view.
extern String apName, portalPassword;
extern bool apSuspended;

// Non-persistent UI actions can report storage errors without depending on the
// WebServer implementation in desk.cpp.
void notifyUser(const String &message, bool error = false);
