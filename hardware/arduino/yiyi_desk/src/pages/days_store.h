#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace DaysStore {
// Each request applies to the latest device data. An edit/remove whose ID has
// disappeared is rejected, so a stale phone draft cannot overwrite another day.
bool mutate(JsonVariantConst request, String &error);
}
