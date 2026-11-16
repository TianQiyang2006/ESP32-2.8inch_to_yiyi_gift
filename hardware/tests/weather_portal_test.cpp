#include "hardware/arduino/yiyi_desk/src/weather_api.h"
#include "hardware/arduino/yiyi_desk/src/captive_portal.h"
#include <cassert>
#include <iostream>

int main() {
    assert(WeatherApi::url("杭州") == "https://uapis.cn/api/v1/misc/weather?city=%E6%9D%AD%E5%B7%9E&extended=true&forecast=true&lang=zh");
    assert(WeatherApi::url("A&B").indexOf("city=A%26B&") >= 0);
    JsonDocument json;
    assert(!deserializeJson(json, R"json({"city":"杭州","weather":"晴间多云","weather_icon":"103","temperature":0,"humidity":0,"feels_like":0,"visibility":0,"uv":0,"aqi":0,"aqi_category":"优","wind_direction":"东北风","wind_power":"2级","forecast":[{"date":"2026-10-11","temp_max":15,"temp_min":0,"weather_day":"雷阵雨","sunrise":"06:10","sunset":"17:40"},{"date":"2026-02-30","temp_max":99},{"date":"2026-10-12","weather_day":"雨夹雪","temp_max":14}]} )json"));
    Weather weather; weather.query = "杭州"; String error;
    assert(WeatherApi::parse(json.as<JsonVariantConst>(), weather, error, "2026-10-11"));
    assert(weather.valid && weather.temperature == 0 && weather.humidity == 0 && weather.feels == 0);
    assert(weather.visibility == 0 && weather.uv == 0 && weather.aqi == 0);
    assert(weather.query == "杭州" && weather.condition == "晴间多云" && weather.code == 2);
    assert(weather.windText == "东北风 2级" && std::isnan(weather.wind));
    assert(weather.sunrise == "06:10" && weather.sunset == "17:40" && weather.low == 0 && weather.high == 15);
    assert(weather.forecast[0].code == 95 && weather.forecast[1].code == 71);
    assert(weather.forecast[1].date == "2026-10-12" && weather.forecast[2].date.isEmpty());
    assert(std::isnan(weather.forecast[1].low) && std::isnan(weather.pressure));
    json.remove("temperature");
    assert(!WeatherApi::parse(json.as<JsonVariantConst>(), weather, error, "2026-10-11"));
    assert(weather.valid && weather.temperature == 0 && weather.condition == "晴间多云");
    json["temperature"] = true;
    assert(!WeatherApi::parse(json.as<JsonVariantConst>(), weather, error, "2026-10-11"));
    json["temperature"] = -10; json["weather"] = "雾"; json["humidity"] = "75";
    assert(WeatherApi::parse(json.as<JsonVariantConst>(), weather, error, "2026-10-12"));
    assert(weather.code == 45 && std::isnan(weather.humidity) && weather.sunrise == "--:--");
    assert(!WeatherApi::validDate("2026-1-001") && !WeatherApi::validDate("2026-02-30"));
    json["clock"] = "1:000"; assert(WeatherApi::clock(json["clock"]) == "--:--");
    json["clock"] = "24:00"; assert(WeatherApi::clock(json["clock"]) == "--:--");
    json["clock"] = "00:00"; assert(WeatherApi::clock(json["clock"]) == "00:00");
    assert(WeatherApi::code("未知天气", 300) == 61 && WeatherApi::code("未知天气", 302) == 95);
    for (const char *path : CaptivePortal::ProbePaths) assert(CaptivePortal::redirectUnknown(path, true));
    assert(CaptivePortal::redirectUnknown("/unknown-page", true));
    assert(!CaptivePortal::redirectUnknown("/api/missing", true));
    assert(!CaptivePortal::redirectUnknown("/unknown-page", false));
    std::cout << "PASS: real firmware UApi adapter, Chinese queries, optional/zero values, failed cache updates, forecast dates, icons and captive portal route policy.\n";
}
