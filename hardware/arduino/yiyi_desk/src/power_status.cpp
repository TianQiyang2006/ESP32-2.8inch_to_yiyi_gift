#include "power_status.h"
#include "gift_data.h"
#include "../board_config.h"
#include <Arduino.h>
#include <algorithm>
#include <math.h>
namespace PowerStatus {
namespace {
Reading value;
BatteryPolicy::Monitor monitor;
constexpr uint32_t SampleIntervalMs=2000;

void updateWarnings() {
    value.protection=monitor.get();
    value.low=value.installed&&value.voltageValid&&value.protection.low;
    value.critical=value.installed&&value.voltageValid&&value.protection.critical;
}
}

int estimate(float v) {
    return BatteryPolicy::estimate(v);
}

void begin() {
    value=Reading{};
    monitor.reset();
    analogReadResolution(12);
    analogSetPinAttenuation(Board::BatteryAdc,ADC_11db);
    poll();
}

int sampleMillivolts() {
    analogReadMilliVolts(Board::BatteryAdc);
    uint32_t samples[9];
    for(auto &sample:samples)sample=analogReadMilliVolts(Board::BatteryAdc);
    std::sort(samples,samples+9);
    return int(samples[4]*2);
}

void poll() {
    const bool installed=GiftData::get("power")["batteryInstalled"]|true;
    const bool external=GiftData::get("power")["externalPower"]|true;
    const bool supplyChanged=external!=value.external;
    value.external=external;
    value.installed=installed;
    if(!installed) {
        // Clear the filter as well: reinstalling must never reuse an old voltage.
        value=Reading{};
        monitor.reset();
        value.external=external;
        return;
    }
    const uint32_t now=millis();
    if(value.sampled&&!supplyChanged&&uint32_t(now-value.sampledAtMs)<SampleIntervalMs) {
        updateWarnings();
        return;
    }
    // Discard the first conversion after selecting the high-impedance divider.
    const int millivolts=sampleMillivolts();
    const float measured=millivolts*.001f; // R14=R15=200k: BAT_ADC is BAT+/2.
    value.sampled=true;
    value.sampledAtMs=now;
    value.rawVolts=measured;
    const bool valid=std::isfinite(measured)&&measured>=2.5f&&measured<=4.45f;
    if(valid) {
        value.volts=value.voltageValid&&!supplyChanged?value.volts*.8f+measured*.2f:measured;
        value.percent=estimate(value.volts);
    } else {
        value.volts=0;
        value.percent=-1;
    }
    value.voltageValid=valid;
    monitor.update(now,millivolts,value.percent);
    // A high/rising voltage or an estimated 100% cannot establish charging/full.
    value.chargingKnown=false;
    value.charging=false;
    updateWarnings();
}

Reading get() { return value; }

const char *description() {
    if(!value.installed)return "未安装电池";
    if(value.protection.high)return "电压偏高 · 请断开充电检查";
    if(!value.voltageValid)return "电压读数异常";
    if(value.critical)return "电量很低 · 请充电";
    if(value.low)return "低电量 · 请充电";
    return value.external?"外接供电 · 充电状态未知":"电池供电";
}


void snapshot(JsonObject out) {
    out["installed"]=value.installed;
    out["externalPower"]=value.external;
    out["externalPowerKnown"]=value.externalKnown;
    out["externalPowerSource"]="setting";
    out["voltageValid"]=value.voltageValid;
    out["estimated"]=true;
    out["percent"]=nullptr;
    out["volts"]=nullptr;
    out["rawVolts"]=nullptr;
    if(value.voltageValid) {out["percent"]=value.percent;out["volts"]=value.volts;}
    if(value.sampled)out["rawVolts"]=value.rawVolts;
    out["chargingKnown"]=value.chargingKnown;
    out["charging"]=nullptr;
    if(value.chargingKnown)out["charging"]=value.charging;
    out["chargingState"]=value.installed?"unknown":"not_applicable";
    out["low"]=value.low;
    out["critical"]=value.critical;
    out["sampled"]=value.sampled;
    out["sampledAtMs"]=value.sampledAtMs;
    out["sampleAgeMs"]=nullptr;
    if(value.sampled)out["sampleAgeMs"]=uint32_t(millis()-value.sampledAtMs);
    out["description"]=description();
    auto pack=out["pack"].to<JsonObject>();
    pack["type"]="18650-1S";pack["nominalVolts"]=3.7;
    pack["ratedEnergyMwh"]=BatteryPolicy::RatedEnergyMwh;
    pack["ratedCapacityMah"]=BatteryPolicy::RatedCapacityMah;
    out["remainingMahEstimate"]=nullptr;out["remainingMwhEstimate"]=nullptr;
    if(value.voltageValid){out["remainingMahEstimate"]=(BatteryPolicy::RatedCapacityMah*value.percent+50)/100;out["remainingMwhEstimate"]=(BatteryPolicy::RatedEnergyMwh*value.percent+50)/100;}
    auto protection=out["protection"].to<JsonObject>();
    protection["state"]=value.installed?value.protection.name():"not_installed";
    protection["mode"]="notify_only";
    protection["automaticSleep"]=false;
    protection["highVoltage"]=value.protection.high;
    protection["sensorFault"]=value.protection.sensorFault;
    protection["hardwareCutoffAvailable"]=false;
    auto charger=out["charger"].to<JsonObject>();
    charger["source"]="external_module";charger["boardController"]="TP4054";charger["controlAvailable"]=false;
    charger["statusAvailable"]=false;charger["cellTemperatureAvailable"]=false;
    charger["currentMa"]=nullptr; // No current sensor. R12 is a hardware setting.
}
}
