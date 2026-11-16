#pragma once
#include <stdint.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "battery_policy.h"
namespace PowerStatus {
struct Reading {
    bool installed=false, external=true, voltageValid=false;
    // This board has neither VBUS nor TP4054 CHRG connected to the MCU.
    bool externalKnown=false, chargingKnown=false, charging=false;
    bool low=false, critical=false, sampled=false;
    int percent=-1;
    float volts=0, rawVolts=0;
    uint32_t sampledAtMs=0;
    BatteryPolicy::State protection;
};
void begin();
void poll();
Reading get();
void snapshot(JsonObject out);
const char *description();
// A voltage curve is an estimate, not a fuel-gauge measurement.
int estimate(float voltage);
int sampleMillivolts();
}
