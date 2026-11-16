#include "../../arduino/yiyi_desk/src/radio_vendor/radio_stream_core.h"
#include "../../arduino/yiyi_desk/src/radio_vendor/libhelix-mp3/mp3dec.h"
#undef Word64
#include "../../arduino/yiyi_desk/src/radio_vendor/libhelix-aac/aacdec.h"
#include <assert.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static void coreChecks(){
 using namespace RadioCore;
 Chunked transfer;std::string decoded;
 for(uint8_t c:std::string("4;name=value\r\nABCD\r\n3\r\nEFG\r\n0\r\n\r\n")){
  auto result=transfer.put(c);assert(result!=Chunked::Error);if(result==Chunked::Data)decoded+=char(c);
 }
 assert(decoded=="ABCDEFG");
 for(const char *invalid:{"g\r\n","100000000\r\n","4\n","4\rX","1\r\nxZ"}){
  Chunked broken;bool rejected=false;for(uint8_t c:std::string(invalid))rejected|=broken.put(c)==Chunked::Error;assert(rejected);
 }
 Icy icy(3);std::string audio,metadata="StreamTitle='Music test';";metadata.resize(32,0);
 std::string encoded="abc";encoded+=char(2);encoded+=metadata;encoded+="def";encoded+=char(0);encoded+="ghi";
 bool received=false;for(uint8_t c:encoded){bool update=false;if(icy.put(c,update))audio+=char(c);if(update){assert(std::string(icy.title())=="StreamTitle='Music test';");received=true;}}
 assert(received&&audio=="abcdefghi");
 Icy huge(1);bool done=false;bool update=false;assert(huge.put('a',update));assert(!huge.put(255,update));
 for(unsigned i=0;i<4080;++i){assert(!huge.put('x',update));done|=update;}assert(done&&strlen(huge.title())==512&&huge.put('b',update));
 for(unsigned rate:{8000U,16000U,22050U,32000U,44100U,48000U,96000U}){
  Resampler sampler;assert(sampler.setRate(rate));unsigned outputs=0;const unsigned inputs=rate*2;
  for(unsigned i=0;i<inputs;++i)assert(sampler.put(int16_t(10000*sin(i*2*3.141592653589793*1000/rate)),[&](int16_t){++outputs;return true;}));
  assert(outputs==1+uint64_t(inputs-1)*32000/rate);
 }
 auto rms=[](unsigned hz){Resampler r;r.setRate(48000);double power=0;unsigned n=0,total=0;for(unsigned i=0;i<48000;++i)r.put(int16_t(16000*sin(i*2*3.141592653589793*hz/48000)),[&](int16_t x){if(total++>500){power+=double(x)*x;++n;}return true;});return sqrt(power/n);};
 assert(rms(20000)<rms(1000)*.08); // Prevent aliasing above the 16 kHz output Nyquist frequency.
 const char *text="123456789";assert(crc32(text,9)==0xcbf43926U);
 std::cout<<"{\"core_passed\":true,\"resample_rates\":7,\"chunked_and_icy\":true,\"alias_rejection\":true}"<<std::endl;
}

int main(int argc,char **argv){
 if(argc==1){coreChecks();return 0;}
 assert(argc==4);bool aac=std::string(argv[1])=="aac";
 std::ifstream source(argv[2],std::ios::binary);assert(source.good());std::vector<uint8_t> input((std::istreambuf_iterator<char>(source)),{});
 std::ofstream output(std::string(argv[3])+".raw",std::ios::binary),mono(std::string(argv[3])+".32k.raw",std::ios::binary);
 HMP3Decoder mp3=aac?nullptr:MP3InitDecoder();HAACDecoder adts=aac?AACInitDecoder():nullptr;assert(aac?adts!=nullptr:mp3!=nullptr);
 RadioCore::Resampler sampler;unsigned head=0,frames=0,errors=0,rate=0,channels=0,outputCount=0;uint64_t totalSamples=0;
 int16_t pcm[4096];
 while(head+8<input.size()){
  int sync=aac?AACFindSyncWord(input.data()+head,int(input.size()-head)):MP3FindSyncWord(input.data()+head,int(input.size()-head));if(sync<0)break;head+=unsigned(sync);
  auto *cursor=input.data()+head;int left=int(input.size()-head);int result=aac?AACDecode(adts,&cursor,&left,pcm):MP3Decode(mp3,&cursor,&left,pcm,0);
  unsigned consumed=unsigned(cursor-input.data()-head);head+=consumed?consumed:1;
  if(result){++errors;continue;}
  unsigned samples=0;
  if(aac){AACFrameInfo info{};AACGetLastFrameInfo(adts,&info);rate=info.sampRateOut;channels=info.nChans;samples=info.outputSamps;}
  else{MP3FrameInfo info{};MP3GetLastFrameInfo(mp3,&info);rate=info.samprate;channels=info.nChans;samples=info.outputSamps;}
  assert(channels>=1&&channels<=2&&samples<=4096&&samples%channels==0);assert(sampler.setRate(rate));
  output.write(reinterpret_cast<const char*>(pcm),samples*2);++frames;totalSamples+=samples/channels;
  for(unsigned i=0;i<samples;i+=channels){int16_t x=channels==1?pcm[i]:int16_t((int32_t(pcm[i])+pcm[i+1])/2);sampler.put(x,[&](int16_t v){mono.write(reinterpret_cast<const char*>(&v),2);++outputCount;return true;});}
 }
 if(mp3)MP3FreeDecoder(mp3);if(adts)AACFreeDecoder(adts);
 // AAC at 16 kHz has only about 47 frames in a three-second fixture; validate
 // decoded duration rather than assuming every codec has MP3's frame rate.
 assert(frames>0&&rate>0&&totalSamples>uint64_t(rate)*2);
 std::cout<<"{\"frames\":"<<frames<<",\"errors\":"<<errors<<",\"rate\":"<<rate<<",\"channels\":"<<channels<<",\"input_samples\":"<<totalSamples<<",\"output_samples\":"<<outputCount<<"}"<<std::endl;
 assert(outputCount==1+(totalSamples-1)*32000/rate);
}
