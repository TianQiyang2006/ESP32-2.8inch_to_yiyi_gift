#include "../../arduino/yiyi_desk/src/music_ima.h"
#include "../../arduino/yiyi_desk/src/tone_synth.h"
#include <fstream>
#include <vector>
#include <cassert>
#include <iostream>

std::vector<uint8_t> read(const char *path){
    std::ifstream f(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    auto blocks=read(argv[1]),reference=read(argv[2]);
    assert(!blocks.empty()&&blocks.size()%MusicIma::BlockBytes==0);
    assert(reference.size()==blocks.size()/MusicIma::BlockBytes*MusicIma::BlockSamples*2);
    int16_t decoded[MusicIma::BlockSamples];
    for(size_t b=0;b<blocks.size()/MusicIma::BlockBytes;++b){
        assert(MusicIma::decode(blocks.data()+b*MusicIma::BlockBytes,decoded));
        for(unsigned i=0;i<MusicIma::BlockSamples;++i){
            size_t p=(b*MusicIma::BlockSamples+i)*2;
            int16_t want=int16_t(unsigned(reference[p])|(unsigned(reference[p+1])<<8));
            if(decoded[i]!=want){
                std::cerr<<"Mismatch block "<<b<<" sample "<<i<<": "<<decoded[i]<<" vs "<<want<<"\n";
                return 1;
            }
            assert(ToneSynth::applyMaster(decoded[i],40)==int64_t(want)*40/100);
        }
    }
    auto invalid=blocks;invalid[2]=89;assert(!MusicIma::decode(invalid.data(),decoded));
    invalid[2]=0;invalid[3]=1;assert(!MusicIma::decode(invalid.data(),decoded));
    // A seek starts at a fresh block; no decoder history may leak across blocks.
    assert(MusicIma::decode(blocks.data()+MusicIma::BlockBytes,decoded));
    size_t p=MusicIma::BlockSamples*2;
    assert(decoded[0]==int16_t(unsigned(reference[p])|(unsigned(reference[p+1])<<8)));
    assert(ToneSynth::applyMaster(-10000,40)==-4000);
    assert(ToneSynth::applyMaster(10000,40)==4000);
    assert(ToneSynth::applyMaster(-32768,100)==-32768);
    std::cout<<"PASS: "<<reference.size()/2<<" samples equal ffmpeg, signed volume 40, invalid blocks rejected, independent seek\n";
}
