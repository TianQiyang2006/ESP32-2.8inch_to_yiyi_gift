#pragma once
#include <stdint.h>

namespace Board {
// The glass/CTP native geometry is portrait; the confirmed seven-page UI is landscape.
constexpr int NativeWidth = 240, NativeHeight = 320;
constexpr int Width = NativeHeight, Height = NativeWidth;
constexpr int LcdCs = 10, LcdDc = 46, LcdClock = 12;
constexpr int LcdMosi = 11, LcdMiso = 13, Backlight = 45;
// LCDWiki ILI9341 IPS profile; rotation 1 pairs with CTP x=yRaw, y=239-xRaw.
constexpr uint8_t LcdRotation = 1;
constexpr bool LcdBgr = true, LcdInverted = true;
constexpr uint8_t TouchMode = 0;
// Migrate only panel options from older builds that retained diagnostic toggles.
constexpr uint8_t PanelProfileRevision = 2;
constexpr int TouchSda = 16, TouchScl = 15, TouchReset = 18, TouchInt = 17;
constexpr uint8_t TouchAddress = 0x38;
constexpr int AudioEnable = 1; // FM8002E: LOW enables the amplifier.
constexpr int AudioMclk = 4, AudioBclk = 5, AudioOut = 8;
constexpr int AudioLrck = 7, AudioIn = 6;
constexpr int Rgb = 42;
constexpr int BatteryAdc = 9;
constexpr int Expansion[] = {2, 3, 14, 21};
constexpr int BacklightChannel = 0;
constexpr uint32_t IdleMs = 60000;
constexpr uint32_t WeatherMs = 10 * 60 * 1000, WeatherRetryMs = 60000;
constexpr const char *Version = "0.19.6-barbar-clock";
}
