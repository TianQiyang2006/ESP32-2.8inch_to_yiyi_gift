#pragma once
#include "Arduino.h"
#include <fstream>
#include <filesystem>
#include <memory>
#include <limits>
class File {
 std::shared_ptr<std::fstream> stream;
 std::shared_ptr<size_t> remaining;
public:
 File(){}File(const std::string &path,const char *mode,size_t limit=std::numeric_limits<size_t>::max()):stream(std::make_shared<std::fstream>(path,std::ios::binary|(mode[0]=='r'?std::ios::in:std::ios::out|std::ios::trunc))),remaining(std::make_shared<size_t>(limit)){}
 explicit operator bool()const{return stream&&stream->is_open();}
 size_t write(const uint8_t *bytes,size_t len){if(!*this)return 0;size_t count=std::min(len,*remaining);stream->write((const char*)bytes,count);*remaining-=count;return stream->good()?count:0;}size_t write(uint8_t byte){return write(&byte,1);}
 int read(){return stream->get();}int available(){return stream&&stream->peek()!=std::char_traits<char>::eof();}size_t readBytes(char *buf,size_t len){stream->read(buf,len);return stream->gcount();}
 size_t size(){auto pos=stream->tellg();stream->seekg(0,std::ios::end);auto end=stream->tellg();stream->seekg(pos);return (size_t)end;}
 void flush(){stream->flush();}void close(){stream->close();}
};
class HostFS {
public:
 std::string directory="build/host-lvgl/store";
 bool failWrites=false;
 size_t writeLimit=std::numeric_limits<size_t>::max();
 std::string failRemovePath,failRenameFrom,failRenameTo;
 void resetFaults(){failWrites=false;writeLimit=std::numeric_limits<size_t>::max();failRemovePath.clear();failRenameFrom.clear();failRenameTo.clear();}
 bool begin(bool,const char*,int,const char*){std::filesystem::create_directories(directory);return true;}bool format(){return true;}
 File open(const char *name,const char *mode){return failWrites&&mode[0]=='w'?File():File(directory+name,mode,mode[0]=='w'?writeLimit:std::numeric_limits<size_t>::max());}bool exists(const char *name){return std::filesystem::exists(directory+name);}
 bool remove(const char *name){if(failRemovePath==name)return false;std::error_code error;std::filesystem::remove(directory+name,error);return !error;}bool rename(const char *from,const char *to){if(failRenameFrom==from||failRenameTo==to)return false;std::error_code error;std::filesystem::rename(directory+from,directory+to,error);return !error;}
};
inline HostFS LittleFS;
