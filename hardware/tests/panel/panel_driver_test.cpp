#include "Arduino.h"
#include "Wire.h"
#include "hardware/arduino/yiyi_desk/src/touch.h"
#include "hardware/arduino/yiyi_desk/desk_logic.h"
#include "hardware/arduino/yiyi_desk/board_config.h"
#include "hardware/arduino/yiyi_desk/src/transition_effect.h"
#include <algorithm>
#include <cassert>

uint32_t panelTestMillis = 1000;
PanelTestSerial Serial;
PanelTestWire Wire;
int gestures = 0;
Touch::GestureInfo lastGesture = {};
void gesture(const Touch::GestureInfo &info) { ++gestures; lastGesture = info; }

int main() {
    static_assert(Board::NativeWidth == 240 && Board::NativeHeight == 320, "native IPS glass");
    static_assert(Board::Width == 320 && Board::Height == 240, "landscape UI");
    static_assert(Board::TouchMode == 0 && Board::LcdBgr && Board::LcdInverted, "LCDWiki IPS profile");
    assert(DeskLogic::selfTest()); assert(Touch::selfTest());
    const int raw[][2] = {{0,0},{239,0},{0,319},{239,319},{101,247}};
    const int mapped[][2] = {{0,239},{0,0},{319,239},{319,0},{247,138}};
    Touch::begin(); assert(Touch::present()); Touch::setGestureHandler(gesture);
    lv_indev_drv_t driver; Touch::attach(&driver); lv_indev_data_t data;
    auto poll = [&](){ panelTestMillis += 35; driver.read_cb(&driver, &data); };
    auto release = [&](){ Wire.point(0,0,false); poll(); poll(); };
    for (int mode=0; mode<4; ++mode) {
        Touch::setRotationMode(mode);
        for (int i=0; i<5; ++i) {
            Wire.point(raw[i][0],raw[i][1],true); poll();
            const int x=(mode & 1)?319-mapped[i][0]:mapped[i][0];
            const int y=(mode & 2)?239-mapped[i][1]:mapped[i][1];
            assert(data.state==LV_INDEV_STATE_PR && data.point.x==x && data.point.y==y);
            const auto &sample=Touch::lastPhysical();
            assert(sample.valid && sample.rawX==raw[i][0] && sample.rawY==raw[i][1]);
            assert(sample.x==x && sample.y==y);
            release();
        }
    }
    assert(Touch::lastPhysical().presses==20 && !Touch::lastPhysical().pressed);
    // Live remapping under a held finger must suppress the old gesture until release.
    Touch::setRotationMode(0); Wire.point(50,100,true); poll();
    const int before=gestures; Touch::setRotationMode(2); poll();
    assert(data.state==LV_INDEV_STATE_REL); release(); assert(gestures==before);
    Wire.point(50,100,true); poll(); assert(data.point.x==100 && data.point.y==50); release();
    // Out-of-range native coordinates must never become a valid LVGL click.
    Wire.point(240,120,true); poll(); poll(); assert(data.state==LV_INDEV_STATE_REL);
    release();
    // Serial-injected landscape taps bypass CTP calibration, and cannot count as real taps.
    Touch::setCalibration(Touch::Calibration{10,300,20,220,true});
    const auto physicalBefore=Touch::lastPhysical();
    Touch::simulate(65,75,65,75); poll();
    assert(data.state==LV_INDEV_STATE_PR && data.point.x==65 && data.point.y==75);
    assert(Touch::lastPhysical().presses==physicalBefore.presses);
    assert(Touch::lastPhysical().rawX==physicalBefore.rawX);
    panelTestMillis+=400; release();
    // Five-point real raw calibration handles sign, swapped axes and scale together.
    TouchTransform::Point targets[]={{0,0,24,24},{0,0,295,24},{0,0,295,215},{0,0,24,215},{0,0,160,120}};
    for(auto &p:targets){p.rawX=(p.y-7)/0.9f;p.rawY=(310-p.x)/0.95f;}
    TouchTransform::Affine fit;assert(TouchTransform::fit(targets,4,fit));
    for(const auto &p:targets)assert(TouchTransform::error(fit,p)<0.001f);
    Touch::setRotationMode(0);Touch::setAffineCalibration(fit);
    Wire.point(80,120,true);poll();
    assert(data.point.x==196 && data.point.y==79);release();
    Touch::setRotationMode(2);Wire.point(80,120,true);poll();
    assert(data.point.x==196 && data.point.y==160);release();
    TouchTransform::Point degenerate[]={{1,1,24,24},{1,1,295,24},{1,1,295,215}};
    assert(!TouchTransform::fit(degenerate,3,fit));
    // A press during a visual transition must not click the destination on
    // unblock. Only a new press after releasing can interact with it.
    Touch::setBlocked(true);Wire.point(80,120,true);poll();assert(data.state==LV_INDEV_STATE_REL);
    const int blockedGestures=gestures;Touch::setBlocked(false);poll();assert(data.state==LV_INDEV_STATE_REL);
    release();assert(gestures==blockedGestures);Wire.point(80,120,true);poll();assert(data.state==LV_INDEV_STATE_PR);release();
    for(int direction:{-1,1}){
        uint16_t visible[320];for(auto &pixel:visible)pixel=0xf800;
        int previous=0;
        for(unsigned elapsed:{0,16,41,60,108,171,233,277}){
            int p=TransitionEffect::progress(elapsed,260);assert(p>=previous);
            for(int x=0;x<320;++x){int axis=direction>0?x:319-x;
                if(axis>=std::max(0,previous-TransitionEffect::Edge)&&axis<=std::min(319,p))visible[x]=TransitionEffect::blend(0xf800,0x001f,TransitionEffect::opacity(p,x,direction));
                assert(visible[x]==TransitionEffect::blend(0xf800,0x001f,TransitionEffect::opacity(p,x,direction)));
            }
            previous=p;
        }
        for(auto pixel:visible)assert(pixel==0x001f);
    }
    assert(TransitionEffect::blend(0xf800,0x001f,128)==0x7810);
    // Synthetic gestures must report the commanded start point (P0-3): the first
    // down sample is forced to (x1,y1), so x0==x1 and dx==x2-x1 exactly even on a
    // diagonal path; the old code reported the first interpolated sample instead.
    const int syntheticGestures = gestures;
    Touch::simulate(40, 60, 200, 100);
    poll();
    assert(data.state==LV_INDEV_STATE_PR && data.point.x==40 && data.point.y==60);
    for (int i=0; i<5; ++i) poll();  // reach the 180 ms end of the interpolation
    release();
    assert(gestures==syntheticGestures+1);
    assert(lastGesture.kind==Touch::GestureSwipe);
    assert(lastGesture.x0==40 && lastGesture.y0==60);
    assert(lastGesture.x1==200 && lastGesture.y1==100);
    assert(lastGesture.dx==160 && lastGesture.dy==40);
    std::puts("PANEL DRIVER PASS: actual touch.cpp, native corners, live mirrors, held input, physical/synthetic separation, synthetic gesture origin");
}
