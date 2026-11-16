// Page 1 / weather app - 天气页. Hand-written LVGL page.
//
// Layout and colours follow build/design/weather/final-20261027-weather-v1/
// (weather-redesign-states.png is the pixel reference); widget/interaction
// rules follow UI实现规格.md §4. 320x240 landscape, no status bar, no Bluetooth,
// no page dots.
//
// Structure:
//   top info row   (0..28)   city / date / last update block (click = refresh)
//   main card      (8,30)    304x90  round icon backdrop, condition, big temp,
//                                     four label/value rows
//   metric card    (8,126)   304x54  six metrics, 3 rows x 2 columns
//   forecast card  (8,184)   304x52  seven equal columns, today tinted green
//
// States (weather-v1): online / updating (old values kept, icon pulses) /
// offline with cache / offline without cache / update failed.
//
// The last successful reading is persisted to LittleFS (/weather_cache.txt) so
// the "offline with cache" state survives a reboot (UI实现规格.md §4.5); only the
// display fields are stored, no query or coordinates.
#include "page_api.h"

#include "../app_state.h"
#include "../clock_source.h"
#include "../gift_assets/gift_fonts.h"
#include "../saved_design/saved_resources.h"
#include "../text_util.h"
#include "../ui_styles.h"
#include "../ui_widgets.h"
#include "../../desk_logic.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Digits-only NotoSansSC-Bold 44px font generated for the big temperature
// (src/pages/assets/weather_temp44.c): the locked design image's digit ink is
// 34.5px tall, which a 42px face does not reach.
extern "C" const lv_font_t weather_temp44;
// NotoSansSC-Bold 15px font for the city name; matches the 14.25px CJK ink in
// the design image and covers the place-name glyphs missing from the shared sets.
extern "C" const lv_font_t weather_city15;

