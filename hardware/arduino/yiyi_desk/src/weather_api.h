#pragma once
#include "app_state.h"
#include "text_util.h"
#include <ArduinoJson.h>

namespace WeatherApi {
inline String url(const String &city) {
    return "https://uapis.cn/api/v1/misc/weather?city=" + TextUtil::urlEncode(city) + "&extended=true&forecast=true&lang=zh";
}
inline double optional(JsonVariantConst value, double low, double high) {
    if (!value.is<double>()) return NAN;
    double number = value.as<double>();
    return std::isfinite(number) && number >= low && number <= high ? number : NAN;
}
inline int code(const String &text, int icon = -1) {
    if (text.indexOf("雷") >= 0) return 95;
    if (text.indexOf("雪") >= 0) return 71;
    if (text.indexOf("雨") >= 0) return 61;
    if (text.indexOf("雾") >= 0 || text.indexOf("霾") >= 0) return 45;
    if (text.indexOf("阴") >= 0) return 3;
    if (text.indexOf("云") >= 0) return 2;
    if (text.indexOf("晴") >= 0) return 0;
    if (icon == 100 || icon == 150) return 0;
    if ((icon >= 101 && icon <= 103) || (icon >= 151 && icon <= 153)) return 2;
    if (icon == 104 || icon == 154) return 3;
    if (icon >= 302 && icon <= 304) return 95;
    if (icon >= 300 && icon <= 399) return 61;
    if (icon >= 400 && icon <= 499) return 71;
    if (icon >= 500 && icon <= 515) return 45;
    return -1;
}
inline bool validDate(const String &value) {
    if (value.length() != 10 || value[4] != '-' || value[7] != '-') return false;
    for (int i = 0; i < 10; ++i) if (i != 4 && i != 7 && (value[i] < '0' || value[i] > '9')) return false;
    int y, m, d, consumed = 0;
    return sscanf(value.c_str(), "%d-%d-%d%n", &y, &m, &d, &consumed) == 3
        && consumed == 10 && DeskLogic::validDate(y, m, d);
}
inline String clock(JsonVariantConst value) {
    String text = value | "";
    if (text.length() != 5 || text[2] != ':') return "--:--";
    for (int i : {0, 1, 3, 4}) if (text[i] < '0' || text[i] > '9') return "--:--";
    int hour, minute, consumed = 0;
    return text.length() == 5 && sscanf(text.c_str(), "%d:%d%n", &hour, &minute, &consumed) == 2
        && consumed == 5 && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 ? text : String("--:--");
}
// Parse into a temporary record, so a malformed reply cannot overwrite a cache.
inline bool parse(JsonVariantConst json, Weather &out, String &error, const String &today) {
    double temperature = optional(json["temperature"], -99, 99);
    String condition = json["weather"] | "";
    if (!std::isfinite(temperature) || condition.isEmpty()) {
        error = "天气缺少温度或天气描述"; return false;
    }
    Weather next;
    next.query = out.query;
    next.temperature = temperature; next.condition = condition;
    String icon = json["weather_icon"] | "";
    next.code = code(condition, icon.isEmpty() ? -1 : icon.toInt());
    next.day = !(icon.toInt() >= 150 && icon.toInt() <= 154);
    next.feels = optional(json["feels_like"], -99, 99);
    next.humidity = optional(json["humidity"], 0, 100);
    next.wind = optional(json["wind_speed"], 0, 500);
    next.windText = json["wind_direction"] | "";
    String power = json["wind_power"] | "";
    if (!power.isEmpty()) next.windText += (next.windText.isEmpty() ? "" : " ") + power;
    next.high = optional(json["temp_max"], -99, 99); next.low = optional(json["temp_min"], -99, 99);
    next.pressure = optional(json["pressure"], 0, 2000); next.uv = optional(json["uv"], 0, 30);
    next.visibility = optional(json["visibility"], 0, 1000); next.aqi = optional(json["aqi"], 0, 500);
    next.aqiCategory = json["aqi_category"] | "";
    int index = 0;
    for (JsonVariantConst day : json["forecast"].as<JsonArrayConst>()) {
        String date = day["date"] | "";
        if (!validDate(date)) continue;
        if (date == today) {
            next.sunrise = clock(day["sunrise"]); next.sunset = clock(day["sunset"]);
            if (!std::isfinite(next.high)) next.high = optional(day["temp_max"], -99, 99);
            if (!std::isfinite(next.low)) next.low = optional(day["temp_min"], -99, 99);
            if (!std::isfinite(next.uv)) next.uv = optional(day["uv_index"], 0, 30);
        }
        if (index >= 7) continue;
        auto &forecast = next.forecast[index++];
        forecast.date = date; forecast.low = optional(day["temp_min"], -99, 99);
        forecast.high = optional(day["temp_max"], -99, 99); forecast.code = code(day["weather_day"] | "");
    }
    next.valid = true; out = next; return true;
}
}
