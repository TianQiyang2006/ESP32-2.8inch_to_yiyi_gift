#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace DashboardClockConfig {
struct Config {
    uint32_t color = 0xffffff;
    int weight = 700;
    int opacity = 90;
    int spacing = -18;
    bool seconds = false;
    bool hour24 = true;
    bool tilt = true;
    bool lunar = true;
    bool festival = true;
    bool rain = true;
    bool snow = true;
    bool thunder = true;
};

// Missing fields in an older saved configuration use the upstream defaults.
Config load();
// Saves all shared clock options together, after validation by GiftData.
bool save(const Config &value, String &error);
}
