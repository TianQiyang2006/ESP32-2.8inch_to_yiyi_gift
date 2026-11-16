#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Xiaozhi's big-endian v2/v3 envelopes. No packed-struct casts, so alignment
// and truncated or oversized packets are checked before reading any payload.
namespace VoiceProtocol {
constexpr size_t MaxOpus=1500;
inline bool rateValid(int rate){return rate==8000||rate==12000||rate==16000||rate==24000||rate==48000;}
inline size_t frame(int version,const uint8_t *opus,size_t size,uint32_t at,uint8_t *out,size_t capacity){
 if(version<1||version>3||!opus||size<1||size>MaxOpus)return 0;
 size_t offset=version==2?16:version==3?4:0;if(capacity<size+offset)return 0;
 memset(out,0,offset);
 if(version==2){out[1]=2;for(int i=0;i<4;i++)out[8+i]=at>>(24-i*8);out[14]=size>>8;out[15]=size;}
 else if(version==3){out[2]=size>>8;out[3]=size;}
 memcpy(out+offset,opus,size);return size+offset;
}
inline bool payload(int version,const uint8_t *&bytes,size_t &size){
 if(version<1||version>3||!bytes)return false;
 size_t offset=version==2?16:version==3?4:0;if(size<=offset)return false;
 if(version==2){
  if(bytes[0]!=0||bytes[1]!=2||bytes[2]!=0||bytes[3]!=0)return false;
  uint32_t declared=((uint32_t)bytes[12]<<24)|((uint32_t)bytes[13]<<16)|((uint32_t)bytes[14]<<8)|bytes[15];if(declared!=size-offset)return false;
 }else if(version==3){if(bytes[0]!=0||((size_t)bytes[2]<<8|bytes[3])!=size-offset)return false;}
 if(size-offset>MaxOpus)return false;bytes+=offset;size-=offset;return true;
}
}
