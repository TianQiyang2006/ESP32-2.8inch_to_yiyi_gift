#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

namespace RadioCore {
// Separates HTTP transfer framing before ICY framing. Only Result::Data bytes
// may enter the compressed decoder. Sizes/extensions can cross socket reads.
class Chunked {
 enum Phase { Size, Bytes, CR, LF, Finished, Broken } phase=Size;
 uint32_t remaining=0;
 unsigned digits=0;
 bool extension=false,sawCR=false;
public:
 enum Result { Skip, Data, End, Error };
 Result put(uint8_t byte) {
  if(phase==Finished)return End;
  if(phase==Broken)return Error;
  if(phase==Bytes){if(--remaining==0)phase=CR;return Data;}
  if(phase==CR){phase=byte=='\r'?LF:Broken;return phase==Broken?Error:Skip;}
  if(phase==LF){phase=byte=='\n'?Size:Broken;digits=0;remaining=0;extension=false;sawCR=false;return phase==Broken?Error:Skip;}
  if(byte=='\r'){sawCR=true;return Skip;}
  if(byte=='\n'){
   if(!digits||!sawCR){phase=Broken;return Error;}
   phase=remaining?Bytes:Finished;return phase==Finished?End:Skip;
  }
  if(sawCR){phase=Broken;return Error;}
  if(byte==';'){extension=true;return Skip;}
  if(extension)return Skip;
  int digit=byte>='0'&&byte<='9'?byte-'0':byte>='a'&&byte<='f'?byte-'a'+10:byte>='A'&&byte<='F'?byte-'A'+10:-1;
  if(digit<0||digits>=8||remaining>0x0fffffffU){phase=Broken;return Error;}
  ++digits;remaining=(remaining<<4)|unsigned(digit);return Skip;
 }
};

class Icy {
 uint32_t interval=0,audio=0,metadata=0;
 unsigned length=0;
 char text[513]{};
public:
 explicit Icy(uint32_t every=0):interval(every),audio(every){}
 // Returns true only for an audio byte. A metadata block is always consumed in
 // full, even when its text is too long for the bounded display buffer.
 bool put(uint8_t byte,bool &updated) {
  updated=false;
  if(!interval)return true;
  if(metadata){
   if(length<sizeof(text)-1)text[length++]=char(byte);
   if(--metadata==0){text[length]=0;audio=interval;updated=true;}
   return false;
  }
  if(!audio){metadata=uint32_t(byte)*16;length=0;text[0]=0;if(!metadata)audio=interval;return false;}
  --audio;return true;
 }
 const char *title()const{return text;}
};

// A causal 32-tap low-pass filter precedes rational interpolation. The phase is
// carried across decoder frames, so 44.1/48 kHz sources retain their true speed.
// Output is always mono 32 kHz, the existing ES8311 path's supported music rate.
class Resampler {
 static constexpr unsigned Target=32000,Taps=32;
 unsigned rate=0,index=0,phase=0;
 float coefficients[Taps]{};
 int16_t history[Taps]{};
 int16_t previous=0;
 bool first=true;
public:
 bool setRate(unsigned input) {
  if(input<8000||input>96000)return false;
  if(input==rate)return true;
  rate=input;phase=0;index=0;first=true;memset(history,0,sizeof(history));
  if(rate==Target)return true;
  const double cutoff=fmin(.45,14000.0/rate);double total=0;
  for(unsigned i=0;i<Taps;++i){
   double d=double(i)-(Taps-1)*.5;
   double sinc=sin(2*3.141592653589793*cutoff*d)/(3.141592653589793*d);
   coefficients[i]=float(sinc*(.54-.46*cos(2*3.141592653589793*i/(Taps-1))));total+=coefficients[i];
  }
  for(float &c:coefficients)c=float(c/total);
  return true;
 }
 template<class Emit>bool put(int16_t input,Emit emit) {
  if(!rate)return false;
  int16_t sample=input;
  if(rate!=Target){
   history[index]=input;float value=0;unsigned p=index;
   for(unsigned i=0;i<Taps;++i){value+=coefficients[i]*history[p];p=(p+Taps-1)%Taps;}
   index=(index+1)%Taps;
   sample=int16_t(fmax(-32768.f,fmin(32767.f,value)));
  }
  if(first){previous=sample;first=false;return true;}
  while(phase<=Target){
   int32_t value=int32_t(previous)+(int64_t(int32_t(sample)-previous)*phase)/Target;
   if(!emit(int16_t(value)))return false;
   phase+=rate;
  }
  phase-=Target;previous=sample;return true;
 }
};

inline uint32_t crc32(const void *data,size_t bytes) {
 uint32_t crc=~0U;const uint8_t *p=static_cast<const uint8_t*>(data);
 while(bytes--){crc^=*p++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320U&uint32_t(-int32_t(crc&1)));}
 return ~crc;
}
inline uint32_t idHash(const char *text) {uint32_t h=2166136261U;for(;*text;++text)h=(h^uint8_t(*text))*16777619U;return h;}
}
