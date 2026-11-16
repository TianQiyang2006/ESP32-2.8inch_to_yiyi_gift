#include "Arduino.h"
#include "hardware/arduino/yiyi_desk/src/display.h"
#include <cassert>
#include <cstdio>
#include <vector>

uint32_t displayTestMillis=1000;
uint16_t displayedPixels[320*240];unsigned transferredPixels=0;
unsigned displayTestTransactions=0,displayTestEnds=0;
bool displayTestFailInternal=false;
DisplayTestSerial Serial;
static unsigned checks=0,observedDraws=0;
static bool observing=false;
static std::vector<uint16_t> beforeDraw;
static void check(bool value){++checks;assert(value);}
static void fill(uint32_t color){lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(color),0);lv_obj_invalidate(lv_scr_act());}
static void same(const std::vector<uint16_t> &expected){
    check(expected.size()==320*240);
    for(unsigned i=0;i<expected.size();++i)check(displayedPixels[i]==expected[i]);
}
static void uniform(uint16_t expected){for(auto pixel:displayedPixels)check(pixel==expected);}
static Display::FrameStats stats(){Display::FrameStats value;Display::frameStats(value);return value;}
static unsigned pushCalls(){uint32_t last,max,calls;uint64_t total;Display::panelPushStats(last,max,total,calls);return calls;}
static void noChangeRefresh(){
    const unsigned pixels=transferredPixels,calls=pushCalls(),transactions=displayTestTransactions;
    lv_obj_invalidate(lv_scr_act());lv_refr_now(nullptr);
    check(transferredPixels==pixels);check(pushCalls()==calls);
    check(stats().lastPixels==0);check(stats().lastPatches==0);
    check(displayTestTransactions==transactions);check(displayTestTransactions==displayTestEnds);
}
static void observeDraw(lv_event_t *){
    if(!observing)return;
    ++observedDraws;
    // Actual LVGL draw events for separate regions must still see the old LCD.
    same(beforeDraw);
}
static lv_obj_t *patch(int x,int y,int w,int h,uint32_t color,bool observe=false){
    auto *object=lv_obj_create(lv_scr_act());lv_obj_remove_style_all(object);
    lv_obj_set_pos(object,x,y);lv_obj_set_size(object,w,h);
    lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(object,lv_color_hex(color),0);
    if(observe)lv_obj_add_event_cb(object,observeDraw,LV_EVENT_DRAW_MAIN,nullptr);
    return object;
}
static void expectedPatch(std::vector<uint16_t> &image,int x,int y,int w,int h,uint16_t color){
    for(int row=y;row<y+h;++row)for(int column=x;column<x+w;++column)image[row*320+column]=color;
}
static void captureMatchesPanel(){
    Serial.clear();check(Display::sendFrame());
    check(Serial.bytes.size()==320*240*2);
    for(unsigned i=0;i<320*240;++i)
        check(uint16_t(Serial.bytes[i*2]|(Serial.bytes[i*2+1]<<8))==displayedPixels[i]);
    check(Serial.text.find("FRAME 320 240 153600 ")!=std::string::npos);
    check(Serial.text.find("\nEND")!=std::string::npos);
}