namespace WeatherPage {
namespace {

// ---- palette (WMO/design locked) -----------------------------------------
constexpr uint32_t PageBg = 0xFCF7ED;   // cream page background
constexpr uint32_t CardBg = 0xFFFDF8;   // card surface
constexpr uint32_t Ink = 0x574637;      // primary brown text
constexpr uint32_t Muted = 0xA38C70;    // secondary brown text
constexpr uint32_t IconCircle = 0xEDF3FB;  // main icon round backdrop
constexpr uint32_t TodayBg = 0xEEF5E7;  // today's forecast column
constexpr uint32_t OfflineDot = 0xD68A40;  // orange dot of the offline state
constexpr uint32_t ShadowColor = 0x9C8E7A;

enum class State { Online, OnlineStale, Updating, OfflineCache, OfflineEmpty, Failed };

// ---- cached widget pointers (all reset by build()) -----------------------
lv_obj_t *page = nullptr;
lv_obj_t *cityLabel = nullptr;
lv_obj_t *dateLabel = nullptr;
lv_obj_t *updateLabel = nullptr;
lv_obj_t *updateDot = nullptr;
lv_obj_t *updateHit = nullptr;
lv_obj_t *mainIcon = nullptr;
lv_obj_t *conditionLabel = nullptr;
lv_obj_t *tempLabel = nullptr;
lv_obj_t *unitLabel = nullptr;
lv_obj_t *rowLabel[4] = {};
lv_obj_t *rowValue[4] = {};
lv_obj_t *metricIcon[6] = {};
lv_obj_t *metricLabel[6] = {};
lv_obj_t *metricValue[6] = {};
lv_obj_t *todayCol = nullptr;
lv_obj_t *weekLabel[7] = {};
lv_obj_t *weekIcon[7] = {};
lv_obj_t *weekTemp[7] = {};

bool built = false;
int debugState = 0;  // host renderer hook: 0 default, 1 online, 2 cache, 3 empty, 4 updating

// Last successful reading kept in RAM. The weather struct itself survives a
// failed refresh; this copy (plus the LittleFS file below) makes the cached
// reading available again after a reboot, when `weather.valid` starts false.
Weather lastGood;
bool hasLastGood = false;
int64_t lastGoodAt = -1;

// `/weather_cache.txt` holds the minimal display fields of the last successful
// fetch. `cacheWrittenAt` is deliberately *not* reset by build(): one file write
// per new reading, not one per page rebuild.
constexpr const char *CachePath = "/weather_cache.txt";
constexpr size_t CacheMaxBytes = 4096;
int64_t cacheWrittenAt = -1;
bool cacheLoadAttempted = false;

bool pulseRunning = false;
bool wasUpdating = false;
int todayIndex = -1;

// ---- fonts ---------------------------------------------------------------
const lv_font_t *const cityFonts[] = {&weather_city15, saved_gift_font_bold_14_0,
                                      saved_gift_font_bold_13_0};
const lv_font_t *const dateFonts[] = {&fd_sans_bold_12, saved_gift_font_bold_10_0};
const lv_font_t *const conditionFonts[] = {&weather_city15, saved_gift_font_bold_14_0,
                                           saved_gift_font_bold_13_0};
const lv_font_t *const valueFonts[] = {saved_gift_font_bold_11_0,
                                       saved_gift_font_bold_10_0,
                                       saved_gift_font_bold_9_0};
const lv_font_t *const tempBigFonts[] = {saved_gift_font_bold_34_0,
                                         saved_gift_font_bold_28_0};
const lv_font_t *const weekTempFonts[] = {saved_gift_font_bold_8_0,
                                          saved_gift_font_bold_7_0};

// ---- small helpers -------------------------------------------------------
const lv_img_dsc_t *mainIconFor(int code) {
    if (code < 0) return saved_gift_weather_unknown;
    if (code == 0) return saved_gift_weather_sun;
    if (code <= 3) return saved_gift_weather_cloud;
    if (code < 50) return saved_gift_weather_fog;
    if (code < 70 || (code >= 80 && code <= 82)) return saved_gift_weather_rain;
    if (code < 90) return saved_gift_weather_snow;
    return saved_gift_weather_storm;
}

const lv_img_dsc_t *weekIconFor(int code) {
    if (code < 0) return saved_gift_forecast_unknown;
    if (code == 0) return saved_gift_forecast_sun;
    if (code <= 3) return saved_gift_forecast_cloud;
    if (code < 50) return saved_gift_forecast_fog;
    if (code < 70 || (code >= 80 && code <= 82)) return saved_gift_forecast_rain;
    if (code < 90) return saved_gift_forecast_snow;
    return saved_gift_forecast_storm;
}

const char *conditionName(int code) {
    if (code < 0) return "未知";
    if (code == 0) return "晴";
    if (code == 3) return "阴";
    if (code < 4) return "多云";
    if (code < 50) return "雾";
    if (code < 70 || (code >= 80 && code <= 82)) return "雨";
    if (code < 90) return "雪";
    return "雷雨";
}

const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};

int weekdayOf(DeskLogic::Civil d) {
    int64_t n = DeskLogic::civilDays(d.year, d.month, d.day);
    return (int)((n + 4) % 7 + 7) % 7;  // 0 = Sunday
}

String dateKey(DeskLogic::Civil d) {
    char key[16];
    snprintf(key, sizeof(key), "%04d-%02d-%02d", d.year, d.month, d.day);
    return key;
}

bool parseDate(const String &value, DeskLogic::Civil &out) {
    int y = 0, m = 0, d = 0;
    if (sscanf(value.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return false;
    if (!DeskLogic::validDate(y, m, d)) return false;
    out = DeskLogic::beijingDate(DeskLogic::beijingEpoch(y, m, d));
    return true;
}

int textWidth(const String &value, const lv_font_t *font) {
    return (int)lv_txt_get_width(value.c_str(), (uint32_t)value.length(), font, 0,
                                 LV_TEXT_FLAG_NONE);
}

// True when every codepoint of `text` has a glyph in `font` (following LVGL's
// .fallback chain like the renderer does). A font without the glyphs would draw
// them as placeholder boxes, so fitFont() must not select such a font.
bool fontCovers(const char *text, const lv_font_t *font) {
    if (!text || !font) return false;
    lv_font_glyph_dsc_t glyph;
    for (const unsigned char *p = (const unsigned char *)text; *p;) {
        uint32_t cp;
        const unsigned char c = *p++;
        if (c < 0x80) cp = c;
        else if ((c & 0xE0) == 0xC0) {
            if ((*p & 0xC0) != 0x80) return false;
            cp = ((uint32_t)(c & 0x1F) << 6) | (*p++ & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
            if ((p[0] & 0xC0) != 0x80 || (p[1] & 0xC0) != 0x80) return false;
            cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((c & 0xF8) == 0xF0) {
            if ((p[0] & 0xC0) != 0x80 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
                return false;
            cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[0] & 0x3F) << 12) |
                 ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            return false;  // malformed UTF-8
        }
        if (cp == '\n' || cp == '\r' || cp == '\t') continue;
        if (!lv_font_get_glyph_dsc(font, &glyph, cp, 0)) return false;
    }
    return true;
}

const lv_font_t *fitFont(const String &value, const lv_font_t *const *fonts, int count,
                         int maxWidth) {
    if (value.isEmpty()) return fonts[0];
    for (int i = 0; i < count; ++i)
        if (fontCovers(value.c_str(), fonts[i]) && textWidth(value, fonts[i]) <= maxWidth)
            return fonts[i];
    return fonts[count - 1];
}

void setText(lv_obj_t *label, const String &value) {
    if (!label) return;
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, value.c_str()) != 0) lv_label_set_text(label, value.c_str());
}

void setFont(lv_obj_t *label, const lv_font_t *font) {
    if (!label || !font) return;
    if (lv_obj_get_style_text_font(label, 0) != font) lv_obj_set_style_text_font(label, font, 0);
    const lv_coord_t height = (lv_coord_t)font->line_height;
    if (lv_obj_get_height(label) != height) lv_obj_set_height(label, height);
}

void setVisible(lv_obj_t *obj, bool visible) {
    if (!obj) return;
    const bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (visible && hidden) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (!visible && !hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void setImage(lv_obj_t *img, const lv_img_dsc_t *src) {
    if (!img) return;
    if (lv_img_get_src(img) != (const void *)src) {
        lv_img_set_src(img, src);
        // lv_img_set_src() re-centres the pivot; restore the top-left scaling our
        // layout positions assume.
        lv_img_set_pivot(img, 0, 0);
    }
}

// ---- persisted weather cache ---------------------------------------------
double cachedNumber(JsonVariantConst value) {
    return value.isNull() ? NAN : value.as<double>();
}

template <class Target>
void storeNumber(Target &target, const char *key, double value) {
    // Missing readings are stored as absent keys and restored as NAN, so the page
    // keeps showing "--" instead of inventing a value.
    if (std::isfinite(value)) target[key] = value;
}

bool writeWeatherCache(const Weather &reading) {
    if (!reading.valid || reading.fetchedAt <= 0) return false;
    JsonDocument doc;
    doc["v"] = 1;
    doc["at"] = (long long)reading.fetchedAt;
    doc["city"] = settings.city;  // discard the cache if the configured city changes
    doc["code"] = reading.code;
    doc["cond"] = reading.condition;
    doc["windText"] = reading.windText;
    doc["aqiCategory"] = reading.aqiCategory;
    storeNumber(doc, "temp", reading.temperature);
    storeNumber(doc, "feels", reading.feels);
    storeNumber(doc, "humidity", reading.humidity);
    storeNumber(doc, "wind", reading.wind);
    storeNumber(doc, "low", reading.low);
    storeNumber(doc, "high", reading.high);
    storeNumber(doc, "pressure", reading.pressure);
    storeNumber(doc, "uv", reading.uv);
    storeNumber(doc, "visibility", reading.visibility);
    storeNumber(doc, "aqi", reading.aqi);
    doc["sunrise"] = reading.sunrise;
    doc["sunset"] = reading.sunset;
    JsonArray forecast = doc["fc"].to<JsonArray>();
    for (int i = 0; i < 7; ++i) {
        if (reading.forecast[i].date.isEmpty()) continue;
        JsonObject item = forecast.add<JsonObject>();
        item["date"] = reading.forecast[i].date;
        item["code"] = reading.forecast[i].code;
        storeNumber(item, "high", reading.forecast[i].high);
        storeNumber(item, "low", reading.forecast[i].low);
    }
    String text;
    if (serializeJson(doc, text) > CacheMaxBytes) return false;
    File file = LittleFS.open(CachePath, "w");
    if (!file) return false;
    const size_t written = file.write((const uint8_t *)text.c_str(), text.length());
    file.close();
    if (written != text.length()) {
        LittleFS.remove(CachePath);
        return false;
    }
    return true;
}

// A3: write a new reading at most once per fetchedAt. Kept independent of the
// page so the data layer (GiftUI/desk) can persist a reading while another page
// is visible; a failed write leaves cacheWrittenAt alone and is retried next call.
void persistReading(const Weather &reading) {
    if (!reading.valid || reading.fetchedAt <= 0) return;
    if (reading.fetchedAt == cacheWrittenAt) return;
    if (writeWeatherCache(reading)) cacheWrittenAt = reading.fetchedAt;
}

bool readWeatherCache(Weather &out) {
    if (!LittleFS.exists(CachePath)) return false;
    File file = LittleFS.open(CachePath, "r");
    if (!file) return false;
    const size_t size = file.size();
    if (size < 8 || size > CacheMaxBytes) {
        file.close();
        return false;
    }
    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        file.close();
        return false;
    }
    const size_t read = file.readBytes(buffer, size);
    file.close();
    buffer[read] = '\0';
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buffer, read);
    free(buffer);
    if (error) return false;
    if ((int)(doc["v"] | 0) != 1) return false;
    const long long fetchedAt = doc["at"].isNull() ? 0 : doc["at"].as<long long>();
    if (fetchedAt <= 0) return false;
    // Legacy caches have no city key (accepted as-is); new ones are discarded
    // once the configured city changes so another city's values cannot be shown.
    const char *cachedCity = doc["city"] | "";
    if (cachedCity[0] != '\0' && settings.city != cachedCity) return false;

    Weather cached;
    cached.valid = true;
    cached.fetchedAt = fetchedAt;
    cached.code = (int)(doc["code"] | -1);
    cached.condition = (const char *)(doc["cond"] | "");
    cached.windText = (const char *)(doc["windText"] | "");
    cached.aqiCategory = (const char *)(doc["aqiCategory"] | "");
    cached.temperature = cachedNumber(doc["temp"]);
    cached.feels = cachedNumber(doc["feels"]);
    cached.humidity = cachedNumber(doc["humidity"]);
    cached.wind = cachedNumber(doc["wind"]);
    cached.low = cachedNumber(doc["low"]);
    cached.high = cachedNumber(doc["high"]);
    cached.pressure = cachedNumber(doc["pressure"]);
    cached.uv = cachedNumber(doc["uv"]);
    cached.visibility = cachedNumber(doc["visibility"]);
    cached.aqi = cachedNumber(doc["aqi"]);
    cached.sunrise = (const char *)(doc["sunrise"] | "");
    cached.sunset = (const char *)(doc["sunset"] | "");
    JsonArrayConst forecast = doc["fc"].as<JsonArrayConst>();
    int index = 0;
    for (JsonVariantConst item : forecast) {
        if (index >= 7) break;
        cached.forecast[index].date = (const char *)(item["date"] | "");
        cached.forecast[index].code = (int)(item["code"] | -1);
        cached.forecast[index].high = cachedNumber(item["high"]);
        cached.forecast[index].low = cachedNumber(item["low"]);
        ++index;
    }
    out = cached;
    return true;
}

// ---- widget factories ----------------------------------------------------
lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius,
               bool clip) {
    auto *panel = Ui::panel(parent, x, y, w, h, bg, radius);
    lv_obj_set_style_shadow_width(panel, 2, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 2, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(ShadowColor), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    if (clip) lv_obj_set_style_clip_corner(panel, true, 0);
    return panel;
}

lv_obj_t *text(lv_obj_t *parent, int x, int y, int w, const lv_font_t *font,
               uint32_t color) {
    auto *label = Ui::label(parent, "", x, y, w, font, color);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(label, (lv_coord_t)font->line_height);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

lv_obj_t *image(lv_obj_t *parent, const lv_img_dsc_t *src, int x, int y, int zoom) {
    auto *img = lv_img_create(parent);
    lv_img_set_src(img, src);
    lv_img_set_pivot(img, 0, 0);  // scale from the top-left: the design coordinates are top-left
    lv_img_set_zoom(img, (uint16_t)zoom);
    lv_obj_set_pos(img, x, y);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}

// ---- animations ----------------------------------------------------------
// Updating keeps the old reading on screen; only the icon breathes until the
// request finishes, then the (possibly new) icon fades in.
void iconOpaExec(void *var, int32_t value) {
    lv_obj_set_style_img_opa((lv_obj_t *)var, (lv_opa_t)value, 0);
}

void startPulse() {
    if (!mainIcon || pulseRunning) return;
    pulseRunning = true;
    lv_anim_del(mainIcon, nullptr);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, mainIcon);
    lv_anim_set_values(&anim, 255, 150);
    lv_anim_set_time(&anim, 700);
    lv_anim_set_playback_time(&anim, 700);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, iconOpaExec);
    lv_anim_start(&anim);
}

void stopPulse(bool fadeIn) {
    if (!mainIcon) {
        pulseRunning = false;
        return;
    }
    if (pulseRunning) lv_anim_del(mainIcon, nullptr);
    pulseRunning = false;
    if (!fadeIn) {
        lv_obj_set_style_img_opa(mainIcon, LV_OPA_COVER, 0);
        return;
    }
    lv_obj_set_style_img_opa(mainIcon, 0, 0);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, mainIcon);
    lv_anim_set_values(&anim, 0, 255);
    lv_anim_set_time(&anim, 240);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, iconOpaExec);
    lv_anim_start(&anim);
}

