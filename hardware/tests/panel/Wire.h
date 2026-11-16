#pragma once
#include <cstdint>
struct PanelTestWire {
    uint8_t bytes[5] = {};
    int offset = 0;
    void begin(int, int, int) {}
    void setTimeOut(int) {}
    void beginTransmission(uint8_t) {}
    void write(int) {}
    int endTransmission(bool = true) { return 0; }
    int requestFrom(uint8_t, uint8_t count) { offset = 0; return count; }
    int available() { return 5-offset; }
    int read() { return bytes[offset++]; }
    void point(int x, int y, bool down) {
        bytes[0] = down ? 1 : 0;
        bytes[1] = (x >> 8) & 15; bytes[2] = x & 255;
        bytes[3] = (y >> 8) & 15; bytes[4] = y & 255;
    }
};
extern PanelTestWire Wire;
