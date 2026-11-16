#include "Arduino.h"
#include "hardware/arduino/yiyi_desk/src/power_status.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/board_config.h"
#include <cassert>
#include <iostream>
#include <limits>

uint32_t HostMillis=0;
int HostBatteryMv=1850;
static JsonDocument config;
static int reads=0,sequenceAt=0;
static uint32_t sequence[]={0,0,4095,1850,1851,1849,1850,1850,1850,1850};
namespace GiftData {JsonVariantConst get(const char *key){return config[key];}}
static uint32_t readBattery(int pin){assert(pin==Board::BatteryAdc);++reads;return HostBatteryMv;}
static uint32_t noisyBattery(int pin){assert(pin==Board::BatteryAdc);++reads;return sequence[sequenceAt++%10];}
static void sample(int millivolts,int count=1){HostBatteryMv=millivolts;for(int i=0;i<count;++i){HostMillis+=2000;PowerStatus::poll();}}
static void reset(bool installed,bool external,int mv){
    config["power"]["batteryInstalled"]=installed;
    config["power"]["externalPower"]=external;
    HostBatteryMv=mv;PowerStatus::begin();
}
static JsonDocument snapshot(){JsonDocument json;PowerStatus::snapshot(json.to<JsonObject>());return json;}

int main(){
    using namespace PowerStatus;
    HostBatteryReader=readBattery;
    assert(estimate(std::numeric_limits<float>::quiet_NaN())==-1);
    assert(estimate(std::numeric_limits<float>::infinity())==-1);
    assert(estimate(0)==-1&&estimate(4.5f)==-1);
    assert(estimate(3.0f)==0&&estimate(3.65f)==20&&estimate(4.2f)==100);
    int previous=-1;
    for(int mv=2500;mv<=4450;++mv){int percent=estimate(mv/1000.f);assert(percent>=previous&&percent<=100);previous=percent;}

    reset(false,true,2100);assert(!get().sampled&&get().percent==-1&&reads==0);
    auto json=snapshot();assert(json["percent"].isNull()&&json["volts"].isNull()&&json["charging"].isNull());
    assert(json["chargingState"]=="not_applicable");
    config["power"]["batteryInstalled"]=true;poll();
    assert(get().sampled&&get().voltageValid&&get().percent==100&&reads==10);
    assert(!get().charging&&!get().chargingKnown&&!get().externalKnown);
    const auto stamp=get().sampledAtMs;poll();HostMillis+=1999;poll();assert(reads==10&&get().sampledAtMs==stamp);
    HostMillis++;poll();assert(reads==20);
    sample(1850,80);assert(get().percent==30&&!get().charging); // USB must not force 100%.
    json=snapshot();assert(json["percent"]==30&&json["charging"].isNull()&&json["externalPowerSource"]=="setting");

    HostBatteryReader=noisyBattery;reset(true,false,1850);
    assert(std::abs(get().volts-3.7f)<.001f&&get().percent==30);
    HostBatteryReader=readBattery;
    sample(2000);assert(std::abs(get().volts-3.76f)<.001f); // Median then low-pass, not a single outlier.

    reset(true,false,1825);assert(get().low&&!get().critical);
    sample(1830,80);assert(get().percent==22&&get().low);
    sample(1840,80);assert(get().percent==26&&!get().low);
    sample(1830,80);assert(!get().low); // Does not re-enter until <=20%.
    sample(1825,80);assert(get().low);
    reset(true,false,1650);assert(get().critical);
    sample(1670,80);assert(get().percent==6&&get().critical);
    sample(1720,80);assert(get().percent>=8&&!get().critical);
    config["power"]["externalPower"]=true;poll();assert(get().low&&!get().charging); // Manual USB setting cannot disable care.

    sample(0);assert(!get().voltageValid&&get().percent==-1&&get().volts==0);
    json=snapshot();assert(json["percent"].isNull()&&json["volts"].isNull());
    sample(2000);assert(get().percent==80&&std::abs(get().volts-4.f)<.001f);
    reset(false,false,1500);assert(!get().sampled&&get().rawVolts==0);
    config["power"]["batteryInstalled"]=true;poll();assert(get().percent==0); // No stale filter after reinstall.
    reset(true,false,2500);assert(!get().voltageValid&&get().percent==-1);

    HostMillis=0xfffffff0u;reset(true,false,1850);const int before=reads;
    HostMillis+=1999;poll();assert(reads==before);HostMillis++;poll();assert(reads==before+10);
    json=snapshot();assert(json["sampleAgeMs"]==0u);
    assert(json["pack"]["ratedEnergyMwh"]==12775&&json["pack"]["ratedCapacityMah"]==3453);
    assert(json["remainingMahEstimate"]==1036&&json["remainingMwhEstimate"]==3833);
    assert(json["charger"]["controlAvailable"]==false&&json["charger"]["currentMa"].isNull());
    assert(json["protection"]["externalBoardWiringVerified"].isNull());

    assert(json["protection"]["mode"]=="notify_only");
    assert(json["protection"]["automaticSleep"]==false);
    using BatteryPolicy::Monitor;
    using BatteryPolicy::Notice;
    Monitor guard;BatteryPolicy::NoticeGate notices;
    guard.update(0,3650,20);assert(notices.update(guard.get())==Notice::Low);
    for(unsigned i=1;i<=30;++i){guard.update(i*2000,3650,20);assert(notices.update(guard.get())==Notice::None);}
    guard.update(62000,3300,5);assert(notices.update(guard.get())==Notice::Critical);
    guard.update(64000,3340,6);assert(notices.update(guard.get())==Notice::None);
    guard.update(66000,3440,8);assert(!guard.get().critical&&guard.get().low);
    guard.update(68000,3700,30);assert(notices.update(guard.get())==Notice::None);
    guard.update(70000,3650,20);assert(notices.update(guard.get())==Notice::Low);
    guard.update(72000,0,-1);assert(guard.get().sensorFault&&notices.update(guard.get())==Notice::SensorFault);
    guard.update(74000,3700,30);assert(notices.update(guard.get())==Notice::None);
    guard.reset();guard.update(0,4260,100);guard.update(2000,4260,100);
    assert(!guard.get().high);guard.update(4000,4260,100);assert(guard.get().high);
    assert(notices.update(guard.get())==Notice::HighVoltage);
    guard.update(6000,4200,100);assert(guard.get().high);
    guard.update(8000,4140,94);assert(!guard.get().high);
    guard.reset();guard.update(0,4260,100);guard.update(9000,4260,100);assert(!guard.get().high);
    guard.reset();guard.update(0xfffffff0u,4260,100);guard.update(0xfffffff0u+2000,4260,100);guard.update(0xfffffff0u+4000,4260,100);assert(guard.get().high);
    guard.update(0xfffffff0u+6000,3700,30,false);assert(!guard.get().low&&!guard.get().high);
    // Sustained low voltage is notification-only, with no sleep or restart API.
    reset(true,true,1500);sample(1500,300);assert(get().critical&&get().voltageValid);
    std::cout<<"PASS: voltage curve, ADC outliers, filtering, sample cadence/rollover, invalid recovery, reinstall, low/critical hysteresis and honest charging JSON\n";
}
