#include "hal.h"
#include "../../arduino/yiyi_desk/src/audio_board.h"
#include <cassert>
#include <iostream>

int main(int argc,char **argv){
    using namespace AudioTest;
    assert(AudioBoard::begin());
    // Datasheet transfer curve: -95.5 + 0.5 * reg. The old 0xff added +32 dB.
    assert(registers[0x32]==191);
    assert(-191+int(registers[0x32])==0); // dB in half-dB units: unity
    assert(rate==16000&&registers[0x04]==0x20);
    if(argc>1&&std::string(argv[1])=="reminder-ownership"){
    // A timer and Pomodoro share priority 2; stopping one must leave the other
    // request intact. The alarm still preempts both at priority 3.
    assert(AudioBoard::ringtone(1,20,3000,2,0,AudioBoard::Reminder::Timer));
    AudioBoard::stopRingtone(2,AudioBoard::Reminder::Pomodoro);
    assert(!AudioBoard::ringtone(0,20,3000,1));
    AudioBoard::stopRingtone(2,AudioBoard::Reminder::Timer);
    assert(AudioBoard::ringtone(0,20,3000,1));
    assert(AudioBoard::ringtone(0,20,3000,3));
    AudioBoard::stopRingtone(2,AudioBoard::Reminder::Timer);
    assert(!AudioBoard::ringtone(1,20,3000,2,0,AudioBoard::Reminder::Timer));
    AudioBoard::stopRingtone(3);
    std::cout<<"PASS: actual audio reminder ownership and alarm preemption\n";
    return 0;
    }
    const int16_t samples[]={-32768,-20000,-10000,-1,0,1,10000,20000,32767};
    constexpr unsigned n=sizeof(samples)/sizeof(*samples);
    if(argc>1&&std::string(argv[1])=="media-ownership"){
        // Delayed writes and cleanup from the previous player must not affect
        // the new source, in either direction. Exercise actual driver calls.
        AudioBoard::selectStream(AudioBoard::StreamSource::Radio);
        assert(AudioBoard::streamRadioPcm(samples,n,35));
        events.clear();const auto radioPcm=output;
        assert(!AudioBoard::streamPcm(samples,n,90,32000));
        AudioBoard::endStream();
        assert(events.empty()&&output==radioPcm&&amplifier==LOW);
        AudioBoard::selectStream(AudioBoard::StreamSource::Music);
        assert(AudioBoard::streamPcm(samples,n,40,32000));
        events.clear();const auto musicPcm=output;
        assert(!AudioBoard::streamRadioPcm(samples,n,90));
        AudioBoard::endRadioStream();
        assert(events.empty()&&output==musicPcm&&amplifier==LOW);
        AudioBoard::endStream();assert(amplifier==HIGH);
        assert(AudioBoard::streamPcm(samples,n,40,32000));
        assert(AudioBoard::ringtone(1,20,3000,3));
        assert(!AudioBoard::streamPcm(samples,n,40,32000));
        assert(!AudioBoard::streamRadioPcm(samples,n,35));
        AudioBoard::stopRingtone(3);
        // This fake HAL does not execute the reminder worker; real asynchronous
        // completion/resume remains part of the eventual device acceptance.
        std::cout<<"PASS: music/radio handoff, stale writes/cleanup and reminder priority\n";
        return 0;
    }
    for(unsigned percent:{0U,20U,40U,100U,150U}){
        assert(AudioBoard::streamPcm(samples,n,percent,32000));
        assert(output.size()==n*2);
        for(unsigned i=0;i<n;++i){
            int16_t expected=int64_t(samples[i])*std::min(percent,100U)/100;
            assert(output[i*2]==expected&&output[i*2+1]==expected);
        }
        assert(amplifier==(percent?LOW:HIGH));
    }
    assert(rate==32000&&registers[0x04]==0x10);
    events.clear();
    assert(AudioBoard::streamPcm(samples,n,40,16000));
    assert(rate==16000&&registers[0x04]==0x20);
    assert((events==std::vector<std::string>{"mute","clear","clock","register","unmute"}));
    AudioBoard::protect(true,15);
    assert(AudioBoard::streamPcm(samples,n,40));
    assert(output[0]==int64_t(samples[0])*15/100);
    AudioBoard::protect(false);
    assert(AudioBoard::streamPcm(samples,n,40));
    for(auto sample:output)assert(!sample);
    assert(amplifier==HIGH);
    AudioBoard::protect(true);
    assert(!AudioBoard::streamPcm(samples,0,40));
    assert(!AudioBoard::streamPcm(nullptr,n,40));
    assert(!AudioBoard::streamPcm(samples,257,40));
    assert(!AudioBoard::streamPcm(samples,n,40,8000));
    // Exercise each hardware failure in a fresh process because faults latch.
    std::string failure=argc>1?argv[1]:"clock-failure";
    failRegister=failure=="register-failure";
    failClock=failure=="clock-failure";
    failWrite=failure=="write-failure";
    shortWrite=failure=="short-write";
    assert(!AudioBoard::streamPcm(samples,n,40,32000));
    assert(amplifier==HIGH&&!AudioBoard::error().empty());
    assert(!AudioBoard::streamPcm(samples,n,40,16000));
    std::cout<<"PASS: unity DAC gain, signed PCM/stereo/volume, 16/32 kHz OSR, muted clock change, protection, latched "
             <<failure<<"\n";
}