// ---- state resolution ----------------------------------------------------
State resolveState(bool &cached) {
    cached = false;
    if (debugState == 1) return State::Online;
    if (debugState == 2) {
        cached = true;
        return State::OfflineCache;
    }
    if (debugState == 3) return State::OfflineEmpty;
    if (debugState == 4) return State::Updating;

    if (weatherPending) return State::Updating;
    const bool online = WiFi.status() == WL_CONNECTED;
    if (weather.valid) {
        if (!online) {
            cached = true;
            return State::OfflineCache;
        }
        if (!weather.error.isEmpty()) return State::Failed;
        return State::Online;
    }
    // A failed refresh while online is "update failed" even when only the
    // persisted cache is available; the cached state itself requires offline.
    if (online && !weather.error.isEmpty()) return State::Failed;
    // B9: online with a cached reading but no fresh fetch yet (e.g. NTP not
    // synced / auto update not started) must not claim "waiting for network".
    if (online && hasLastGood) return State::OnlineStale;
    if (hasLastGood && !online) {
        // Reading restored from LittleFS (or the RAM copy): the device has no
        // fresh data, so show the cached state with its fetch time.
        cached = true;
        return State::OfflineCache;
    }
    return State::OfflineEmpty;
}

void rememberLastGood() {
    if (!weather.valid) return;
    // Persist before the last-good bookkeeping so a failed write is retried on
    // the next 1 Hz refresh (cacheWrittenAt only advances on success).
    persistReading(weather);
    if (weather.fetchedAt == lastGoodAt) return;
    lastGood = weather;
    lastGoodAt = weather.fetchedAt;
    hasLastGood = true;
}

