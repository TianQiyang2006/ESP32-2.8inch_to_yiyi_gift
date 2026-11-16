#pragma once
// ILI9341V panel, backlight and the LVGL display driver.
//
// Native glass is 240x320; LVGL uses the 320x240 landscape address window.
#include <lvgl.h>
#include <stdint.h>

namespace Display {

// Panel/backlight options owned by the settings layer and handed in here.
struct Options {
    bool bgr = true;          // red/blue order (MADCTL BGR bit)
    bool inverted = true;     // IPS inversion
    uint8_t brightness = 180; // LEDC duty
};

// Panel bring-up, backlight, then lv_init() and the LVGL display registration.
// Returns false when the PSRAM draw buffers could not be allocated.
bool begin(const Options &options);

// Re-applies MADCTL, inversion and brightness, then invalidates the active screen.
void apply(const Options &options);

void setBrightness(uint8_t value);

// True once the LVGL display driver is registered and the frame mirror exists.
bool ready();

// Capture the old pixels, build the destination offscreen, then reveal it using
// narrow SPI bands. The widgets stay at their final coordinates throughout.
// The reveal is an lv_anim timeline (value = elapsed ms); LVGL timers step it via
// lv_timer_handler(), and pollTransition() only keeps host tests/watchdogs alive.
bool beginTransition(int direction);
void startTransition(uint32_t duration=260);
void pollTransition();
bool transitioning();
void transitionStats(uint32_t &frames,uint32_t &duration);
// Milliseconds spent actively pushing pixels during the last reveal (the rest
// of the duration is the paused animation curve, not bus/CPU work).
uint32_t transitionPushMs();

// Cumulative time spent transferring pixel regions to the LCD.
void panelPushStats(uint32_t &lastUs, uint32_t &maxUs, uint64_t &totalUs, uint32_t &calls);

struct FrameStats {
    uint32_t flushRegions=0,commits=0,lastRegions=0,maxRegions=0;
    uint32_t lastPatches=0,lastPixels=0,lastDirtyPixels=0,lastCommitUs=0,maxCommitUs=0;
    uint32_t lastRenderUs=0,presentedFrames=0,drawBufferPixels=0;
    uint64_t totalCommitUs=0,totalPixels=0,totalDirtyPixels=0,totalRenderUs=0;
    bool differenceReady=false,internalDraw=false;
};
void frameStats(FrameStats &stats);
// Read-only scan-counter capability check; does not alter panel configuration.
void probeScanline();

// Renders the active screen and streams it as a CRC-checked frame over Serial.
// The frame is snapshotted and sent in chunks with lv_timer_handler() between
// chunks, so the UI is not frozen for the whole transfer.
// Returns true only when every byte was accepted. Caller serialises IO access.
bool sendFrame();

// Rendered frames per second over the last completed one-second window
// (LVGL flush callbacks), 0 until the first window closes. Used by `status`.
uint32_t framesPerSecond();

}
