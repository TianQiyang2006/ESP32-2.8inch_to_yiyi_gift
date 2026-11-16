#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#define OUTPUT 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1
using std::min;
extern uint32_t panelTestMillis;
inline uint32_t millis() { return panelTestMillis; }
inline void delay(uint32_t ms) { panelTestMillis += ms; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
struct PanelTestSerial {
    void println(const char *) {}
    void printf(const char *, ...) {}
};
extern PanelTestSerial Serial;
