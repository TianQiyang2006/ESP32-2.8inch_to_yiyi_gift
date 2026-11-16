#pragma once
#include <stdint.h>
#include <stddef.h>

namespace MusicPlayback {
// History follows what was actually heard, including manually selected songs.
class History {
    uint16_t items_[200]{};unsigned size_=0,cursor_=0;
public:
    void clear(){size_=cursor_=0;}
    void remember(unsigned track){
        if(size_&&items_[cursor_]==track)return;
        if(size_)size_=cursor_+1;
        if(size_==200){for(unsigned i=1;i<size_;++i)items_[i-1]=items_[i];--size_;}
        items_[size_++]=track;cursor_=size_-1;
    }
    int step(int direction,const uint16_t *list,unsigned count,int current,uint32_t random){
        if(!count)return -1;
        if(direction<0&&size_){if(cursor_)--cursor_;return items_[cursor_];}
        if(direction>=0&&size_&&cursor_+1<size_)return items_[++cursor_];
        unsigned currentIndex=count;
        for(unsigned i=0;i<count;++i)if(int(list[i])==current){currentIndex=i;break;}
        unsigned pick=random%(count-(count>1&&currentIndex<count?1:0));
        if(count>1&&currentIndex<count&&pick>=currentIndex)++pick;
        remember(list[pick]);return list[pick];
    }
};
// Parse LRC timestamps exactly: one/two/three fraction digits, no float drift.
inline bool timestamp(const char *text,size_t size,unsigned &ms){
    size_t i=0;unsigned minutes=0,seconds=0,fraction=0,digits=0;
    while(i<size&&text[i]>='0'&&text[i]<='9'){minutes=minutes*10+text[i++]-'0';if(minutes>=1440)return false;}
    if(!i||i>=size||text[i++]!=':')return false;
    unsigned start=i;
    while(i<size&&text[i]>='0'&&text[i]<='9'){seconds=seconds*10+text[i++]-'0';if(seconds>=60)return false;}
    if(i==start||i-start>2)return false;
    if(i<size&&text[i]=='.'){
        ++i;while(i<size&&text[i]>='0'&&text[i]<='9'&&digits<3){fraction=fraction*10+text[i++]-'0';++digits;}
        if(!digits)return false;
        while(digits<3){fraction*=10;++digits;}
    }
    if(i!=size)return false;
    ms=minutes*60000+seconds*1000+fraction;return true;
}
inline unsigned shifted(unsigned ms,int offset){int64_t result=int64_t(ms)+offset;return result<0?0:result>86400000?86400000:unsigned(result);}
}
