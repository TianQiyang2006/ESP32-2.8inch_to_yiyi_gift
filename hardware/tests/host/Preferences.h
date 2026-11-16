#pragma once
#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

// An explicit test directory makes values survive process exit, unlike RAM maps.
class Preferences {
public:
 static inline unsigned failNextWrites=0;
 static inline size_t partialNextWriteBytes=0;
 static inline bool failBegin=false;
 bool begin(const char *name,bool readOnly=false){name_=name?name:"";readOnly_=readOnly;opened_=!failBegin&&!name_.empty();return opened_;}
 void end(){opened_=false;}
 bool isKey(const char *key){if(!opened_)return false;if(directory())return std::filesystem::exists(path(key));return blobs_.count(qualified(key))!=0;}
 size_t getBytesLength(const char *key){return read(key).size();}
 size_t getBytes(const char *key,void *destination,size_t maximum){auto bytes=read(key);if(!destination||bytes.size()>maximum)return 0;if(!bytes.empty())memcpy(destination,bytes.data(),bytes.size());return bytes.size();}
 size_t putBytes(const char *key,const void *source,size_t size){
  if(!opened_||readOnly_||!source||!size)return 0;
  if(failNextWrites){--failNextWrites;return 0;}
  auto *start=static_cast<const uint8_t*>(source);
  size_t count=partialNextWriteBytes?std::min(size,partialNextWriteBytes):size;
  bool partial=partialNextWriteBytes!=0;partialNextWriteBytes=0;
  std::vector<uint8_t> bytes(start,start+count);
  if(directory()){
   std::error_code error;std::filesystem::create_directories(path(key).parent_path(),error);if(error)return 0;
   std::ofstream file(path(key),std::ios::binary|std::ios::trunc);
   file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.flush();if(!file)return 0;
  }else blobs_[qualified(key)]=bytes;
  return partial?0:size;
 }
 bool getBool(const char *key,bool fallback=false){auto bytes=read(key);return bytes.size()==1?bytes[0]!=0:fallback;}
 size_t putBool(const char *key,bool value){uint8_t raw=value?1:0;return putBytes(key,&raw,1);}
 uint16_t getUShort(const char *key,uint16_t fallback=0){auto bytes=read(key);return bytes.size()==2?(uint16_t)(bytes[0]|(bytes[1]<<8)):fallback;}
 size_t putUShort(const char *key,uint16_t value){uint8_t raw[]={(uint8_t)value,(uint8_t)(value>>8)};return putBytes(key,raw,2);}
 uint8_t getUChar(const char *key,uint8_t fallback=0){auto bytes=read(key);return bytes.size()==1?bytes[0]:fallback;}
 size_t putUChar(const char *key,uint8_t value){return putBytes(key,&value,1);}
 uint32_t getUInt(const char *key,uint32_t fallback=0){auto bytes=read(key);return bytes.size()==4?(uint32_t)(bytes[0]|(bytes[1]<<8)|(bytes[2]<<16)|((uint32_t)bytes[3]<<24)):fallback;}
 size_t putUInt(const char *key,uint32_t value){uint8_t raw[]={(uint8_t)value,(uint8_t)(value>>8),(uint8_t)(value>>16),(uint8_t)(value>>24)};return putBytes(key,raw,4);}
private:
 std::string name_;bool opened_=false,readOnly_=false;
 static inline std::map<std::string,std::vector<uint8_t>> blobs_;
 const char *directory()const{const char *value=std::getenv("GIFT_HOST_PREFS_DIR");return value&&*value?value:nullptr;}
 std::string qualified(const char *key)const{return name_+"/"+key;}
 std::filesystem::path path(const char *key)const{return std::filesystem::path(directory())/name_/(std::string(key)+".bin");}
 std::vector<uint8_t> read(const char *key){
  if(!opened_)return {};
  if(directory()){std::ifstream file(path(key),std::ios::binary);if(!file)return {};return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
  auto found=blobs_.find(qualified(key));return found==blobs_.end()?std::vector<uint8_t>{}:found->second;
 }
};
