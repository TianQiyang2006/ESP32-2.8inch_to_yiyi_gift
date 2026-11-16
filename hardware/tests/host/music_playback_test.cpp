#include "../../arduino/yiyi_desk/src/music_memory.h"
#include "../../arduino/yiyi_desk/src/music_playback.h"
#include <cassert>
#include <iostream>

uint32_t HostMillis=0;int HostBatteryMv=0;
int main(int argc,char **argv){
    using namespace MusicMemory;
    Preferences preferences;assert(preferences.begin("desk-music"));
    Journal journal;State s;
    std::string action=argc>1?argv[1]:"unit";
    if(action=="write"){
        strcpy(s.track,"stable-song-id");strcpy(s.playlist,"stable-list-name");
        s.position=86345;s.duration=269794;s.mode=2;s.volume=5;
        assert(journal.save(preferences,s));std::cout<<"PASS: checkpoint persisted\n";return 0;
    }
    if(action=="read"){
        assert(journal.load(preferences,s));
        assert(!strcmp(s.track,"stable-song-id")&&!strcmp(s.playlist,"stable-list-name"));
        assert(s.position==86345&&s.duration==269794&&s.mode==2&&s.volume==5);
        std::cout<<"PASS: song ID, playlist name, position, mode and volume survive process restart\n";return 0;
    }
    assert(!journal.load(preferences,s));
    strcpy(s.track,"song-2");strcpy(s.playlist,"my-list");s.position=17000;s.duration=90000;s.mode=1;
    assert(journal.save(preferences,s));
    auto changed=s;changed.position=27000;
    Preferences::failNextWrites=1;assert(!journal.save(preferences,changed));
    Journal afterFailure;assert(afterFailure.load(preferences,s)&&s.position==17000);
    Preferences::partialNextWriteBytes=35;assert(!afterFailure.save(preferences,changed));
    Journal afterTear;assert(afterTear.load(preferences,s)&&s.position==17000);
    assert(afterTear.save(preferences,changed));
    Journal afterCommit;assert(afterCommit.load(preferences,s)&&s.position==27000);
    Record record;assert(preferences.getBytes("play1",&record,sizeof(record))==sizeof(record));
    record.state.position^=1;assert(preferences.putBytes("play1",&record,sizeof(record))==sizeof(record));
    Journal afterCorruption;assert(afterCorruption.load(preferences,s)&&s.position==17000);
    s.mode=3;assert(!afterCorruption.save(preferences,s));s.mode=2;s.volume=101;assert(!valid(s));
    s.volume=100;memset(s.track,'x',sizeof(s.track));assert(!valid(s));
    std::cout<<"PASS: failed and torn writes recover the older checkpoint, CRC rejects corrupt records\n";
    MusicPlayback::History history;uint16_t list[]={19,3,87,12};history.remember(19);
    int current=19;
    for(unsigned random=0;random<500;++random){int next=history.step(1,list,4,current,random);assert(next>=0&&next!=current);current=next;}
    assert(history.step(-1,list,4,current,0)!=current);
    history.clear();history.remember(19);int second=history.step(1,list,4,19,2);
    assert(history.step(-1,list,4,second,0)==19);
    assert(history.step(1,list,4,19,0)==second);
    history.remember(87);assert(history.step(-1,list,4,87,0)==second);
    history.clear();uint16_t one[]={7};assert(history.step(1,one,1,7,0)==7);
    assert(history.step(1,one,0,7,0)==-1);
    std::cout<<"PASS: shuffle has no adjacent repeat, previous/forward follow real listening history\n";
    unsigned ms=0;
    for(auto pair:{std::pair<const char*,unsigned>{"00:01.1",1100},{"02:03.45",123450},{"12:34.567",754567},{"00:01",1000}}){assert(MusicPlayback::timestamp(pair.first,strlen(pair.first),ms));assert(ms==pair.second);}
    for(auto bad:{"ar:artist","01:60.00","00:01.1234","00:01.","00:","01:-1","999999999999:01","00:01x"})assert(!MusicPlayback::timestamp(bad,strlen(bad),ms));
    assert(MusicPlayback::shifted(1000,-1500)==0&&MusicPlayback::shifted(1000,250)==1250);
    std::cout<<"PASS: LRC timestamp precision, offset bounds, malformed tags rejected\n";
}
