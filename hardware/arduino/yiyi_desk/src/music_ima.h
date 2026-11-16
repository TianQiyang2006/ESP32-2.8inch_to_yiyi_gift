#pragma once
#include <stdint.h>
#include <stddef.h>

// Mono IMA WAV blocks as emitted by ffmpeg (low nibble first).
namespace MusicIma {
constexpr unsigned Rate=32000, BlockBytes=1024, BlockSamples=2041;
inline bool decode(const uint8_t *block, int16_t *pcm) {
    static const int steps[89]={
        7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,
        66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,
        371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
        1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,
        4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,
        13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    static const int adjust[8]={-1,-1,-1,-1,2,4,6,8};
    int predictor=int16_t(unsigned(block[0])|(unsigned(block[1])<<8));
    int index=block[2];
    if(index>88||block[3])return false;
    pcm[0]=predictor;
    for(unsigned i=0;i<BlockSamples-1;++i){
        unsigned nibble=(block[4+i/2]>>((i&1)*4))&15;
        int diff=((2*int(nibble&7)+1)*steps[index])>>3;
        predictor+=(nibble&8)?-diff:diff;
        if(predictor>32767)predictor=32767;
        if(predictor< -32768)predictor=-32768;
        index+=adjust[nibble&7];if(index<0)index=0;if(index>88)index=88;
        pcm[i+1]=int16_t(predictor);
    }
    return true;
}
}
