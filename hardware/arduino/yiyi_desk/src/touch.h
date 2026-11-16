#pragma once
// FT6336G capacitive touch: hardware access, orientation, affine calibration and
// gesture recognition.
//
// The CTP reports native portrait coordinates (0..239, 0..319). Rotation into
// landscape precedes optional measured calibration; mirror modes are overrides.
//
// Navigation is deliberately NOT implemented here: the module reports gestures
// through a callback so page/app logic stays in the UI layer.
#include <lvgl.h>
#include <stdint.h>
#include "touch_transform.h"

namespace Touch {

// Landscape-space raw bounds measured by tapping the two calibration targets.
// An explicit constructor keeps this usable as an aggregate initializer under
// the C++11 dialect this core compiles with.
struct Calibration {
    int16_t xMin, xMax, yMin, yMax;
    bool valid;
    Calibration(int16_t x0 = 0, int16_t x1 = 319, int16_t y0 = 0, int16_t y1 = 239, bool ok = false)
        : xMin(x0), xMax(x1), yMin(y0), yMax(y1), valid(ok) {}
};

enum GestureKind { GestureTap = 0, GestureSwipe = 1 };

// One completed gesture, reported on release. Coordinates are landscape and
// already rotated/calibrated; x0/y0 is the press origin (not the release point),
// x1/y1 the release point and dx/dy the travel (x1-x0, y1-y0). The origin is
// cleared right after the callback returns, so this struct is the only way the
// UI layer can route an edge swipe.
struct GestureInfo {
    int kind;            // GestureKind: GestureTap or GestureSwipe
    int16_t x0, y0;      // press origin
    int16_t x1, y1;      // release point
    int16_t dx, dy;      // x1-x0, y1-y0
};
using GestureHandler = void (*)(const GestureInfo &);

// Development diagnostics for the V2 gesture acceptance scripts (see status JSON).
// `suppressClick` is the value of Touch::swiped() right after this gesture was
// classified; `swallowed` is true when a swipe arrived while a previous gesture's
// swipeSeen_ flag was still set (i.e. no fresh press reset it).
struct LastGesture {
    int kind = -1;
    int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int16_t dx = 0, dy = 0;
    bool suppressClick = false;
    bool swallowed = false;
};
const LastGesture &lastGesture();

void begin();
bool present();
void setRotationMode(uint8_t mode);
uint8_t rotationMode();

// Hardware-only observations: synthetic taps must never count as physical proof.
struct PhysicalSample {
    int16_t rawX = 0, rawY = 0, x = 0, y = 0;
    bool valid = false, pressed = false;
    uint32_t presses = 0;
};
const PhysicalSample &lastPhysical();
void setAffineCalibration(const TouchTransform::Affine &calibration);
const TouchTransform::Affine &affineCalibration();

void setCalibration(const Calibration &calibration);
const Calibration &calibration();

// Applies the affine calibration to already-rotated landscape raw coordinates.
// Exposed so the mapping can be self-tested and reused by a calibration UI.
void applyCalibration(int16_t &x, int16_t &y);

// Verifies the affine mapping, including clamping and degenerate bounds.
bool selfTest();

void setGestureHandler(GestureHandler handler);
void setCoordinateLabel(lv_obj_t *label);

// Registers read_cb on the driver. The driver must outlive setup(): LVGL 8
// retains the pointer.
void attach(lv_indev_drv_t *driver);

// True once per physical press, so callers can drive an idle timer.
bool consumeActivity();
uint32_t lastPressMs();

// True while the gesture that just ended travelled far enough that it must not
// also be treated as a click on whatever widget sits under the release point.
bool swiped();

// Called from the release read callback. LVGL already suppresses CLICKED for a
// scroll; let it also keep the last pressed object so a new press can finish
// the scroll throw. wait_release() clears that object without clearing the
// scroll object in LVGL 8.4, which can otherwise leave every later tap blocked.
inline void suppressSwipeClick(lv_indev_t *input, int dx, int dy) {
    if (input && (dx < -32 || dx > 32 || dy < -32 || dy > 32) &&
        !lv_indev_get_scroll_obj(input)) lv_indev_wait_release(input);
}

// Sends already-mapped landscape input through LVGL/gesture handling, bypassing
// hardware rotation/calibration. This cannot verify the physical CTP mapping.
void simulate(int16_t x1, int16_t y1, int16_t x2, int16_t y2);

// Live raw I2C readout, for measuring the calibration bounds by hand.
void setRawReporting(bool enabled);
bool rawReporting();

void tick();
void setBlocked(bool blocked);

}
