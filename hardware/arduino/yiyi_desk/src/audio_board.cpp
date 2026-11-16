#include "audio_board.h"
#include "../board_config.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <math.h>
#include <esp_timer.h>
#include "tone_synth.h"

namespace AudioBoard {
namespace {
constexpr int Rate = 16000, Frame = 960;
unsigned outputRate=Rate;
constexpr uint32_t TaskStackBytes = 6144;
TaskHandle_t audioTask = nullptr;
SemaphoreHandle_t outputLock = nullptr;
SemaphoreHandle_t mediaControlLock = nullptr;
std::atomic<int> volume{45};
std::atomic<bool> playing_{false}, outputFault{false};
std::atomic<StreamSource> streamSource{StreamSource::Music};
bool ready_ = false;
String error_;
portMUX_TYPE controlMux=portMUX_INITIALIZER_UNLOCKED;
struct Request {unsigned tone=0,percent=0,priority=0,fade=0;uint32_t duration=0,generation=0;bool knock=false;Reminder source=Reminder::Alarm;};
Request request;
std::atomic<bool> soundAllowed{true};
std::atomic<unsigned> volumeCap{100};
std::atomic<unsigned> alarmMaster{20},pomodoroMaster{20};
bool reg(uint8_t address,uint8_t value){Wire.beginTransmission(0x18);Wire.write(address);Wire.write(value);return Wire.endTransmission()==0;}
bool codec(){
 Wire.beginTransmission(0x18);if(Wire.endTransmission()!=0){error_="ES8311 未响应，请检查音频电源";return false;}
 // Clock coefficients: 4096000 MCLK / 16000 Hz, from Espressif's ES8311 driver.
 const uint8_t settings[][2]={{0x00,0x1f},{0x00,0},{0x00,0x80},{0x01,0x3f},{0x02,0},{0x03,0x10},{0x04,0x20},
  {0x05,0},{0x06,3},{0x07,0},{0x08,0xff},{0x09,0x0c},{0x0a,0x0c},{0x0d,1},{0x0e,2},{0x12,0},{0x13,0x10},
  {0x1c,0x6a},{0x37,8},{0x14,0x1a},{0x17,0xc8},{0x31,0},
  // ES8311 DAC volume is -95.5 + register * 0.5 dB. 0xff adds 32 dB
  // AFTER software volume, clipping even a 20% music signal. 0xbf is unity.
  {0x32,0xbf}};
 for(size_t i=0;i<sizeof(settings)/sizeof(settings[0]);i++){if(!reg(settings[i][0],settings[i][1])){error_="ES8311 初始化失败";return false;}if(i==0)delay(20);}
 return true;
}
// Called with outputLock held. The dividers stay at 256x MCLK, but the
// ES8311 DAC oversampling ratio must follow the sample rate too.
bool setRate(unsigned rate){
 if(outputRate==rate)return true;
 digitalWrite(Board::AudioEnable,HIGH);
 i2s_zero_dma_buffer(I2S_NUM_0);
 if(i2s_set_sample_rates(I2S_NUM_0,rate)!=ESP_OK){outputFault=true;return false;}
 if(!reg(0x04,rate==16000?0x20:0x10)){outputFault=true;return false;}
 outputRate=rate;return true;
}

bool writeKnock() {
    xSemaphoreTake(outputLock,portMAX_DELAY);
    if(!setRate(Rate)){xSemaphoreGive(outputLock);return false;}
    int16_t stereo[320];
    int out = 0;
    digitalWrite(Board::AudioEnable, volume.load() > 0 ? LOW : HIGH);
    for (int i = 0; i < Frame; ++i) {
        const float wave = 5500 * expf(-i / 140.f) * (sinf(i * .55f) + .3f * sinf(i * 1.2f));
        const int16_t sample = static_cast<int16_t>(wave) * volume.load() / 100;
        stereo[out++] = sample;
        stereo[out++] = sample;
        if (out == 320) {
            size_t written = 0;
            if (i2s_write(I2S_NUM_0, stereo, sizeof(stereo), &written, pdMS_TO_TICKS(100)) != ESP_OK || written != sizeof(stereo)){xSemaphoreGive(outputLock);return false;}
            out = 0;
        }
    }
    xSemaphoreGive(outputLock);return true;
}
void worker(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Request current;uint64_t index=0;
        for(;;){
            Request next;portENTER_CRITICAL(&controlMux);next=request;portEXIT_CRITICAL(&controlMux);
            if(next.generation!=current.generation){current=next;index=0;}
            if(!current.priority)break;
            if(current.knock){if(!writeKnock())outputFault=true;vTaskDelay(pdMS_TO_TICKS(65));break;}
            if(index>=uint64_t(current.duration)*16)break;
            unsigned gain=current.percent,cap=volumeCap.load();if(gain>cap)gain=cap;
            if(!soundAllowed.load())gain=0;
            unsigned master=current.source==Reminder::Timer?100:current.source==Reminder::Pomodoro?pomodoroMaster.load():alarmMaster.load();
            int16_t stereo[320];
            for(unsigned i=0;i<160;++i){int16_t sample=ToneSynth::applyMaster(ToneSynth::pcm(current.tone,index++,gain,current.fade),master);stereo[i*2]=stereo[i*2+1]=sample;}
            size_t written=0;
            xSemaphoreTake(outputLock,portMAX_DELAY);
            bool ok=setRate(Rate);
            if(ok){digitalWrite(Board::AudioEnable,gain&&master?LOW:HIGH);ok=i2s_write(I2S_NUM_0,stereo,sizeof(stereo),&written,pdMS_TO_TICKS(100))==ESP_OK&&written==sizeof(stereo);}
            xSemaphoreGive(outputLock);
            if(!ok){outputFault=true;break;}
        }
        xSemaphoreTake(outputLock,portMAX_DELAY);
        i2s_zero_dma_buffer(I2S_NUM_0);
        digitalWrite(Board::AudioEnable, HIGH);
        portENTER_CRITICAL(&controlMux);
        if(request.generation==current.generation){request.priority=0;playing_=false;}
        else xTaskNotifyGive(audioTask);
        portEXIT_CRITICAL(&controlMux);
        xSemaphoreGive(outputLock);
    }
}
}
bool begin() {
    if (ready_) return true;
    if(!outputLock)outputLock=xSemaphoreCreateMutex();
    if(!mediaControlLock)mediaControlLock=xSemaphoreCreateMutex();
    if(!outputLock||!mediaControlLock){error_="无法创建音频锁";return false;}
    error_ = "";
    outputFault = false;
    pinMode(Board::AudioEnable, OUTPUT);
    digitalWrite(Board::AudioEnable, HIGH);
    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = Rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = 160;
    cfg.use_apll = true;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = 0;
    cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = Board::AudioMclk;
    pins.bck_io_num = Board::AudioBclk;
    pins.ws_io_num = Board::AudioLrck;
    pins.data_out_num = Board::AudioOut;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        error_ = "I2S 初始化失败";
        return false;
    }
    const auto cleanup = []() {
        i2s_driver_uninstall(I2S_NUM_0);
        digitalWrite(Board::AudioEnable, HIGH);
    };
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        error_ = "I2S 引脚初始化失败";
        cleanup();
        return false;
    }
    if (!codec()) { cleanup(); return false; }
    i2s_zero_dma_buffer(I2S_NUM_0);
    if (xTaskCreatePinnedToCore(worker, "gift-audio", TaskStackBytes, nullptr, 3, &audioTask, 0) != pdPASS) {
        error_ = "无法启动音频任务";
        cleanup();
        return false;
    }
    ready_ = true;
    return true;
}
void setVolume(int percent) {
    volume = constrain(percent, 0, 100);
    if (!volume.load()&&!playing_.load()) digitalWrite(Board::AudioEnable, HIGH);
}
void knock() {
    if (!ready_ || !volume.load()) return;
    portENTER_CRITICAL(&controlMux);
    if(!request.priority){request.knock=true;request.priority=1;request.percent=volume.load();++request.generation;playing_=true;xTaskNotifyGive(audioTask);}
    portEXIT_CRITICAL(&controlMux);
}
bool ringtone(unsigned tone,unsigned percent,uint32_t durationMs,unsigned priority,unsigned fadeMs,Reminder source){
    if(!ready_||outputFault.load()||tone>2||priority<1||priority>3||!durationMs)return false;
    bool accepted=false;portENTER_CRITICAL(&controlMux);
    if(priority>=request.priority){request.knock=false;request.source=source;request.tone=tone;request.percent=percent>100?100:percent;request.duration=durationMs;request.priority=priority;request.fade=fadeMs;++request.generation;playing_=true;accepted=true;xTaskNotifyGive(audioTask);}
    portEXIT_CRITICAL(&controlMux);return accepted;
}
void setReminderVolume(Reminder source,unsigned percent){if(source==Reminder::Timer)return;if(percent>100)percent=100;if(source==Reminder::Pomodoro)pomodoroMaster=percent;else alarmMaster=percent;}
void stopRingtone(unsigned priority){portENTER_CRITICAL(&controlMux);if(request.priority==priority){request.priority=0;++request.generation;xTaskNotifyGive(audioTask);}portEXIT_CRITICAL(&controlMux);}
void stopRingtone(unsigned priority,Reminder source){portENTER_CRITICAL(&controlMux);if(request.priority==priority&&request.source==source){request.priority=0;++request.generation;xTaskNotifyGive(audioTask);}portEXIT_CRITICAL(&controlMux);}
void protect(bool allowed,unsigned cap){soundAllowed=allowed;volumeCap=cap>100?100:cap;}
bool playing() { return playing_.load(); }
void selectStream(StreamSource source){streamSource=source;}
bool streamSelected(StreamSource source){return streamSource.load()==source;}
bool lockMediaControl(){return mediaControlLock&&xSemaphoreTake(mediaControlLock,pdMS_TO_TICKS(250))==pdTRUE;}
void unlockMediaControl(){if(mediaControlLock)xSemaphoreGive(mediaControlLock);}
static bool streamFrom(StreamSource source,const int16_t *samples,unsigned count,unsigned percent,unsigned rate){
 if(!ready_||outputFault.load()||playing_.load()||!samples||!count||count>256||(rate!=16000&&rate!=32000))return false;
 if(streamSource.load()!=source)return false;
 if(xSemaphoreTake(outputLock,pdMS_TO_TICKS(30))!=pdTRUE)return false;
 if(playing_.load()||streamSource.load()!=source){xSemaphoreGive(outputLock);return false;}
 if(!setRate(rate)){xSemaphoreGive(outputLock);return false;}
 int16_t stereo[512];percent=percent>100?100:percent;
 if(!soundAllowed.load())percent=0;if(percent>volumeCap.load())percent=volumeCap.load();
 for(unsigned i=0;i<count;++i){int16_t v=ToneSynth::applyMaster(samples[i],percent);stereo[i*2]=stereo[i*2+1]=v;}
 digitalWrite(Board::AudioEnable,percent?LOW:HIGH);size_t written=0;
 bool ok=i2s_write(I2S_NUM_0,stereo,count*4,&written,pdMS_TO_TICKS(100))==ESP_OK&&written==count*4;
 if(!ok){outputFault=true;digitalWrite(Board::AudioEnable,HIGH);}
 xSemaphoreGive(outputLock);return ok;
}
bool streamPcm(const int16_t *samples,unsigned count,unsigned percent,unsigned rate){return streamFrom(StreamSource::Music,samples,count,percent,rate);}
bool streamRadioPcm(const int16_t *samples,unsigned count,unsigned percent,unsigned rate){return streamFrom(StreamSource::Radio,samples,count,percent,rate);}
static void endStreamFrom(StreamSource source){
 if(!ready_||!outputLock)return;
 if(xSemaphoreTake(outputLock,pdMS_TO_TICKS(100))==pdTRUE){
  if(!playing_.load()&&streamSource.load()==source){i2s_zero_dma_buffer(I2S_NUM_0);digitalWrite(Board::AudioEnable,HIGH);}
  xSemaphoreGive(outputLock);
 }
}
void endStream(){endStreamFrom(StreamSource::Music);}
void endRadioStream(){endStreamFrom(StreamSource::Radio);}
bool ready() { return ready_; }
String error() { return outputFault.load() ? String("音频输出失败") : error_; }
}
