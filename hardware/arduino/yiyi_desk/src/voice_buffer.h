#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace VoiceBuffer {
struct Entry {uint16_t size;uint16_t rate;uint32_t generation;};
// Bounded byte/descriptor ring. Caller supplies memory and synchronization.
// Compressed packets consume their actual size, not a 1500-byte worst case.
class Ring {
    uint8_t *bytes_=nullptr;Entry *entries_=nullptr;
    size_t capacity_=0,slots_=0,read_=0,write_=0,used_=0,head_=0,tail_=0,count_=0;
public:
    void init(uint8_t *bytes,size_t capacity,Entry *entries,size_t slots){
        bytes_=bytes;capacity_=capacity;entries_=entries;slots_=slots;clear();
    }
    void clear(){read_=write_=used_=head_=tail_=count_=0;}
    size_t count() const{return count_;}
    size_t used() const{return used_;}
    bool push(const uint8_t *data,size_t size,uint16_t rate,uint32_t generation){
        if(!bytes_||!entries_||!data||!size||size>1500||count_==slots_||size>capacity_-used_)return false;
        size_t first=size<capacity_-write_?size:capacity_-write_;
        memcpy(bytes_+write_,data,first);if(first<size)memcpy(bytes_,data+first,size-first);
        entries_[tail_]={(uint16_t)size,rate,generation};
        write_=(write_+size)%capacity_;used_+=size;tail_=(tail_+1)%slots_;++count_;return true;
    }
    bool pop(uint8_t *data,size_t capacity,Entry &entry){
        if(!count_)return false;entry=entries_[head_];if(capacity<entry.size)return false;
        size_t first=entry.size<capacity_-read_?entry.size:capacity_-read_;
        memcpy(data,bytes_+read_,first);if(first<entry.size)memcpy(data+first,bytes_,entry.size-first);
        read_=(read_+entry.size)%capacity_;used_-=entry.size;head_=(head_+1)%slots_;--count_;return true;
    }
};
}
