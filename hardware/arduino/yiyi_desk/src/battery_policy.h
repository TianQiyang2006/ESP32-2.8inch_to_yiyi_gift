#pragma once
#include <stdint.h>
#include <math.h>

// One-cell Li-ion profile shared by the desktop and music application.
// Software reports/alerts only; charging and current cutoff belong to hardware.
namespace BatteryPolicy {
constexpr int NominalMillivolts=3700, RatedEnergyMwh=12775;
constexpr int RatedCapacityMah=(RatedEnergyMwh*1000+NominalMillivolts/2)/NominalMillivolts;
constexpr uint32_t SampleMs=2000;

inline int estimate(float volts) {
    if(!std::isfinite(volts)||volts<2.5f||volts>4.45f)return -1;
    const float voltage[]={3.0f,3.3f,3.5f,3.65f,3.75f,3.85f,4.0f,4.1f,4.2f};
    const int percent[]={0,5,10,20,40,60,80,90,100};
    if(volts<=voltage[0])return 0;
    for(int i=1;i<9;++i)if(volts<=voltage[i])
        return percent[i-1]+int((volts-voltage[i-1])/(voltage[i]-voltage[i-1])*(percent[i]-percent[i-1])+.5f);
    return 100;
}

struct State {
    bool low=false, critical=false, sensorFault=false, high=false;
    const char *name() const {
        return sensorFault?"sensor_fault":high?"high_voltage":critical?"critical":low?"low":"normal";
    }
};

class Monitor {
    State state_;
    bool sampled_=false, highPending_=false;
    uint32_t last_=0, highAt_=0;
    unsigned highCount_=0;
public:
    const State &get() const {return state_;}
    void reset() {*this=Monitor{};}
    void update(uint32_t now,int millivolts,int percent,bool installed=true) {
        if(!installed){reset();return;}
        if(sampled_&&uint32_t(now-last_)<SampleMs)return;
        if(sampled_&&uint32_t(now-last_)>6000){highPending_=false;highCount_=0;}
        sampled_=true;last_=now;
        state_.sensorFault=millivolts<2500||millivolts>4450;
        if(state_.sensorFault) {
            highPending_=false;highCount_=0;
            return;
        }
        state_.low=percent<=20||(state_.low&&percent<25);
        state_.critical=percent<=5||(state_.critical&&percent<8);
        if(millivolts>=4250) {
            if(!highPending_){highPending_=true;highAt_=now;highCount_=0;}
            ++highCount_;
            if(uint32_t(now-highAt_)>=4000&&highCount_>=3)state_.high=true;
        } else {
            highPending_=false;highCount_=0;
            if(millivolts<=4150)state_.high=false;
        }
    }
};

enum class Notice { None=0, Low=1, Critical=2, SensorFault=3, HighVoltage=4 };
class NoticeGate {
    unsigned shown_=0;
public:
    Notice update(const State &state) {
        const unsigned mask=(state.low?2u:0u)|(state.critical?4u:0u)|
                            (state.sensorFault?8u:0u)|(state.high?16u:0u);
        shown_ &= mask; // Recovery rearms that notice for the next real episode.
        for(int i=4;i>=1;--i)if(mask&(1u<<i)) {
            if(shown_&(1u<<i))return Notice::None;
            // A critical notice also covers the lower-priority low warning.
            shown_|=i==2?6u:(1u<<i);
            return static_cast<Notice>(i);
        }
        return Notice::None;
    }
};
}