int main(int argc,char **argv){
    displayTestFailInternal=argc>1&&!std::strcmp(argv[1],"--fail-internal");
    check(Display::begin(Display::Options{}));
    fill(0xff0000);lv_refr_now(nullptr);uniform(0xf800);
    check(stats().differenceReady);check(stats().internalDraw==!displayTestFailInternal);
    check(stats().drawBufferPixels==(displayTestFailInternal?320*240:320*24));noChangeRefresh();

    // Three true disjoint invalid areas expose old immediate-flush behavior:
    // the first object's new pixels appear while the next object is drawing.
    auto *a=patch(10,20,18,18,0xff0000,true);
    auto *b=patch(145,110,18,18,0xff0000,true);
    auto *c=patch(290,205,18,18,0xff0000,true);
    lv_refr_now(nullptr);uniform(0xf800);
    beforeDraw.assign(displayedPixels,displayedPixels+320*240);
    const auto initial=stats();const unsigned initialPixels=transferredPixels,initialTransactions=displayTestTransactions;
    observing=true;
    lv_obj_set_style_bg_color(a,lv_color_hex(0x00ff00),0);
    lv_obj_set_style_bg_color(b,lv_color_hex(0xffff00),0);
    lv_obj_set_style_bg_color(c,lv_color_hex(0x0000ff),0);
    lv_refr_now(nullptr);observing=false;
    check(observedDraws>=3);check(stats().lastRegions>=3);
    check(stats().commits==initial.commits+1);
    check(stats().presentedFrames==initial.presentedFrames+1);
    check(displayTestTransactions==initialTransactions+1);check(displayTestTransactions==displayTestEnds);
    check(stats().flushRegions>=initial.flushRegions+3);
    check(transferredPixels-initialPixels<320*240/10);
    auto expected=beforeDraw;
    expectedPatch(expected,10,20,18,18,0x07e0);
    expectedPatch(expected,145,110,18,18,0xffe0);
    expectedPatch(expected,290,205,18,18,0x001f);same(expected);
    check(stats().lastPixels==transferredPixels-initialPixels);
    noChangeRefresh();captureMatchesPanel();noChangeRefresh();
    // Force a full-screen dirty area: the fast internal buffer renders it as
    // at least ten strips. Every strip must observe the same old panel frame.
    lv_obj_add_event_cb(lv_scr_act(),observeDraw,LV_EVENT_DRAW_MAIN,nullptr);
    beforeDraw.assign(displayedPixels,displayedPixels+320*240);
    const unsigned priorDraws=observedDraws;const auto priorFrame=stats();
    const unsigned priorTransactions=displayTestTransactions;
    observing=true;fill(0x00ffff);lv_refr_now(nullptr);observing=false;
    check(observedDraws-priorDraws>=(displayTestFailInternal?1u:10u));
    check(stats().commits==priorFrame.commits+1);
    check(displayTestTransactions==priorTransactions+1);
    expected.assign(320*240,0x07ff);
    expectedPatch(expected,10,20,18,18,0x07e0);
    expectedPatch(expected,145,110,18,18,0xffe0);
    expectedPatch(expected,290,205,18,18,0x001f);same(expected);noChangeRefresh();
    lv_obj_clean(lv_scr_act());fill(0xff0000);lv_refr_now(nullptr);uniform(0xf800);

    for(int direction:{1,-1}){
        unsigned before=transferredPixels;
        check(Display::beginTransition(direction));fill(0x0000ff);
        const unsigned capturedPixels=transferredPixels;
        Display::startTransition(260);uniform(0xf800);
        check(transferredPixels==capturedPixels);
        check(!Display::beginTransition(-direction));
        displayTestMillis+=130;Display::pollTransition();
        if(direction>0){check(displayedPixels[0]==0x001f);check(displayedPixels[319]==0xf800);}
        else{check(displayedPixels[0]==0xf800);check(displayedPixels[319]==0x001f);}
        displayTestMillis+=170;Display::pollTransition();uniform(0x001f);check(!Display::transitioning());
        check(transferredPixels-before<320*240*2);
        // The shadow includes every blended/reveal pixel in either direction.
        noChangeRefresh();captureMatchesPanel();noChangeRefresh();
        auto *small=patch(17,33,3,5,0x00ff00);before=transferredPixels;
        lv_refr_now(nullptr);check(transferredPixels-before==15);
        expected.assign(320*240,0x001f);expectedPatch(expected,17,33,3,5,0x07e0);same(expected);
        lv_obj_del(small);lv_refr_now(nullptr);uniform(0x001f);noChangeRefresh();
        fill(0xff0000);lv_refr_now(nullptr);uniform(0xf800);
    }
    // Destination redraw during reveal stays withheld until the reveal ends.
    check(Display::beginTransition(1));fill(0x0000ff);Display::startTransition();
    auto *green=patch(5,5,12,12,0x00ff00);(void)green;
    const unsigned beforeDeferred=transferredPixels;
    lv_refr_now(nullptr);uniform(0xf800);check(transferredPixels==beforeDeferred);
    displayTestMillis+=300;Display::pollTransition();
    expected.assign(320*240,0x001f);expectedPatch(expected,5,5,12,12,0x07e0);same(expected);
    noChangeRefresh();captureMatchesPanel();noChangeRefresh();

    // Capture cancels an unfinished reveal and publishes the full destination.
    check(Display::beginTransition(-1));fill(0xffffff);Display::startTransition();
    captureMatchesPanel();check(!Display::transitioning());
    expected.assign(320*240,0xffff);expectedPatch(expected,5,5,12,12,0x07e0);same(expected);
    noChangeRefresh();
    Display::probeScanline();
    const auto final=stats();
    check(displayTestTransactions==displayTestEnds);
    std::printf("PASS: %u assertions; actual display.cpp + LVGL, %u draw observers, %lu flush regions, %lu completed refreshes, %u balanced SPI transactions, internal draw=%d; final-only commit, sparse differences, zero-change repaint, bidirectional reveal, deferred destination and capture shadow.\n",
                checks,observedDraws,(unsigned long)final.flushRegions,(unsigned long)final.commits,displayTestTransactions,final.internalDraw);
}