// One attempt per page build: a missing/corrupt file must fall back to the
// "waiting for network" state instead of retrying the flash read every second.
void loadPersistedCache() {
    if (cacheLoadAttempted) return;
    cacheLoadAttempted = true;
    if (hasLastGood) return;
    Weather cached;
    if (!readWeatherCache(cached)) return;
    lastGood = cached;
    lastGoodAt = cached.fetchedAt;
    hasLastGood = true;
}

void updateForecastToday(const Weather *data) {
    int index = 0;  // design always tints the first column when no data exists
    if (data) {
        const auto today = DeskLogic::beijingDate(ClockSource::now());
        const String key = dateKey(today);
        for (int i = 0; i < 7; ++i) {
            if (!data->forecast[i].date.isEmpty() && data->forecast[i].date == key) {
                index = i;
                break;
            }
        }
    }
    if (index == todayIndex) return;
    todayIndex = index;
    if (!todayCol) return;
    const int x = (304 * index) / 7;
    const int width = (304 * (index + 1)) / 7 - x;
    lv_obj_set_pos(todayCol, (lv_coord_t)x, 0);
    lv_obj_set_size(todayCol, (lv_coord_t)width, 52);
}

// Places the unit right of the temperature and keeps both on the design rows.
// The design puts "°C" so that its ink starts at page x=147; the temp digits use
// a 3px left side bearing, hence the base offset of 82 and the +5 gap.
void placeUnit(const String &temperature, const lv_font_t *tempFont) {
    if (!tempLabel || !unitLabel) return;
    const int width = textWidth(temperature, tempFont);
    lv_obj_set_x(unitLabel, (lv_coord_t)(82 + width + 4));  // card-local x of temp = 82
}

