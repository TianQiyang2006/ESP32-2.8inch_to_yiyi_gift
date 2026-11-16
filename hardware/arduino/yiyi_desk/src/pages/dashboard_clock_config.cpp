#include "dashboard_clock_config.h"
#include "../gift_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace DashboardClockConfig {
namespace {
bool hexColor(const char *text, uint32_t &value) {
    if (!text || strlen(text) != 7 || text[0] != '#') return false;
    for (int i = 1; i < 7; ++i) {
        char ch = text[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) return false;
    }
    value = uint32_t(strtoul(text + 1, nullptr, 16));
    return true;
}
}

Config load() {
    Config value;
    auto stored = GiftData::get("dashboardClock");
    hexColor(stored["color"] | "", value.color);
    int weight = stored["weight"] | value.weight;
    if (weight >= 100 && weight <= 900 && weight % 100 == 0) value.weight = weight;
    int opacity = stored["opacity"] | value.opacity;
    if (opacity >= 10 && opacity <= 100) value.opacity = opacity;
    int spacing = stored["spacing"] | value.spacing;
    if (spacing >= -18 && spacing <= -8) value.spacing = spacing;
    value.seconds = stored["seconds"] | value.seconds;
    value.hour24 = stored["hour24"] | value.hour24;
    value.tilt = stored["tilt"] | value.tilt;
    value.lunar = stored["lunar"] | value.lunar;
    value.festival = stored["festival"] | value.festival;
    value.rain = stored["rain"] | value.rain;
    value.snow = stored["snow"] | value.snow;
    value.thunder = stored["thunder"] | value.thunder;
    return value;
}

bool save(const Config &value, String &error) {
    if (value.color > 0xffffff || value.weight < 100 || value.weight > 900 ||
        value.weight % 100 != 0 || value.opacity < 10 || value.opacity > 100 ||
        value.spacing < -18 || value.spacing > -8) {
        error = "钟面设置超出范围";
        return false;
    }
    JsonDocument patch;
    auto target = patch["dashboardClock"].to<JsonObject>();
    char color[8];
    snprintf(color, sizeof(color), "#%06lx", (unsigned long)value.color);
    target["color"] = color;
    target["weight"] = value.weight;
    target["opacity"] = value.opacity;
    target["spacing"] = value.spacing;
    target["seconds"] = value.seconds;
    target["hour24"] = value.hour24;
    target["tilt"] = value.tilt;
    target["lunar"] = value.lunar;
    target["festival"] = value.festival;
    target["rain"] = value.rain;
    target["snow"] = value.snow;
    target["thunder"] = value.thunder;
    return GiftData::patch(patch.as<JsonVariantConst>(), error);
}
}
