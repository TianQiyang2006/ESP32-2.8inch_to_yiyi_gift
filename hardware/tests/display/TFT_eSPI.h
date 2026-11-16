#pragma once
#include <cstdint>
#include <cassert>
extern uint16_t displayedPixels[320*240];
extern unsigned transferredPixels;
extern unsigned displayTestTransactions,displayTestEnds;
struct TFT_eSPI {
    int x=0,y=0,width=320,height=240,offset=0;
    bool writing=false;
    void init(){}
    void setRotation(int){}
    void setSwapBytes(bool){}
    void writecommand(uint8_t){}
    void writedata(uint8_t){}
    void invertDisplay(bool){}
    uint8_t readcommand8(uint8_t,uint8_t=0){assert(!writing);return 0;}
    void startWrite(){assert(!writing);writing=true;++displayTestTransactions;}
    void endWrite(){assert(writing);writing=false;++displayTestEnds;}
    void setAddrWindow(int px,int py,int w,int h){assert(writing);x=px;y=py;width=w;height=h;offset=0;}
    void pushPixels(const uint16_t *pixels,int count){
        assert(writing&&count>=0&&offset+count<=width*height);
        for(int i=0;i<count;++i,++offset){
            int px=x+offset%width,py=y+offset/width;
            assert(px>=0&&px<320&&py>=0&&py<240);
            displayedPixels[py*320+px]=pixels[i];
        }
        transferredPixels+=count;
    }
};
