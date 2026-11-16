#include "hardware/arduino/yiyi_desk/src/voice_activity.h"
#include "hardware/arduino/yiyi_desk/src/voice_session.h"
#include "hardware/arduino/yiyi_desk/src/voice_buffer.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <deque>

int main(){
 using VoiceActivity::Event;
 VoiceActivity::Detector vad;
 // Quiet waiting never submits an empty utterance. An isolated tap is ignored.
 for(int i=0;i<2000;i++)assert(vad.feed(90)==Event::None);
 assert(vad.feed(1800)==Event::None);assert(vad.feed(90)==Event::None);
 assert(vad.feed(1800)==Event::None);assert(vad.feed(1800)==Event::None);
 assert(vad.feed(1800)==Event::Start);
 for(int i=0;i<10;i++)assert(vad.feed(1700)==Event::None);
 for(int i=0;i<14;i++)assert(vad.feed(90)==Event::None);
 assert(vad.feed(90)==Event::End);assert(vad.feed(90)==Event::None);
 vad.reset();assert(!vad.started&&!vad.ended);
 // A short pause inside a sentence must not end the turn.
 for(int i=0;i<3;i++)vad.feed(1200);
 for(int i=0;i<8;i++)assert(vad.feed(100)==Event::None);
 assert(vad.feed(1200)==Event::None);
 for(int i=0;i<14;i++)assert(vad.feed(100)==Event::None);
 assert(vad.feed(100)==Event::End);
 // A noisy environment cannot keep one capture running indefinitely.
 vad.reset();Event event=Event::None;
 for(int i=0;i<500;i++)event=vad.feed(2000);
 assert(event==Event::End);

 VoiceSession::Session session;assert(!session.active);
 session.start();session.listen(100);assert(session.active);
 assert(!session.idle(60099,false,0));assert(session.idle(60100,false,0));
 // WebSocket keepalives do not postpone sleep; a real utterance does.
 assert(!session.idle(65000,true,60000));assert(session.idle(120000,true,60000));
 session.waiting(65000);assert(!session.responseExpired(124999));assert(session.responseExpired(125000));
 session.playbackDone(80000);assert(!session.resume(80219));assert(session.resume(80220));
 session.listen(80220);assert(session.active&&!session.resume(90000));
 assert(!session.idle(140219,false,0));assert(session.idle(140220,false,0));
 session.end(true);assert(!session.active&&session.sleeping&&!session.resume(150000));
 session.start();assert(session.active&&!session.sleeping);session.end();assert(!session.active&&!session.sleeping);
 session.start();session.listen(UINT32_MAX-1000);
 assert(!session.idle(58998,false,0));assert(session.idle(58999,false,0));
 session.playbackDone(UINT32_MAX-219);assert(!session.resume(UINT32_MAX));assert(session.resume(0));
 session.end();assert(!session.resume(100));

 uint8_t bytes[512*1024];VoiceBuffer::Entry entries[2048];VoiceBuffer::Ring ring;
 ring.init(bytes,sizeof(bytes),entries,2048);
 std::vector<uint8_t> frame(180);VoiceBuffer::Entry entry;
 uint8_t output[1500];
 // Burst-load 36 seconds of typical Opus packets (old buffer held 1.44s).
 for(int i=0;i<600;i++){for(int j=0;j<180;j++)frame[j]=(i+j)%256;assert(ring.push(frame.data(),frame.size(),24000,7));}
 assert(ring.count()==600&&ring.used()==108000);
 for(int i=0;i<600;i++){assert(ring.pop(output,sizeof(output),entry));assert(entry.size==180&&entry.rate==24000&&entry.generation==7);for(int j=0;j<180;j++)assert(output[j]==(i+j)%256);}
 assert(!ring.count()&&!ring.used()&&!ring.pop(output,sizeof(output),entry));
 // Mixed packet sizes repeatedly wrap both rings without corruption.
 uint32_t random=31;std::deque<std::vector<uint8_t>> expected;
 for(int step=0;step<40000;step++){
  random=random*1664525+1013904223;
  if(expected.empty()||(random&3)){
   size_t size=1+((random>>4)%1500);frame.resize(size);
   for(size_t i=0;i<size;i++)frame[i]=(step+i)%256;
   if(ring.push(frame.data(),size,16000,9))expected.push_back(frame);
  }else{
   assert(ring.pop(output,sizeof(output),entry));assert(entry.size==expected.front().size());
   assert(!memcmp(output,expected.front().data(),entry.size));expected.pop_front();
  }
  assert(ring.count()==expected.size());assert(ring.used()<=sizeof(bytes));
 }
 ring.clear();assert(!ring.count()&&!ring.used());
 uint8_t smallBytes[12];VoiceBuffer::Entry smallEntries[2];ring.init(smallBytes,12,smallEntries,2);frame.assign(6,42);
 assert(!ring.push(frame.data(),0,16000,1));assert(!ring.push(frame.data(),1501,16000,1));
 assert(ring.push(frame.data(),6,16000,1));assert(ring.push(frame.data(),6,16000,1));assert(!ring.push(frame.data(),1,16000,1));
 assert(!ring.pop(output,5,entry));assert(ring.count()==2);assert(ring.pop(output,sizeof(output),entry));assert(ring.push(frame.data(),6,16000,2));
 puts("PASS: automatic speech onset/end, no empty turns, pause/noise bounds, continuous rearm/sleep/abort and millis wrap, 36s Opus burst and 40,000 variable FIFO operations.");
}
