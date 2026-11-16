#pragma once
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <mutex>
#include "battery_policy.h"  // Snapshot of the desktop's battery policy.

// This board exposes only BAT_ADC; no charger, VBUS, cell-current or cell-NTC
// signal reaches the ESP32. Never infer charging from voltage or from USB CDC.
class YiyiBattery {
    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t calibration_ = nullptr;
    BatteryPolicy::Monitor policy_;
    mutable std::mutex mutex_;
    int millivolts_ = 0, percent_ = -1;
    float smoothed_ = 0;
    bool valid_ = false;

public:
    struct Reading {
        int millivolts, percent;
        bool valid;
        BatteryPolicy::State protection;
    };
    Reading Get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {millivolts_, percent_, valid_, policy_.get()};
    }
    int Measure() {
        if (!adc_ || !calibration_)
            return 0;
        int raw = 0, mv = 0, samples[9];
        adc_oneshot_read(adc_, ADC_CHANNEL_8, &raw);
        for (auto& sample : samples) {
            if (adc_oneshot_read(adc_, ADC_CHANNEL_8, &raw) != ESP_OK ||
                adc_cali_raw_to_voltage(calibration_, raw, &mv) != ESP_OK)
                return 0;
            sample = mv * 2;
        }
        std::sort(samples, samples + 9);
        return samples[4];
    }
    void Begin() {
        adc_oneshot_unit_init_cfg_t unit = {};
        unit.unit_id = ADC_UNIT_1;
        if (adc_oneshot_new_unit(&unit, &adc_) == ESP_OK) {
            adc_oneshot_chan_cfg_t channel = {};
            channel.atten = ADC_ATTEN_DB_12;
            channel.bitwidth = ADC_BITWIDTH_12;
            if (adc_oneshot_config_channel(adc_, ADC_CHANNEL_8, &channel) == ESP_OK) {
                adc_cali_curve_fitting_config_t cal = {};
                cal.unit_id = ADC_UNIT_1;
                cal.chan = ADC_CHANNEL_8;
                cal.atten = ADC_ATTEN_DB_12;
                cal.bitwidth = ADC_BITWIDTH_12;
                if (adc_cali_create_scheme_curve_fitting(&cal, &calibration_) != ESP_OK)
                    calibration_ = nullptr;
            }
        }
        Poll();
    }
    void Poll() {
        const int mv = Measure();
        const uint32_t now = uint32_t(esp_timer_get_time() / 1000);
        std::lock_guard<std::mutex> lock(mutex_);
        millivolts_ = mv;
        const bool valid = mv >= 2500 && mv <= 4450;
        if (valid) {
            smoothed_ = valid_ ? smoothed_ * .8f + (mv * .001f) * .2f : mv * .001f;
            percent_ = BatteryPolicy::estimate(smoothed_);
        } else {
            smoothed_ = 0;
            percent_ = -1;
        }
        valid_ = valid;
        policy_.update(now, mv, percent_);
    }
};
