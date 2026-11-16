#pragma once
// Hardware substitutes only: tests compile the firmware's actual audio_board.cpp.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
using String = std::string;
constexpr int HIGH=1, LOW=0, OUTPUT=1, ESP_OK=0;
namespace AudioTest {
inline uint8_t registers[256]{};
inline std::vector<int16_t> output;
inline std::vector<std::string> events;
inline bool failRegister=false, failClock=false, failWrite=false, shortWrite=false;
inline int amplifier=HIGH;
inline unsigned rate=0;
}
inline void pinMode(int,int){}
inline void digitalWrite(int,int level){AudioTest::amplifier=level;AudioTest::events.push_back(level?"mute":"unmute");}
inline void delay(unsigned){}
template<class T,class A,class B>T constrain(T v,A a,B b){return std::max<T>(a,std::min<T>(b,v));}
struct TestWire {
    unsigned address=0, count=0;uint8_t bytes[2]{};
    void beginTransmission(unsigned a){address=a;count=0;}
    void write(uint8_t b){if(count<2)bytes[count++]=b;}
    int endTransmission(){
        if(AudioTest::failRegister)return 1;
        if(count==2){AudioTest::registers[bytes[0]]=bytes[1];AudioTest::events.push_back("register");}
        return 0;
    }
};
inline TestWire Wire;
using TaskHandle_t=void*;
using SemaphoreHandle_t=void*;
using portMUX_TYPE=int;
constexpr int portMUX_INITIALIZER_UNLOCKED=0, portMAX_DELAY=-1, pdTRUE=1, pdPASS=1;
inline unsigned pdMS_TO_TICKS(unsigned n){return n;}
inline void portENTER_CRITICAL(portMUX_TYPE*){}
inline void portEXIT_CRITICAL(portMUX_TYPE*){}
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
inline int xSemaphoreTake(SemaphoreHandle_t,int){return pdTRUE;}
inline void xSemaphoreGive(SemaphoreHandle_t){}
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,int,TaskHandle_t *task,int){*task=reinterpret_cast<void*>(2);return pdPASS;}
inline void xTaskNotifyGive(TaskHandle_t){}
inline void vTaskDelay(unsigned){}
inline void ulTaskNotifyTake(int,int){}
using i2s_mode_t=int;
constexpr int I2S_NUM_0=0,I2S_MODE_MASTER=1,I2S_MODE_TX=2,I2S_BITS_PER_SAMPLE_16BIT=16;
constexpr int I2S_CHANNEL_FMT_RIGHT_LEFT=0,I2S_COMM_FORMAT_STAND_I2S=0,ESP_INTR_FLAG_LEVEL1=1;
constexpr int I2S_MCLK_MULTIPLE_256=256,I2S_PIN_NO_CHANGE=-1;
struct i2s_config_t {
    int mode,sample_rate,bits_per_sample,channel_format,communication_format,intr_alloc_flags;
    int dma_buf_count,dma_buf_len;bool use_apll,tx_desc_auto_clear;int fixed_mclk,mclk_multiple;
};
struct i2s_pin_config_t {int mck_io_num,bck_io_num,ws_io_num,data_out_num,data_in_num;};
inline int i2s_driver_install(int,const i2s_config_t *c,int,void*){AudioTest::rate=c->sample_rate;return ESP_OK;}
inline void i2s_driver_uninstall(int){}
inline int i2s_set_pin(int,const i2s_pin_config_t*){return ESP_OK;}
inline void i2s_zero_dma_buffer(int){AudioTest::events.push_back("clear");}
inline int i2s_set_sample_rates(int,unsigned rate){
    AudioTest::events.push_back("clock");
    if(AudioTest::failClock)return -1;
    AudioTest::rate=rate;return ESP_OK;
}
inline int i2s_write(int,const void *data,size_t bytes,size_t *written,unsigned){
    const auto *pcm=static_cast<const int16_t*>(data);
    AudioTest::output.assign(pcm,pcm+bytes/2);*written=AudioTest::shortWrite?bytes/2:bytes;return AudioTest::failWrite?-1:ESP_OK;
}