// D-6: the 1 Hz refresh used to rebuild every label/font/measurement even when
// nothing visible had changed. This signature lets it return before the heavy
// work; the first build and every real data/state/date/city change still runs the
// full pass, so the rendered result is identical.
bool haveSig = false;
int lastStateSig = -1, lastDebugSig = 0;
bool lastValidSig = false, lastHasGoodSig = false;
int64_t lastFetchedSig = 0, lastGoodFetchedSig = 0, lastDaySig = -1;
String lastCitySig;

void applyState(bool force = false) {
    if (!built) return;
    rememberLastGood();
    if (!weather.valid) loadPersistedCache();

    bool cached = false;
    const State state = resolveState(cached);
    const int64_t nowSig = ClockSource::now();
    const int64_t daySig = (nowSig + 8 * 3600) / 86400;  // Beijing date number
    const int64_t fetchedSig = weather.valid ? weather.fetchedAt : 0;
    const int64_t goodFetchedSig = hasLastGood ? lastGood.fetchedAt : 0;
    if (!force && haveSig && static_cast<int>(state) == lastStateSig && weather.valid == lastValidSig &&
        fetchedSig == lastFetchedSig && hasLastGood == lastHasGoodSig &&
        goodFetchedSig == lastGoodFetchedSig && daySig == lastDaySig &&
        settings.city == lastCitySig && debugState == lastDebugSig)
        return;
    haveSig = true;
    lastStateSig = (int)state;
    lastDebugSig = debugState;
    lastValidSig = weather.valid;
    lastHasGoodSig = hasLastGood;
    lastFetchedSig = fetchedSig;
    lastGoodFetchedSig = goodFetchedSig;
    lastDaySig = daySig;
    lastCitySig = settings.city;

    const bool empty = state == State::OfflineEmpty;
    const Weather *data = nullptr;
    if (!empty) {
        if (weather.valid) data = &weather;
        else if (hasLastGood) data = &lastGood;
    }

    // ---- top row ---------------------------------------------------------
    const String city = settings.city.isEmpty() ? String("未设置城市") : settings.city;
    const lv_font_t *cityFont = fitFont(city, cityFonts, 3, 96);
    setText(cityLabel, city);
    setFont(cityLabel, cityFont);
    lv_obj_set_y(cityLabel, cityFont == cityFonts[0] ? 5 : 6);

    const int64_t now = ClockSource::now();
    const bool timeOk = DeskLogic::timeValid(now);
    String dateText;
    if (timeOk) {
        const auto d = DeskLogic::beijingDate(now);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d月%d日 星期%s", d.month, d.day, week[weekdayOf(d)]);
        dateText = buffer;
    } else {
        dateText = "等待联网校时";
    }
    const lv_font_t *dateFont = fitFont(dateText, dateFonts, 2, 140);
    setText(dateLabel, dateText);
    setFont(dateLabel, dateFont);
    lv_obj_set_y(dateLabel, dateFont == dateFonts[0] ? 8 : 7);

    String stamp = "--:--";
    if (data && data->fetchedAt > 0) stamp = TextUtil::formatDate(data->fetchedAt, "%H:%M");
    String update;
    bool showDot = false;
    switch (state) {
        case State::Online: update = "更新 " + stamp; break;
        case State::OnlineStale: update = "在线 · 等待更新"; break;
        case State::Updating: update = "更新中…"; break;
        case State::OfflineCache:
            update = "离线 · " + stamp;
            showDot = true;
            break;
        case State::OfflineEmpty: update = "等待联网"; break;
        case State::Failed: update = "更新失败 · 点击重试"; break;
    }
    setText(updateLabel, update);
    setVisible(updateDot, showDot);
    setVisible(updateHit, true);

    // ---- main card -------------------------------------------------------
    String condition;
    String temperature = "--";
    const lv_font_t *tempFont = &weather_temp44;
    if (data) {
        condition = data->condition.isEmpty() ? String(conditionName(data->code)) : data->condition;
        temperature = TextUtil::number(data->temperature);
        const bool shrink = temperature.length() >= 3 || temperature.startsWith("-");
        if (shrink) {
            tempFont = tempBigFonts[0];
            if (textWidth(temperature, tempFont) > 112) tempFont = tempBigFonts[1];
        } else if (textWidth(temperature, tempFont) > 112) {
            tempFont = tempBigFonts[0];
        }
        setImage(mainIcon, mainIconFor(data->code));
    } else {
        condition = "等待联网";
        setImage(mainIcon, saved_gift_weather_unknown);
    }
    const lv_font_t *conditionFont = fitFont(condition, conditionFonts, 3, 136);
    setText(conditionLabel, condition);
    setFont(conditionLabel, conditionFont);
    setText(tempLabel, temperature);
    setFont(tempLabel, tempFont);
    placeUnit(temperature, tempFont);
    setVisible(unitLabel, data && std::isfinite(data->temperature));

    String rowValues[4];
    if (data) {
        rowValues[0] = TextUtil::number(data->feels, "°");
        rowValues[1] = TextUtil::number(data->humidity, "%");
        rowValues[2] = data->windText.isEmpty() ? TextUtil::number(data->wind, " km/h")
                                                : data->windText;
        rowValues[3] = TextUtil::number(data->high, "°") + " / " + TextUtil::number(data->low, "°");
    } else {
        rowValues[0] = "--";
        rowValues[1] = "--%";
        rowValues[2] = "--";
        rowValues[3] = "--° / --°";
    }
    for (int i = 0; i < 4; ++i) {
        setText(rowValue[i], rowValues[i]);
        setFont(rowValue[i], fitFont(rowValues[i], valueFonts, 3, 62));
    }

    // ---- metrics ---------------------------------------------------------
    String metricValues[6];
    if (data) {
        metricValues[0] = data->sunrise.isEmpty() ? String("--") : data->sunrise;
        metricValues[1] = data->sunset.isEmpty() ? String("--") : data->sunset;
        metricValues[2] = TextUtil::number(data->uv);
        metricValues[3] = TextUtil::number(data->pressure, " hPa");
        metricValues[4] = TextUtil::number(data->visibility, " km");
        String aqi = TextUtil::number(data->aqi);
        if (std::isfinite(data->aqi)) {
            if (!data->aqiCategory.isEmpty()) aqi += " " + data->aqiCategory;
            else if (data->aqi <= 50) aqi += " 优";
            else if (data->aqi <= 100) aqi += " 良";
        }
        metricValues[5] = aqi;
    } else {
        for (auto &value : metricValues) value = "--";
    }
    static const int metricMaxWidth[6] = {84, 84, 84, 88, 84, 88};
    for (int i = 0; i < 6; ++i) {
        setText(metricValue[i], metricValues[i]);
        setFont(metricValue[i], fitFont(metricValues[i], valueFonts, 3, metricMaxWidth[i]));
    }

    // ---- seven-day forecast ---------------------------------------------
    for (int i = 0; i < 7; ++i) {
        const Weather::Forecast *forecast = data ? &data->forecast[i] : nullptr;
        DeskLogic::Civil day{};
        bool dated = forecast && parseDate(forecast->date, day);
        if (!dated) {
            if (!timeOk) {
                setText(weekLabel[i], "--");
            } else {
                const auto value = DeskLogic::beijingDate(now + (int64_t)i * 86400);
                day = value;
                dated = true;
            }
        }
        if (dated) setText(weekLabel[i], String("周") + week[weekdayOf(day)]);

        if (forecast) {
            setText(weekTemp[i], TextUtil::number(forecast->high, "°") + "/" +
                                     TextUtil::number(forecast->low, "°"));
            setImage(weekIcon[i], weekIconFor(forecast->code));
        } else {
            setText(weekTemp[i], "--°/--°");
            setImage(weekIcon[i], saved_gift_forecast_unknown);
        }
        setFont(weekTemp[i], fitFont(String(lv_label_get_text(weekTemp[i])), weekTempFonts, 2, 42));
    }
    updateForecastToday(data);

    // ---- updating animation ---------------------------------------------
    const bool updating = state == State::Updating;
    if (updating) {
        wasUpdating = true;
        startPulse();
    } else if (wasUpdating) {
        wasUpdating = false;
        stopPulse(data != nullptr);
    }
}

