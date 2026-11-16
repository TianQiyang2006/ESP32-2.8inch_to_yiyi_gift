#include "hardware/arduino/yiyi_desk/src/voice_protocol.h"
#include "hardware/arduino/yiyi_desk/src/voice_handshake.h"
#include <opus.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

int main(){
 VoiceHandshake::Clock handshake;
 handshake.connecting(1000);assert(!handshake.expired(15000));
 handshake.helloSent(15000);assert(!handshake.expired(39999));assert(handshake.expired(40000));
 handshake.ready();assert(!handshake.expired(100000));
 handshake.connecting(0);assert(!handshake.expired(39999));assert(handshake.expired(40000));
 handshake.helloSent(UINT32_MAX-4000);assert(!handshake.expired(20998));assert(handshake.expired(20999));
 handshake.reset();assert(!handshake.expired(UINT32_MAX));
 // These are the official v2/v3 wire envelopes, independent of frame().
 const uint8_t pcmPacket[]={0xf8,0xff,0xfe};
 const uint8_t v2[]={0,2,0,0,0,0,0,0,0x12,0x34,0x56,0x78,0,0,0,3,0xf8,0xff,0xfe};
 const uint8_t v3[]={0,0,0,3,0xf8,0xff,0xfe};
 uint8_t buffer[1600];
 for(int version=1;version<=3;version++){
  size_t size=VoiceProtocol::frame(version,pcmPacket,3,0x12345678,buffer,sizeof(buffer));
  const uint8_t *expected=version==1?pcmPacket:version==2?v2:v3;
  size_t expectedSize=version==1?3:version==2?sizeof(v2):sizeof(v3);
  assert(size==expectedSize&&!memcmp(buffer,expected,size));
  for(size_t truncated=0;truncated<size;truncated++){
   const uint8_t *bytes=buffer;size_t count=truncated;
   if(version!=1)assert(!VoiceProtocol::payload(version,bytes,count));
  }
  const uint8_t *bytes=buffer;size_t count=size;assert(VoiceProtocol::payload(version,bytes,count));assert(count==3&&!memcmp(bytes,pcmPacket,3));
  if(version>1){buffer[0]=1;bytes=buffer;count=size;assert(!VoiceProtocol::payload(version,bytes,count));}
 }
 assert(!VoiceProtocol::frame(4,pcmPacket,3,0,buffer,sizeof(buffer)));
 assert(!VoiceProtocol::frame(2,pcmPacket,3,0,buffer,18));
 uint8_t oversized[1501]={};const uint8_t *bytes=oversized;size_t length=sizeof(oversized);assert(!VoiceProtocol::payload(1,bytes,length));
 // Exercise the pinned, actual fixed-point encoder/decoder through all framing
 // variants and negotiated rates. This is codec validation, not an I2S mock.
 for(int rate:{8000,12000,16000,24000,48000}){
  int error=0;auto *encoder=opus_encoder_create(rate,1,OPUS_APPLICATION_VOIP,&error);assert(encoder&&error==OPUS_OK);
  auto *decoder=opus_decoder_create(rate,1,&error);assert(decoder&&error==OPUS_OK);
  opus_encoder_ctl(encoder,OPUS_SET_BITRATE(24000));opus_encoder_ctl(encoder,OPUS_SET_COMPLEXITY(3));
  int samples=rate*60/1000;int16_t input[2880],output[5760];double energy=0;
  for(int frame=0;frame<10;frame++){
   for(int i=0;i<samples;i++)input[i]=(int16_t)(9000*std::sin((frame*samples+i)*2*3.141592653589793*440/rate));
   uint8_t opus[1500];int n=opus_encode(encoder,input,samples,opus,sizeof(opus));assert(n>0&&n<=1500);
   assert(opus_packet_get_nb_samples(opus,n,rate)==samples);
   size_t size=VoiceProtocol::frame(frame%3+1,opus,n,frame*60,buffer,sizeof(buffer));const uint8_t *payload=buffer;
   assert(size&&VoiceProtocol::payload(frame%3+1,payload,size));int decoded=opus_decode(decoder,payload,size,output,5760,0);assert(decoded==samples);
   if(frame>=5)for(int i=0;i<decoded;i++)energy+=(double)output[i]*output[i];
  }
  assert(std::sqrt(energy/(samples*5))>1000);opus_encoder_destroy(encoder);opus_decoder_destroy(decoder);
 }
 puts("PASS: official v1/v2/v3 envelopes, endian/length/truncation rejection, actual Opus 1.3.1 fixed-point encode/decode at all five negotiated rates.");
}
