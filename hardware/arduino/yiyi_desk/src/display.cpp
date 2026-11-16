#include "display.h"
#include "../board_config.h"
#include <TFT_eSPI.h>
#include <Arduino.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <string.h>
#include "transition_effect.h"
#include "display_frame_damage.h"
#include "display_frame_diff.h"

namespace Display {
namespace {

TFT_eSPI lcd;
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_color_t *frame = nullptr;   // LVGL destination; published only after final flush.
lv_color_t *presented = nullptr; // Last pixels actually submitted to panel GRAM.
bool presentedValid=false;
DisplayFrame::Damage frameDamage;
DisplayFrame::DiffPlan difference;
FrameStats refreshStats{};
uint32_t renderStartedUs=0;
bool lvglReady = false;
lv_color_t *outgoing=nullptr;
bool capturing=false,transitionActive=false,transitionAnimating=false;
// During a reveal LVGL keeps drawing, but the panel must keep the old content
// until the band reaches it. Up to 8 separate dirty regions are remembered (no
// longer merged into one possibly-huge union) and pushed once the reveal ends.
constexpr uint8_t MaxDeferred=8;
lv_area_t deferredAreas[MaxDeferred];
uint8_t deferredCount=0;
int transitionDirection=1,previousProgress=0;
uint32_t transitionAt=0,transitionDuration=260;
uint32_t transitionWallStart=0,transitionTickStart=0;
uint32_t transitionFrames=0,lastTransitionFrames=0,lastTransitionMs=0;
uint32_t transitionPushUs=0,lastTransitionPushUs=0;
uint32_t lastPanelPushUs=0,maxPanelPushUs=0,panelPushCalls=0;
uint64_t totalPanelPushUs=0;
// Frame-rate window: flush callbacks counted per second for `status.animFps`.
uint32_t fpsFrameCount=0,fpsWindowMs=0,fpsValue=0;
// Identity token for the timeline animation. lv_anim_del() matches on the var
// pointer value, so a static dummy is enough and nothing is dereferenced.
int transitionToken=0;

alignas(4) uint16_t transfer[Board::Width*8];

void pushArea(const lv_area_t &area,bool blend=false,int progress=0,bool transaction=true){
    uint32_t started=micros();
    int w=area.x2-area.x1+1;
    if(transaction)lcd.startWrite();
    lcd.setAddrWindow(area.x1,area.y1,w,area.y2-area.y1+1);
    for(int y=area.y1;y<=area.y2;y+=8){
        int rows=std::min(8,int(area.y2-y+1));
        for(int row=0;row<rows;++row){
            const int offset=(y+row)*Board::Width+area.x1;
            if(!blend)memcpy(transfer+row*w,frame+offset,w*sizeof(uint16_t));
            else for(int x=0;x<w;++x)transfer[row*w+x]=TransitionEffect::blend(outgoing[offset+x].full,frame[offset+x].full,TransitionEffect::opacity(progress,area.x1+x,transitionDirection));
            if(presented)memcpy(presented+offset,transfer+row*w,w*sizeof(uint16_t));
        }
        lcd.pushPixels(transfer,w*rows);
    }
    if(transaction)lcd.endWrite();
    lastPanelPushUs=micros()-started;
    maxPanelPushUs=std::max(maxPanelPushUs,lastPanelPushUs);
    totalPanelPushUs+=lastPanelPushUs;
    ++panelPushCalls;
}

void presentFrame(const DisplayFrame::Rect &changed){
    const uint32_t started=micros();
    refreshStats.lastPatches=refreshStats.lastPixels=0;
    refreshStats.lastDirtyPixels=(changed.x2-changed.x1+1)*(changed.y2-changed.y1+1);
    refreshStats.totalDirtyPixels+=refreshStats.lastDirtyPixels;
    if(presented&&presentedValid){
        DisplayFrame::planDiff(reinterpret_cast<const uint16_t*>(frame),
                               reinterpret_cast<const uint16_t*>(presented),changed,difference);
        refreshStats.lastPatches=difference.count;
        refreshStats.lastPixels=difference.totalPixels;
        // All patches come from the same immutable completed frame. Keep the
        // SPI transaction open across them instead of reconfiguring per tile.
        if(difference.count)lcd.startWrite();
        for(unsigned i=0;i<difference.count;++i){
            const auto &patch=difference.patches[i];
            const lv_area_t area={(lv_coord_t)patch.x1,(lv_coord_t)patch.y1,
                                  (lv_coord_t)patch.x2,(lv_coord_t)patch.y2};
            pushArea(area,false,0,false);
        }
        if(difference.count)lcd.endWrite();
    }else{
        // The LCD contents are unknown at boot. First initialise all pixels;
        // subsequent refreshes compare against the actual submitted contents.
        const lv_area_t area=presentedValid
            ?lv_area_t{(lv_coord_t)changed.x1,(lv_coord_t)changed.y1,(lv_coord_t)changed.x2,(lv_coord_t)changed.y2}
            :lv_area_t{0,0,Board::Width-1,Board::Height-1};
        pushArea(area);presentedValid=true;
        refreshStats.lastPatches=1;
        refreshStats.lastPixels=(area.x2-area.x1+1)*(area.y2-area.y1+1);
    }
    ++refreshStats.commits;
    refreshStats.lastCommitUs=micros()-started;
    refreshStats.maxCommitUs=std::max(refreshStats.maxCommitUs,refreshStats.lastCommitUs);
    refreshStats.totalCommitUs+=refreshStats.lastCommitUs;
    refreshStats.totalPixels+=refreshStats.lastPixels;
    if(refreshStats.lastPixels)++refreshStats.presentedFrames;
}

void renderStart(lv_disp_drv_t *){renderStartedUs=micros();}

// Standard CRC-32 (poly 0xEDB88320, init 0xFFFFFFFF, final xor 0xFFFFFFFF), matching
// zlib.crc32 in the Python tool. esp_rom_crc32_le() must NOT be used here: its seed
// argument does not act as a standard init (measured: ~esp_rom_crc32_le(~0u,d,n)
// equals a CRC with init 0 and no final xor), which made every captured frame fail
// the tool's integrity check even though the pixels were intact.
uint32_t standardCrc32(const uint8_t *data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

// Match TFT_eSPI's ILI9341 rotation table and its B6 scan-control settings.
// Mixing the old LCDWiki B6=0xA2 / MADCTL=0x68 with another driver reverses axes.
// Reference: https://github.com/mrcyclo/ES3C28P (same board/pins).
void writePanelMode(const Options &options) {
    static const uint8_t panelMadctl[] = {0x48, 0x28, 0x88, 0xE8};
    uint8_t madctl = panelMadctl[Board::LcdRotation % 4];
    if (!options.bgr) madctl &= static_cast<uint8_t>(~0x08);
    lcd.writecommand(0x36);lcd.writedata(madctl);
    lcd.invertDisplay(options.inverted);
    ledcWrite(Board::BacklightChannel, options.brightness);
}

void apply(const Options &options, bool invalidate) {
    writePanelMode(options);
    if (invalidate && lvglReady) lv_obj_invalidate(lv_scr_act());
}

void clearDeferred(){
    deferredCount=0;
}

void addDeferred(const lv_area_t &area){
    if(deferredCount<MaxDeferred){deferredAreas[deferredCount++]=area;return;}
    // Region list full: fold the newcomer into the last slot instead of
    // forgetting it. The list is bounded so a repaint storm cannot grow memory.
    lv_area_t &last=deferredAreas[MaxDeferred-1];
    last.x1=LV_MIN(last.x1,area.x1);last.y1=LV_MIN(last.y1,area.y1);
    last.x2=LV_MAX(last.x2,area.x2);last.y2=LV_MAX(last.y2,area.y2);
}

void pushDeferred(){
    for(uint8_t i=0;i<deferredCount;++i){
        const lv_area_t &area=deferredAreas[i];
        if(area.x2<area.x1||area.y2<area.y1)continue;
        uint32_t started=micros();
        pushArea(area);
        transitionPushUs+=micros()-started;
    }
    clearDeferred();
}

void pushBand(int progress){
    if(progress<=previousProgress)return;
    int first=std::max(0,previousProgress-TransitionEffect::Edge),last=std::min(Board::Width-1,progress);
    lv_area_t band={(lv_coord_t)(transitionDirection>0?first:Board::Width-1-last),0,
                    (lv_coord_t)(transitionDirection>0?last:Board::Width-1-first),Board::Height-1};
    uint32_t started=micros();
    pushArea(band,true,progress);
    previousProgress=progress;++transitionFrames;
    transitionPushUs+=micros()-started;
}

void finishTransition(){
    if(!transitionActive)return;
    transitionActive=false;transitionAnimating=false;
    pushDeferred();
    lastTransitionFrames=transitionFrames;
    lastTransitionMs=lv_tick_elaps(transitionAt);
    lastTransitionPushUs=transitionPushUs;
}

// Timeline step: the animation value is the elapsed time in ms, so the existing
// smoothstep curve and band arithmetic run unchanged. The SPI band push happens
// inside this callback, i.e. inside lv_timer_handler().
void transitionExec(void *var,int32_t elapsed){
    LV_UNUSED(var);
    if(!transitionActive)return;
    pushBand(TransitionEffect::timelineProgress(elapsed,transitionDuration));
}

void transitionFinished(lv_anim_t *anim){
    LV_UNUSED(anim);
    if(!transitionActive)return;
    pushBand(TransitionEffect::End);   // no-op when the last tick already reached End
    finishTransition();
}

void flushDisplay(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors) {
    ++fpsFrameCount;
    ++refreshStats.flushRegions;
    const int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    for (int row = 0; row < h; ++row)
        memcpy(frame + (area->y1 + row) * Board::Width + area->x1, colors + row * w, w * sizeof(lv_color_t));
    // LVGL may render one rotated digit in several dirty rectangles. Finish all
    // those renders before exposing any part of the new frame on the panel.
    frameDamage.add(area->x1,area->y1,area->x2,area->y2);
    const bool last=lv_disp_flush_is_last(drv);
    if(last){
        refreshStats.lastRenderUs=micros()-renderStartedUs;
        refreshStats.totalRenderUs+=refreshStats.lastRenderUs;
        refreshStats.lastRegions=frameDamage.regions();
        refreshStats.maxRegions=std::max(refreshStats.maxRegions,refreshStats.lastRegions);
    }
    DisplayFrame::Rect changed{};
    auto action=frameDamage.finish(last,capturing,transitionActive,changed);
    if(action==DisplayFrame::Action::Present)presentFrame(changed);
    else if(action==DisplayFrame::Action::Defer){
        const lv_area_t pending={(lv_coord_t)changed.x1,(lv_coord_t)changed.y1,
                                 (lv_coord_t)changed.x2,(lv_coord_t)changed.y2};
        addDeferred(pending);
    }
    lv_disp_flush_ready(drv);
}

}  // namespace

uint32_t framesPerSecond(){
    const uint32_t now=millis();
    if(fpsWindowMs==0){fpsWindowMs=now;fpsFrameCount=0;return 0;}
    const uint32_t elapsed=now-fpsWindowMs;
    if(elapsed>=1000){
        fpsValue=(uint32_t)((uint64_t)fpsFrameCount*1000u/elapsed);
        fpsFrameCount=0;fpsWindowMs=now;
    }
    return fpsValue;
}

bool begin(const Options &options) {
    pinMode(Board::Backlight, OUTPUT); digitalWrite(Board::Backlight, LOW);
    lcd.init(); // 40MHz SPI (panel_build_flags.ps1 SPI_FREQUENCY); board-local build flags, independent of global Arduino libraries.
    lcd.setRotation(Board::LcdRotation);
    lcd.setSwapBytes(true); // LV_COLOR_16_SWAP=0, ESP32's native little-endian RGB565.
    ledcSetup(Board::BacklightChannel, 20000, 8);
    ledcAttachPin(Board::Backlight, Board::BacklightChannel);
    ledcWrite(Board::BacklightChannel, options.brightness);
    apply(options,false);

    lv_init();
    const size_t pixels = Board::Width * Board::Height;
    // LVGL's software rotation blends repeatedly into its draw target. A small
    // internal-RAM strip is substantially faster than a PSRAM draw target;
    // completed strips are staged in `frame` and never published mid-render.
    constexpr size_t fastPixels=Board::Width*24;
    auto *a = (lv_color_t*)heap_caps_malloc(fastPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_color_t *b=nullptr;
    const bool internalDraw=a!=nullptr;
    if(!a){
        a=(lv_color_t*)heap_caps_malloc(pixels*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        b=(lv_color_t*)heap_caps_malloc(pixels*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    }
    frame = (lv_color_t*)heap_caps_calloc(pixels, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    outgoing=(lv_color_t*)heap_caps_malloc(pixels*sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    presented=(lv_color_t*)heap_caps_calloc(pixels,sizeof(lv_color_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!a || (!internalDraw&&!b) || !frame) {
        Serial.println("ERROR: display buffer allocation failed");
        free(a); free(b); free(frame);free(outgoing);free(presented);presented=nullptr;outgoing=nullptr; frame = nullptr;
        return false;
    }
    // The 4th (snapshot) buffer is optional: keep the display usable without it,
    // but transitions and serial frame capture are disabled (see sendFrame()).
    if (!outgoing) Serial.println("WARN: display snapshot buffer allocation failed; transitions/screenshots disabled");
    if (!presented) Serial.println("WARN: display difference buffer unavailable; complete-frame submission retained");
    presentedValid=false;frameDamage.clear();refreshStats=FrameStats{};
    refreshStats.internalDraw=internalDraw;refreshStats.drawBufferPixels=internalDraw?fastPixels:pixels;
    lv_disp_draw_buf_init(&drawBuffer, a, b, internalDraw?fastPixels:pixels);
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = Board::Width;
    displayDriver.ver_res = Board::Height;
    displayDriver.flush_cb = flushDisplay;
    displayDriver.render_start_cb = renderStart;
    displayDriver.draw_buf = &drawBuffer;
    lv_disp_drv_register(&displayDriver);
    lvglReady = true;
    return true;
}

void apply(const Options &options) { apply(options, true); }

void setBrightness(uint8_t value) { ledcWrite(Board::BacklightChannel, value); }

bool ready() { return lvglReady && frame != nullptr; }

bool beginTransition(int direction){
    if(!ready()||!outgoing||transitionActive||capturing)return false;
    lv_refr_now(nullptr);memcpy(outgoing,frame,Board::Width*Board::Height*sizeof(lv_color_t));
    transitionDirection=direction<0?-1:1;capturing=true;return true;
}
void startTransition(uint32_t duration){
    if(!capturing)return;
    lv_refr_now(nullptr);capturing=false;clearDeferred();transitionActive=true;
    transitionAt=lv_tick_get();transitionWallStart=millis();transitionTickStart=transitionAt;
    transitionDuration=duration?duration:260;
    previousProgress=0;transitionFrames=0;transitionPushUs=0;
    // Replace a stale animation, then run the reveal as an lv_anim timeline.
    // Values are milliseconds; the smoothstep mapping stays in transition_effect.h.
    lv_anim_del(&transitionToken,nullptr);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim,&transitionToken);
    lv_anim_set_values(&anim,0,(int32_t)transitionDuration);
    lv_anim_set_time(&anim,transitionDuration);
    lv_anim_set_path_cb(&anim,lv_anim_path_linear);
    lv_anim_set_exec_cb(&anim,transitionExec);
    lv_anim_set_ready_cb(&anim,transitionFinished);
    lv_anim_start(&anim);
    transitionAnimating=true;
}
bool transitioning(){return transitionActive||capturing;}
void pollTransition(){
    if(!transitionActive)return;
    // Everything is driven by the lv_anim timeline. This function only
    //   * advances it in environments where lv_timer_handler() is not pumped
    //     (the host unit test calls pollTransition() directly), by folding a
    //     wall-clock gap into LVGL's tick - never by computing progress itself;
    //   * warns when the animation disappeared without its ready callback.
    uint32_t wallElapsed=millis()-transitionWallStart;
    uint32_t tickElapsed=lv_tick_elaps(transitionTickStart);
    if(wallElapsed>tickElapsed+100)lv_tick_inc(wallElapsed-tickElapsed);
    lv_timer_handler();
    if(!transitionAnimating||lv_anim_get(&transitionToken,transitionExec)==nullptr)finishTransition();
}
void transitionStats(uint32_t &frames,uint32_t &duration){frames=lastTransitionFrames;duration=lastTransitionMs;}
uint32_t transitionPushMs(){return lastTransitionPushUs/1000;}

void panelPushStats(uint32_t &lastUs,uint32_t &maxUs,uint64_t &totalUs,uint32_t &calls){
    lastUs=lastPanelPushUs;maxUs=maxPanelPushUs;totalUs=totalPanelPushUs;calls=panelPushCalls;
}

void frameStats(FrameStats &stats){stats=refreshStats;stats.differenceReady=presented!=nullptr;}

void probeScanline(){
    // Read-only diagnosis. This board exposes SDO but has no connected TE pin.
    // Read outside all write transactions and never wait indefinitely for VSYNC.
    Serial.print("SCAN_PROBE {\"samples\":[");
    for(unsigned i=0;i<40;++i){
        uint8_t high=lcd.readcommand8(0x45,0),low=lcd.readcommand8(0x45,1);
        uint8_t highAgain=lcd.readcommand8(0x45,0);
        int line=high==highAgain&&high<=1?int(high)*256+low:-1;
        if(line>319)line=-1;
        if(i)Serial.print(',');Serial.printf("[%lu,%d]",(unsigned long)micros(),line);
        delay(1);
    }
    Serial.println("]}");
}

bool sendFrame() {
    if (!ready()) return false;
    // Without the snapshot buffer the live mirror must NOT be sent: flushDisplay()
    // can rewrite `frame` while the serial chunks stream, so the CRC/frame would be
    // corrupt. Report the condition and refuse instead of capturing bad pixels.
    if (!outgoing) {
        // Do not print a "FRAME " header here: the host frame reader needs a real
        // 5-field FRAME line, and a bare FRAME FAILED terminates it cleanly.
        Serial.println("display snapshot buffer unavailable; frame capture disabled");
        Serial.println("FRAME FAILED");
        return false;
    }
    // A captured frame must describe a complete destination, not a partly
    // revealed panel. Kill the timeline, finish the reveal, then serialise.
    if(transitionActive||transitionAnimating){
        lv_anim_del(&transitionToken,nullptr);
        transitionActive=false;transitionAnimating=false;clearDeferred();
        lv_area_t area={0,0,(lv_coord_t)(Board::Width-1),(lv_coord_t)(Board::Height-1)};pushArea(area);
    }
    lv_refr_now(nullptr);
    const size_t size = Board::Width * Board::Height * 2;
    // LVGL timers keep running between serial chunks (that is what keeps the UI
    // alive during a capture), so the bytes must be read from an immutable copy:
    // `frame` can be updated by flushDisplay while the transfer is in progress.
    memcpy(outgoing,frame,size);
    const uint8_t *bytes = reinterpret_cast<const uint8_t*>(outgoing);
    Serial.printf("FRAME %d %d %u %08lX\n", Board::Width, Board::Height, (unsigned)size,
        (unsigned long)standardCrc32(bytes, size));
    size_t sent = 0;
    uint32_t progressAt = millis();
    while (sent < size && millis() - progressAt < 3000) {
        size_t n = Serial.write(bytes + sent, std::min<size_t>(2048, size - sent));
        if (n) { sent += n; progressAt = millis(); }
        // Chunked, not one 153,600-byte blocking write: LVGL timers, animations
        // and touch keep being serviced between chunks instead of a 3s freeze.
        lv_timer_handler();
        delay(1);
    }
    Serial.println(sent == size ? "\nEND" : "\nFRAME FAILED");
    Serial.flush();
    return sent == size;
}

}