void updateClicked(lv_event_t *) { GiftPages::fire(GiftPages::RefreshWeather); }

}  // namespace

// ---- module API ----------------------------------------------------------
// A3: data-layer entry point (page_api.h). Persists the last successful reading
// even when the weather page is not visible; no LVGL access, no dependency on
// this page being built (deduplicated by fetchedAt via cacheWrittenAt).
void storeCache(const Weather &reading) { persistReading(reading); }

void build(lv_obj_t *parent) {
    // Reset every cached pointer: the host renderer and the firmware rebuild the
    // whole page whenever it is shown.
    page = parent;
    cityLabel = dateLabel = updateLabel = updateDot = updateHit = nullptr;
    mainIcon = conditionLabel = tempLabel = unitLabel = nullptr;
    for (auto *&label : rowLabel) label = nullptr;
    for (auto *&value : rowValue) value = nullptr;
    for (auto *&icon : metricIcon) icon = nullptr;
    for (auto *&label : metricLabel) label = nullptr;
    for (auto *&value : metricValue) value = nullptr;
    todayCol = nullptr;
    for (auto *&label : weekLabel) label = nullptr;
    for (auto *&icon : weekIcon) icon = nullptr;
    for (auto *&value : weekTemp) value = nullptr;
    built = false;
    todayIndex = -1;
    pulseRunning = false;
    wasUpdating = false;
    // Re-read the persisted reading on every build (cheap, once per page open);
    // `cacheWrittenAt` intentionally survives so a new fetch is stored only once.
    lastGood = Weather{};
    hasLastGood = false;
    lastGoodAt = -1;
    cacheLoadAttempted = false;

    lv_obj_set_style_bg_color(parent, lv_color_hex(PageBg), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // ---- top info row (0..28) -------------------------------------------
    updateHit = Ui::panel(parent, 196, 0, 124, 28, PageBg, 0);
    lv_obj_clear_flag(updateHit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(updateHit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(updateHit, updateClicked, LV_EVENT_CLICKED, nullptr);
    Ui::attachPressFeedback(updateHit);

    cityLabel = text(parent, 12, 5, 96, cityFonts[0], Ink);
    dateLabel = text(parent, 80, 8, 160, dateFonts[0], Ink);
    lv_obj_set_style_text_align(dateLabel, LV_TEXT_ALIGN_CENTER, 0);
    updateDot = Ui::panel(parent, 240, 8, 7, 7, OfflineDot, LV_RADIUS_CIRCLE);
    lv_obj_clear_flag(updateDot, LV_OBJ_FLAG_CLICKABLE);
    updateLabel = text(parent, 190, 7, 120, saved_gift_font_bold_10_0, Muted);
    lv_obj_set_style_text_align(updateLabel, LV_TEXT_ALIGN_RIGHT, 0);

    // ---- main weather card (8,30) 304x90 ---------------------------------
    lv_obj_t *mainCard = card(parent, 8, 30, 304, 90, CardBg, 16, true);
    lv_obj_t *circle = Ui::panel(mainCard, 4, 10, 70, 70, IconCircle, LV_RADIUS_CIRCLE);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    mainIcon = image(mainCard, saved_gift_weather_unknown, 11, 17, 211);
    conditionLabel = text(mainCard, 84, 4, 140, conditionFonts[0], Ink);
    tempLabel = text(mainCard, 82, 26, 130, &weather_temp44, Ink);
    unitLabel = text(mainCard, 82, 39, 40, &weather_city15, Ink);
    setText(unitLabel, "°C");

    static const int rowY[4] = {7, 25, 43, 61};  // card-local, page 37/55/73/91
    for (int i = 0; i < 4; ++i) {
        rowLabel[i] = text(mainCard, 222, rowY[i], 22, saved_gift_font_bold_8_0, Muted);
        rowValue[i] = text(mainCard, 244, rowY[i] + 1, 62, valueFonts[0], Ink);
    }
    setText(rowLabel[0], "体感");
    setText(rowLabel[1], "湿度");
    setText(rowLabel[2], "风况");
    setText(rowLabel[3], "温度");

    // ---- six metrics card (8,126) 304x54 ---------------------------------
    lv_obj_t *metricCard = card(parent, 8, 126, 304, 54, CardBg, 14, true);
    static const char *metricNames[6] = {"日出", "日落", "紫外线", "气压", "能见度", "空气质量"};
    static const lv_img_dsc_t *const metricImages[6] = {
        saved_gift_metric_sunrise, saved_gift_metric_sunset, saved_gift_metric_uv,
        saved_gift_metric_pressure, saved_gift_metric_visibility, saved_gift_metric_aqi};
    static const int iconX[6] = {12, 160, 12, 160, 12, 160};    // card-local
    static const int labelX[6] = {26, 174, 26, 174, 26, 174};  // card-local
    static const int valueX[6] = {68, 216, 68, 216, 68, 216};  // card-local
    static const int rowTop[3] = {4, 21, 38};                 // card-local page 130/147/164
    static const int labelTop[3] = {1, 19, 34};               // matches the design label ink tops
    for (int i = 0; i < 6; ++i) {
        const int row = i / 2;
        metricIcon[i] = image(metricCard, metricImages[i], iconX[i], rowTop[row], 148);
        metricLabel[i] = text(metricCard, labelX[i], labelTop[row], 40,
                              saved_gift_font_bold_8_0, Muted);
        setText(metricLabel[i], metricNames[i]);
        metricValue[i] = text(metricCard, valueX[i], rowTop[row] - 2, 84, valueFonts[0], Ink);
    }
    // The design orders the two columns as: 日出/日落, 紫外线/气压, 能见度/空气质量.

    // ---- seven-day forecast card (8,184) 304x52 --------------------------
    lv_obj_t *forecastCard = card(parent, 8, 184, 304, 52, CardBg, 14, true);
    todayCol = Ui::panel(forecastCard, 0, 0, 43, 52, TodayBg, 12);
    lv_obj_clear_flag(todayCol, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 7; ++i) {
        const int x = (304 * i) / 7;
        const int width = (304 * (i + 1)) / 7 - x;
        weekLabel[i] = text(forecastCard, x, 4, width, saved_gift_font_bold_9_0, Ink);
        lv_obj_set_style_text_align(weekLabel[i], LV_TEXT_ALIGN_CENTER, 0);
        setText(weekLabel[i], "--");
        const int iconX = x + (width - 20) / 2;
        weekIcon[i] = image(forecastCard, saved_gift_forecast_unknown, iconX, 15, 190);
        weekTemp[i] = text(forecastCard, x, 36, width, weekTempFonts[0], Muted);
        lv_obj_set_style_text_align(weekTemp[i], LV_TEXT_ALIGN_CENTER, 0);
        setText(weekTemp[i], "--°/--°");
    }

    built = true;
    haveSig = false;
    applyState(true);
}

void refresh(bool force) {
    applyState(force);
}

bool ready() {
    return built && page && cityLabel && dateLabel && updateLabel && updateHit && mainIcon &&
           conditionLabel && tempLabel && unitLabel && rowLabel[3] && rowValue[3] &&
           metricIcon[5] && metricLabel[5] && metricValue[5] && todayCol && weekLabel[6] &&
           weekIcon[6] && weekTemp[6];
}

void cancel() {
    if (mainIcon) lv_anim_del(mainIcon, nullptr);
    pulseRunning = false;
    wasUpdating = false;
    built = false;
    mainIcon = nullptr;
    page = nullptr;
}

bool back() { return false; }

// No detail level on the weather page: only the App/origin layer can go back.
bool canBack() { return false; }

void debugVariant(int which) { debugState = which; }

}  // namespace WeatherPage
