#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>
#define OUTPUT 1
#define LOW 0
extern uint32_t displayTestMillis;
inline uint32_t millis(){return displayTestMillis;}
inline uint32_t micros(){static uint32_t calls=0;return displayTestMillis*1000u+calls++;}
inline void delay(unsigned ms){displayTestMillis+=ms;}
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
inline void ledcSetup(int,int,int){}
inline void ledcAttachPin(int,int){}
inline void ledcWrite(int,int){}
struct DisplayTestSerial {
    std::string text;
    std::vector<uint8_t> bytes;
    void print(const char *value){text+=value;}
    void print(char value){text+=value;}
    void println(const char *value){text+=value;text+='\n';}
    template<class... Args>void printf(const char *format,Args... values){
        char buffer[256];std::snprintf(buffer,sizeof(buffer),format,values...);text+=buffer;
    }
    size_t write(const uint8_t *data,size_t size){bytes.insert(bytes.end(),data,data+size);return size;}
    void clear(){text.clear();bytes.clear();}
    void flush(){}
};
extern DisplayTestSerial Serial;
