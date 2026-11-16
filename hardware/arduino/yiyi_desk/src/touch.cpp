#include "touch.h"
#include "../board_config.h"
#include "../desk_logic.h"
#include <Arduino.h>
#include <Wire.h>

namespace Touch {
namespace {
bool blocked_=false;

Calibration calibration_;
TouchTransform::Affine affine_;
GestureHandler gestureHandler_ = nullptr;
lv_obj_t *coordinateLabel_ = nullptr;

uint8_t rotationMode_ = 0;
PhysicalSample physical_;
bool suppressUntilRelease_ = false;
bool present_ = false;
bool activity_ = false;
bool rawReporting_ = false;

int16_t pressX_ = -1, pressY_ = -1, lastX_ = 0, lastY_ = 0;
uint8_t releaseFailures_ = 0;
bool swipeSeen_ = false;
uint32_t lastPressMs_ = 0;
LastGesture lastGesture_;

int16_t simulatedX1_ = 0, simulatedY1_ = 0, simulatedX2_ = 0, simulatedY2_ = 0;
uint32_t simulatedAt_ = 0;

// Maps one axis of the landscape raw value onto 0..max using the calibrated
// extremes. Falls back to the raw value when the calibration is unusable.
int16_t scaleAxis(int16_t value, int16_t low, int16_t high, int16_t max) {
    if (high <= low) return value;
    int32_t scaled = (int32_t)(value - low) * max / (high - low);
    if (scaled < 0) scaled = 0;
    if (scaled > max) scaled = max;
    return (int16_t)scaled;
}

// Reads the panel and returns landscape coordinates after rotation + calibration.
bool readLandscape(int16_t &x, int16_t &y, int16_t *rawX = nullptr, int16_t *rawY = nullptr, bool *synthetic = nullptr) {
    if (synthetic) *synthetic = simulatedAt_ != 0;
    if (simulatedAt_) {
        uint32_t elapsed = millis() - simulatedAt_;
        if (elapsed >= 350) { simulatedAt_ = 0; if (synthetic) *synthetic = false; }
        else if (elapsed < 220) {
            int progress = min<uint32_t>(elapsed, 180);
            x = simulatedX1_ + (simulatedX2_ - simulatedX1_) * progress / 180;
            y = simulatedY1_ + (simulatedY2_ - simulatedY1_) * progress / 180;
            if (rawX) *rawX = x;
            if (rawY) *rawY = y;
            return true;
        } else {
            return false;
        }
    }
    Wire.beginTransmission(Board::TouchAddress); Wire.write(0x02);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(Board::TouchAddress, (uint8_t)5) != 5) {
        while (Wire.available()) Wire.read();
        return false;
    }
    uint8_t count = Wire.read() & 0x0F;
    uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
    if (!count || count > 2 || (xh >> 6) == 1) { physical_.pressed = false; return false; }
    int panelX = ((xh & 15) << 8) | xl;
    int panelY = ((yh & 15) << 8) | yl;
    if (rawX) *rawX = (int16_t)panelX;
    if (rawY) *rawY = (int16_t)panelY;
    bool mapped=false;
    if(affine_.valid) {
        mapped=TouchTransform::map(affine_,panelX,panelY,x,y);
        if(mapped){if(rotationMode_&1)x=Board::Width-1-x;if(rotationMode_&2)y=Board::Height-1-y;}
    } else {
        mapped=DeskLogic::touchToLandscape(panelX,panelY,rotationMode_,x,y);
        if(mapped)applyCalibration(x,y);
    }
    if(!mapped){physical_.pressed=false;return false;}
    if (!physical_.pressed) ++physical_.presses;
    physical_.rawX = panelX; physical_.rawY = panelY;
    physical_.x = x; physical_.y = y;
    physical_.valid = true; physical_.pressed = true;
    return true;
}

void readCallback(lv_indev_drv_t *, lv_indev_data_t *data) {
    int16_t x, y, rawX = 0, rawY = 0;
    bool synthetic = false;
    bool down = readLandscape(x, y, &rawX, &rawY, &synthetic);
    if(blocked_){
        if(down){activity_=true;suppressUntilRelease_=true;}
        pressX_=pressY_=-1;releaseFailures_=0;swipeSeen_=false;
        data->point.x=lastX_;data->point.y=lastY_;data->state=LV_INDEV_STATE_REL;return;
    }
    if (suppressUntilRelease_) {
        if (!down) suppressUntilRelease_ = false;
        data->point.x = lastX_; data->point.y = lastY_;
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (down) {
        releaseFailures_ = 0;
        activity_ = true;
        lastPressMs_ = millis();
        if (pressX_ < 0) {
            // A synthesized gesture must report the commanded start point, not the
            // first interpolated sample: the edge-swipe contract (x0 <= 19) is
            // unverifiable on device otherwise. See simulate().
            if (synthetic) { x = simulatedX1_; y = simulatedY1_; }
            pressX_ = x; pressY_ = y; swipeSeen_ = false;
            if (rawReporting_ && !synthetic) Serial.printf("RAW %d %d MAP %d %d MODE %u\n", rawX, rawY, x, y, (unsigned)rotationMode_);
        }
        lastX_ = x; lastY_ = y;
        data->point.x = x; data->point.y = y; data->state = LV_INDEV_STATE_PR;
        if (coordinateLabel_) lv_label_set_text_fmt(coordinateLabel_, "触摸：%d, %d", x, y);
        return;
    }
    // Require two release samples to tolerate a transient I2C read failure.
    if (pressX_ >= 0 && ++releaseFailures_ < 2) {
        data->point.x = lastX_; data->point.y = lastY_; data->state = LV_INDEV_STATE_PR;
        return;
    }
    data->point.x = lastX_; data->point.y = lastY_; data->state = LV_INDEV_STATE_REL;
    if (pressX_ >= 0) {
        int dx = lastX_ - pressX_, dy = lastY_ - pressY_;
        // Fill the report (including the origin) before the reset below clears it.
        GestureInfo info;
        info.x0 = pressX_; info.y0 = pressY_;
        info.x1 = lastX_; info.y1 = lastY_;
        info.dx = (int16_t)dx; info.dy = (int16_t)dy;
        const bool swallowed = swipeSeen_;
        int kind = -1;
        if (!swipeSeen_ && abs(dx) > 45 && abs(dx) > abs(dy) * 3 / 2) {
            swipeSeen_ = true;
            info.kind = GestureSwipe;
            kind = GestureSwipe;
            if (gestureHandler_) gestureHandler_(info);
        } else if (!swipeSeen_ && abs(dx) <= 45 && gestureHandler_) {
            info.kind = GestureTap;
            kind = GestureTap;
            gestureHandler_(info);
        }
        // Any sizeable travel must suppress the click that LVGL would otherwise
        // deliver at the release point.
        if (abs(dx) > 32 || abs(dy) > 32) swipeSeen_ = true;
        lastGesture_.kind = kind;
        lastGesture_.x0 = info.x0; lastGesture_.y0 = info.y0;
        lastGesture_.x1 = info.x1; lastGesture_.y1 = info.y1;
        lastGesture_.dx = info.dx; lastGesture_.dy = info.dy;
        lastGesture_.suppressClick = swipeSeen_;
        lastGesture_.swallowed = swallowed;
        pressX_ = -1;
    }
}

}  // namespace

void begin() {
    Wire.begin(Board::TouchSda, Board::TouchScl, 400000);
    Wire.setTimeOut(10);
    pinMode(Board::TouchReset, OUTPUT);
    digitalWrite(Board::TouchReset, LOW);
    delay(10);
    digitalWrite(Board::TouchReset, HIGH);
    delay(200);
    pinMode(Board::TouchInt, INPUT_PULLUP);
    // Polling avoids depending on interrupt edge configuration across FT6336G revisions.
    Wire.beginTransmission(Board::TouchAddress);
    present_ = Wire.endTransmission() == 0;
    Serial.println(present_ ? "FT6336G detected at 0x38" : "FT6336G not responding");
}

bool present() { return present_; }
void setRotationMode(uint8_t mode) {
    mode %= 4;
    if (mode == rotationMode_) return;
    // Changing a map while pressed must not become a swipe or a click elsewhere.
    suppressUntilRelease_ = pressX_ >= 0;
    pressX_ = pressY_ = -1; releaseFailures_ = 0; swipeSeen_ = false;
    rotationMode_ = mode;
}
uint8_t rotationMode() { return rotationMode_; }
const PhysicalSample &lastPhysical() { return physical_; }
const LastGesture &lastGesture() { return lastGesture_; }
void setAffineCalibration(const TouchTransform::Affine &calibration) {
    suppressUntilRelease_=pressX_>=0;
    pressX_=pressY_=-1;releaseFailures_=0;swipeSeen_=false;
    affine_=calibration;if(!TouchTransform::finite(affine_))affine_.valid=false;
}
const TouchTransform::Affine &affineCalibration() { return affine_; }

void setCalibration(const Calibration &calibration) {
    if (calibration.xMin!=calibration_.xMin || calibration.xMax!=calibration_.xMax ||
        calibration.yMin!=calibration_.yMin || calibration.yMax!=calibration_.yMax || calibration.valid!=calibration_.valid) {
        suppressUntilRelease_ = pressX_ >= 0;
        pressX_ = pressY_ = -1; releaseFailures_ = 0; swipeSeen_ = false;
    }
    calibration_ = calibration;
}
const Calibration &calibration() { return calibration_; }

void applyCalibration(int16_t &x, int16_t &y) {
    if (!calibration_.valid) return;
    x = scaleAxis(x, calibration_.xMin, calibration_.xMax, Board::Width - 1);
    y = scaleAxis(y, calibration_.yMin, calibration_.yMax, Board::Height - 1);
}

bool selfTest() {
    const Calibration saved = calibration_;
    bool ok = true;

    // Identity bounds must leave the coordinate untouched.
    calibration_ = Calibration{};
    calibration_.valid = true;
    int16_t x = 137, y = 88;
    applyCalibration(x, y);
    ok &= (x == 137 && y == 88);

    // Measured bounds must stretch to the full screen.
    calibration_ = Calibration{10, 300, 20, 220, true};
    x = 10; y = 20; applyCalibration(x, y);
    ok &= (x == 0 && y == 0);
    x = 300; y = 220; applyCalibration(x, y);
    ok &= (x == 319 && y == 239);

    // Points outside the measured range clamp instead of overflowing.
    x = -40; y = 400; applyCalibration(x, y);
    ok &= (x == 0 && y == 239);

    // Degenerate bounds must not divide by zero or move the point.
    calibration_ = Calibration{50, 50, 60, 60, true};
    x = 123; y = 45; applyCalibration(x, y);
    ok &= (x == 123 && y == 45);

    // An invalid calibration is a no-op.
    calibration_ = Calibration{};
    x = 200; y = 100; applyCalibration(x, y);
    ok &= (x == 200 && y == 100);

    calibration_ = saved;
    return ok;
}

void setGestureHandler(GestureHandler handler) { gestureHandler_ = handler; }
void setCoordinateLabel(lv_obj_t *label) { coordinateLabel_ = label; }

void attach(lv_indev_drv_t *driver) {
    lv_indev_drv_init(driver);
    driver->type = LV_INDEV_TYPE_POINTER;
    driver->read_cb = readCallback;
}

bool consumeActivity() { bool value = activity_; activity_ = false; return value; }
uint32_t lastPressMs() { return lastPressMs_; }
bool swiped() { return swipeSeen_; }

void simulate(int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    // A test tool may issue the next synthetic gesture before the previous one has
    // released (e.g. tap 280 105 followed immediately by swipe ... after a page
    // command). Without this reset both gestures merge into one drag whose press
    // origin is the old tap, which made the swipe disappear and let LVGL deliver a
    // stale CLICKED. Real touch input can never do this; the reset only applies to
    // the serial test path.
    if (simulatedAt_ != 0) {
        pressX_ = pressY_ = -1;
        releaseFailures_ = 0;
        swipeSeen_ = false;
    }
    simulatedX1_ = x1; simulatedY1_ = y1; simulatedX2_ = x2; simulatedY2_ = y2;
    simulatedAt_ = millis();
}

void setRawReporting(bool enabled) { rawReporting_ = enabled; }
bool rawReporting() { return rawReporting_; }

void tick() {}
void setBlocked(bool blocked){blocked_=blocked;}

}
