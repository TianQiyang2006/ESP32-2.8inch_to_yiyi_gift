#include "music_player.h"
#include "music_playback.h"
#ifndef GIFT_HOST_TEST
#include "audio_board.h"
#include "music_ima.h"
#include "music_memory.h"
#include "radio_player.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <algorithm>

extern bool apSuspended;
namespace MusicPlayer {
namespace {
constexpr unsigned Capacity=262144,MaxTracks=200;
constexpr unsigned SamplesPerMs=MusicIma::Rate/1000;
Track tracks[MaxTracks],emptyTrack;
Playlist lists[8],emptyList;
struct Lyric { unsigned ms=0; String text; };
Lyric lines[400]; unsigned lineCount=0;
unsigned songs=0,listCount=0;
SemaphoreHandle_t lock=nullptr;
SemaphoreHandle_t memoryLock=nullptr;
MusicMemory::Journal memoryJournal;
MusicMemory::State rememberedState;
MusicPlayback::History history;
TaskHandle_t networkTask=nullptr,outputTask=nullptr,lyricsTask=nullptr;
String pendingLyrics;unsigned lyricsGeneration=0;
String base,error;
uint8_t *ring=nullptr;
unsigned head=0,tail=0,filled=0,ringGeneration=0;
std::atomic<unsigned> generation{0},offset{0},position{0},duration{0},underruns{0};
std::atomic<unsigned> volume{40},mode{0};
std::atomic<int> selected{-1},selectedList{0};
std::atomic<bool> ready{false},playing{false},paused{false},loading{false},eof{false},buffering{false},remembered{false},memoryFault{false};
std::atomic<bool> catalogRequested{true};
std::atomic<uint32_t> saveAt{0},retryAt{0},memoryRevision{0},savedRevision{0},lastMemorySave{0};
std::atomic<unsigned> retryCount{0},retryPosition{0};
constexpr unsigned RetryMax=6;
constexpr uint32_t RetryDelayMs[RetryMax]={2000,4000,8000,15000,30000,30000};
void take(){xSemaphoreTake(lock,portMAX_DELAY);}
void give(){xSemaphoreGive(lock);}
void markMemory(){saveAt=millis();++memoryRevision;}
void saveMemory(bool force=false){
 if(!memoryLock)return;
 bool dirty=memoryRevision!=savedRevision;
 bool progress=playing&&!paused&&!loading&&millis()-lastMemorySave.load()>=15000;
 if(!force&&((!dirty&&!progress)||(dirty&&millis()-saveAt.load()<750)))return;
 if(!force&&memoryFault&&millis()-lastMemorySave.load()<5000)return;
 if(xSemaphoreTake(memoryLock,force?portMAX_DELAY:0)!=pdTRUE)return;
 uint32_t revision=memoryRevision.load(),epoch=generation.load();
 MusicMemory::State snapshot=ready.load()?MusicMemory::State{}:rememberedState;snapshot.volume=volume;snapshot.mode=mode;
 int i=selected.load(),list=selectedList.load();
 if(ready&&i>=0&&unsigned(i)<songs){
  snprintf(snapshot.track,sizeof(snapshot.track),"%s",tracks[i].id.c_str());
  snapshot.position=position.load()/SamplesPerMs;snapshot.duration=duration;
 }
 if(ready&&list>=0&&unsigned(list)<listCount)snprintf(snapshot.playlist,sizeof(snapshot.playlist),"%s",lists[list].name.c_str());
 if(generation!=epoch){xSemaphoreGive(memoryLock);return;}
 Preferences pref;bool ok=false;
 if(pref.begin("desk-music",false)){ok=memoryJournal.save(pref,snapshot);pref.end();}
 memoryFault=!ok;lastMemorySave=millis();
 if(ok)savedRevision=revision;
 xSemaphoreGive(memoryLock);
}
void fail(const String &value){take();error=value;give();loading=false;buffering=false;playing=false;}
// 可恢复故障：保留曲目/播放位置，按退避时间自动重试（P1-1）。网络任务轮询
// retryAt；Wi-Fi 恢复或点击错误文案会把 retryAt 提前到当前时刻。
void failRetry(const String &value){
 retryPosition=position.load()/SamplesPerMs;fail(value);
 unsigned n=retryCount.load();
 if(n<RetryMax){retryCount=n+1;retryAt=millis()+RetryDelayMs[n];}else{retryCount=0;retryAt=0;}
}
// 起播/切歌统一入口。ms 表示从该毫秒位置继续（重试续播用）；Wi-Fi 未连接时
// 立即给出明确错误，不再进入无限 loading（P1-2）。
struct MediaControlGuard {~MediaControlGuard(){AudioBoard::unlockMediaControl();}};
void playAt(unsigned i,unsigned ms,bool automatic=false,bool keepPaused=false,unsigned expectedGeneration=0){
 if(!ready||i>=songs)return;
 if(!AudioBoard::lockMediaControl())return;
 MediaControlGuard mediaGuard;
 // A stale EOF/retry must never stop a newly selected radio station. Both
 // sources serialize handoff here, then validate the generation and owner.
 if(automatic){if(generation.load()!=expectedGeneration||!AudioBoard::streamSelected(AudioBoard::StreamSource::Music))return;}
 else{RadioPlayer::stop();AudioBoard::selectStream(AudioBoard::StreamSource::Music);}
 retryAt=0;if(!automatic)retryCount=0;retryPosition=ms;remembered=false;
 take();error="";lineCount=0;history.remember(i);give();
 if(selected.load()!=int(i))duration=0;
 if(WiFi.status()!=WL_CONNECTED){
  take();error="网络未连接，请先连接 Wi-Fi";give();
  ++generation;                                    // 旧曲进度/数据失效
  loading=false;buffering=false;playing=false;paused=keepPaused;selected=i;offset=ms;position=ms*SamplesPerMs;
  if(!keepPaused){retryAt=millis()+3000;if(!automatic)retryCount=1;}
  markMemory();
  return;
 }
 selected=i;offset=ms;paused=keepPaused;loading=true;buffering=true;playing=true;position=ms*SamplesPerMs;++generation;markMemory();
}
struct PsAllocator:ArduinoJson::Allocator {
 void *allocate(size_t n)override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
 void deallocate(void *p)override{heap_caps_free(p);}
 void *reallocate(void *p,size_t n)override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
} allocator;
String encoded(const String &value){
 const char *hex="0123456789ABCDEF";String out;out.reserve(value.length()*3);
 for(unsigned i=0;i<value.length();++i){uint8_t c=value[i];if(isalnum(c)||c=='-'||c=='_'||c=='.')out+=(char)c;else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}
 return out;
}
bool getJson(const String &url,JsonDocument &json){
 HTTPClient http;http.setConnectTimeout(6000);http.setTimeout(30000);http.useHTTP10(true);
 if(!http.begin(url))return false;int code=http.GET();
 bool ok=code==200&&!deserializeJson(json,http.getStream());http.end();return ok;
}
bool loadCatalog(){
 JsonDocument json(&allocator);
 if(!getJson(base+"/app/catalog",json))return false;
 unsigned n=0;
 for(JsonObject item:json["songs"].as<JsonArray>()){
  if(n==MaxTracks)break;
  tracks[n].id=String(item["id"]|"");tracks[n].title=String(item["title"]|"");tracks[n].artist=String(item["artist"]|"");
  if(!tracks[n].id.isEmpty())++n;
 }
 songs=n;unsigned k=0;
 for(JsonObject item:json["playlists"].as<JsonArray>()){
  if(k==8)break;auto &list=lists[k];list.name=String(item["name"]|"");list.count=0;
  for(JsonObject song:item["songs"].as<JsonArray>()){
   String id=song["id"]|"";
   for(unsigned i=0;i<songs&&list.count<200;++i)if(tracks[i].id==id){list.tracks[list.count++]=i;break;}
  }
  if(list.count)++k;
 }
 if(!k&&songs){lists[0].name="全部音乐";lists[0].count=songs;for(unsigned i=0;i<songs;++i)lists[0].tracks[i]=i;k=1;}
 listCount=k;
 // Put the user's main collection first, independent of the database sort.
 for(unsigned i=0;i<k;++i)if(lists[i].name=="酷狗我喜欢"&&i){std::swap(lists[0],lists[i]);break;}
 ready=songs&&listCount;
 if(ready){
  take();error="";give();
  for(unsigned i=0;i<listCount;++i)if(lists[i].name==rememberedState.playlist){selectedList=i;break;}
  if(selected<0)for(unsigned i=0;i<songs;++i)if(tracks[i].id==rememberedState.track){
   selected=i;duration=rememberedState.duration;
   unsigned ms=rememberedState.position;
   if(duration&&ms>=duration.load())ms=0;
   offset=ms;position=ms*SamplesPerMs;paused=true;remembered=true;
   take();history.remember(i);give();break;
  }
 }
 return ready;
}
void loadLyrics(const String &url,unsigned epoch){
 take();if(generation!=epoch){give();return;}lineCount=0;give();if(url.isEmpty())return;
 HTTPClient http;http.setConnectTimeout(2000);http.setTimeout(3000);http.useHTTP10(true);
 if(!http.begin(url))return;
 if(http.GET()==200&&http.getSize()>=0&&http.getSize()<=65536){
  String raw=http.getString();int start=0,lyricOffset=0;
  int tag=raw.indexOf("[offset:");
  if(tag>=0){int parsed=0;if(sscanf(raw.c_str()+tag,"[offset:%d]",&parsed)==1&&parsed>=-3600000&&parsed<=3600000)lyricOffset=parsed;}
  while(start<(int)raw.length()&&generation==epoch){
   int end=raw.indexOf('\n',start);if(end<0)end=raw.length();String row=raw.substring(start,end);row.trim();start=end+1;
   int last=-1;
   for(int p=row.indexOf('[');p>=0&&p<(int)row.length()&&row[p]=='[';){int close=row.indexOf(']',p);if(close<0)break;last=close;p=close+1;}
   if(last<0)continue;String words=row.substring(last+1);words.trim();
   for(int p=0;p<=last;){int open=row.indexOf('[',p),close=row.indexOf(']',open);if(open<0||close<0)break;
    unsigned ms=0;
    if(MusicPlayback::timestamp(row.c_str()+open+1,close-open-1,ms)){
     take();if(generation==epoch&&lineCount<400){lines[lineCount].ms=MusicPlayback::shifted(ms,lyricOffset);lines[lineCount++].text=words;}give();
    }p=close+1;
   }
  }
  take();std::sort(lines,lines+lineCount,[](const Lyric&a,const Lyric&b){return a.ms<b.ms;});give();
 }
 http.end();
}
void lyricsWorker(void*){
 for(;;){
  take();String url=pendingLyrics;unsigned epoch=lyricsGeneration;pendingLyrics="";give();
  if(!url.isEmpty()&&generation==epoch)loadLyrics(url,epoch);
  vTaskDelay(pdMS_TO_TICKS(100));
 }
}
void output(void*){
 static int16_t pcm[MusicIma::BlockSamples];
 uint8_t block[MusicIma::BlockBytes];unsigned cursor=MusicIma::BlockSamples,epoch=0;bool primed=false;
 for(;;){
  unsigned wanted=generation.load();
  if(epoch!=wanted){epoch=wanted;primed=false;cursor=MusicIma::BlockSamples;}
  if(!playing||paused||loading||AudioBoard::playing()){vTaskDelay(pdMS_TO_TICKS(10));continue;}
  take();unsigned available=filled,gen=ringGeneration;bool done=eof;give();
  if(gen!=epoch){vTaskDelay(pdMS_TO_TICKS(5));continue;}
  if(!primed){if(available<16384&&!done){buffering=true;vTaskDelay(pdMS_TO_TICKS(10));continue;}primed=true;buffering=false;}
  if(cursor==MusicIma::BlockSamples){
   if(available<MusicIma::BlockBytes){
    if(done){
     if(available){fail("音频数据不完整，请重试");continue;}
     if(generation.load()!=epoch)continue;
     if(mode==1&&selected>=0)playAt(selected.load(),0,true,false,epoch);
     else{
      const auto &list=lists[selectedList.load()];int target=-1;
      if(mode==2){take();target=history.step(1,list.tracks,list.count,selected.load(),esp_random());give();}
      else if(list.count){int current=-1;for(unsigned i=0;i<list.count;++i)if(int(list.tracks[i])==selected.load()){current=int(i);break;}target=list.tracks[(current+1)%list.count];}
      if(target>=0)playAt(unsigned(target),0,true,false,epoch);
     }
    }else{++underruns;primed=false;buffering=true;}
    vTaskDelay(pdMS_TO_TICKS(10));continue;
   }
   take();if(ringGeneration!=epoch){give();continue;}
   for(unsigned i=0;i<MusicIma::BlockBytes;++i)block[i]=ring[(tail+i)%Capacity];
   tail=(tail+MusicIma::BlockBytes)%Capacity;filled-=MusicIma::BlockBytes;give();
   if(!MusicIma::decode(block,pcm)){fail("音频解码失败，请重试");continue;}
   cursor=0;
  }
  unsigned count=std::min(256U,MusicIma::BlockSamples-cursor);
  if(generation!=epoch)continue;
  if(!AudioBoard::streamPcm(pcm+cursor,count,volume,MusicIma::Rate)){
   String fault=AudioBoard::error();if(!fault.isEmpty())fail(fault);
   vTaskDelay(pdMS_TO_TICKS(5));continue;
  }
  if(generation==epoch){cursor+=count;position+=count;}
 }
}
void network(void*){
 unsigned active=0;uint8_t block[4096];uint32_t catalogAt=0;bool wifiWasDown=false;
 for(;;){
  saveMemory();
  if(WiFi.status()!=WL_CONNECTED){
   wifiWasDown=true;
   if(!ready){take();error="网络未连接，请先连接 Wi-Fi";give();}
   // 起播/加载中失去网络：立即明确报错，不再停在无声的 loading 状态（P1-2）。
   if(playing.load()&&loading.load()){fail("网络未连接，正在等待恢复");retryAt=millis();retryCount=0;}
   vTaskDelay(pdMS_TO_TICKS(200));continue;
  }
  if(wifiWasDown){
   wifiWasDown=false;retryCount=0;
   if(!ready){catalogRequested=true;take();error="正在连接曲库…";give();}
   // Wi-Fi 恢复：未完成的播放立即从断点自动续播（P1-1）。
   bool resume=selected.load()>=0&&!playing.load()&&!paused.load();
   if(resume){take();resume=!error.isEmpty();give();}
   if(resume)retryAt=millis();
  }
  // 可恢复故障的自动重试：failRetry() 已排好退避时刻；Wi-Fi 恢复或点击
  // 错误文案（music_page 的 Retry）会把 retryAt 提前到当前时刻（P1-1）。
  unsigned retryEpoch=generation.load();
  if(retryAt.load()&&!playing.load()&&!paused.load()&&selected.load()>=0){
   uint32_t at=retryAt.load();
   if((int32_t)(millis()-at)>=0){retryAt=0;playAt((unsigned)selected.load(),retryPosition.load(),true,false,retryEpoch);continue;}
  }
  if(!ready){
   bool requested=catalogRequested.exchange(false);
   if(!requested&&millis()-catalogAt<3000){vTaskDelay(pdMS_TO_TICKS(100));continue;}catalogAt=millis();
   if(!loadCatalog()){take();error="曲库连接失败，正在重试";give();vTaskDelay(pdMS_TO_TICKS(500));}continue;
  }
  unsigned epoch=generation.load();int index=selected.load();
  if(epoch==active||index<0||!playing){vTaskDelay(pdMS_TO_TICKS(30));continue;}
  active=epoch;loading=true;eof=false;
  take();head=tail=filled=0;ringGeneration=epoch;error="";give();
  unsigned start=offset.load();position=start*SamplesPerMs;duration=0;underruns=0;
  JsonDocument info(&allocator);
  if(!getJson(base+"/app/track/"+encoded(tracks[index].id)+"?format=ima32",info)){if(generation==epoch)failRetry("歌曲加载失败，点击重试");continue;}
  if(generation!=epoch)continue;
  unsigned length=info["bytes"]|0;duration=info["duration_ms"]|0;
  if(!length||length%MusicIma::BlockBytes||int(info["rate"]|0)!=MusicIma::Rate||String(info["codec"]|"")!="ima_adpcm"||int(info["block_bytes"]|0)!=MusicIma::BlockBytes||int(info["block_samples"]|0)!=MusicIma::BlockSamples){fail("音源格式不支持");continue;}
  if(start>=duration)start=0;
  unsigned firstBlock=(uint64_t(start)*MusicIma::Rate/1000)/MusicIma::BlockSamples;
  position=firstBlock*MusicIma::BlockSamples;
  String pcmUrl=info["pcm_url"]|"",lyricUrl=info["lyric_url"]|"";
  // Lyrics must never delay starting the audio or refilling its buffer.
  take();if(generation==epoch){pendingLyrics=lyricUrl;lyricsGeneration=epoch;}give();
  if(generation!=epoch)continue;
  unsigned received=firstBlock*MusicIma::BlockBytes;loading=false;retryAt=0;
  int attempts=0;
  while(generation==epoch&&playing&&received<length){
   HTTPClient http;http.setConnectTimeout(6000);http.setTimeout(4000);http.useHTTP10(true);
   if(!http.begin(pcmUrl)){failRetry("无法连接音源，点击重试");break;}
   if(received)http.addHeader("Range","bytes="+String(received)+"-");
   int code=http.GET();
   if(code!=(received?206:200)){http.end();if(++attempts>=4){failRetry("音源连接中断，点击重试");break;}vTaskDelay(pdMS_TO_TICKS(300));continue;}
   auto *stream=http.getStreamPtr();uint32_t last=millis();
   while(generation==epoch&&playing&&received<length){
    saveMemory();
    take();unsigned room=Capacity-filled;give();
    if(room<2){last=millis();vTaskDelay(pdMS_TO_TICKS(10));continue;}
    int available=stream->available();
    if(available<=0){
     if(WiFi.status()!=WL_CONNECTED)break;   // 断链立即退出，由外层继续/重试
     if(!http.connected()||millis()-last>6000)break;
     vTaskDelay(pdMS_TO_TICKS(5));continue;
    }
    unsigned need=std::min(std::min(room,(unsigned)sizeof(block)),std::min((unsigned)available,length-received));
    int got=stream->read(block,need);
    if(got<=0){vTaskDelay(pdMS_TO_TICKS(1));continue;}   // 消除 socket 忙等（TASK_WDT 相关）
    last=millis();attempts=0;retryCount=0;
    take();if(generation==epoch){for(int i=0;i<got;++i)ring[(head+i)%Capacity]=block[i];head=(head+got)%Capacity;filled+=got;received+=got;}give();
   }
   http.end();
   // Resume a broken TCP transfer on a whole 16-bit sample boundary.
   if((received&1U)&&generation==epoch){take();if(filled){head=(head+Capacity-1)%Capacity;--filled;--received;}give();}
   if(received<length&&generation==epoch&&playing){
    if(WiFi.status()!=WL_CONNECTED){
     // Wi-Fi 断开：先保持 epoch/环形缓冲等待恢复（最多 60s），恢复后继续用
     // Range 从 received 续传（P1-1）。5s 后给出可见提示，但不打断缓冲播放。
     uint32_t downAt=millis();bool noted=false;
     while(generation==epoch&&playing&&WiFi.status()!=WL_CONNECTED){
      saveMemory();
      vTaskDelay(pdMS_TO_TICKS(200));
      uint32_t elapsed=millis()-downAt;
      if(!noted&&elapsed>=5000){take();error="网络已断开，正在等待恢复";give();noted=true;}
      if(elapsed>=60000)break;
     }
     if(generation==epoch&&playing&&WiFi.status()!=WL_CONNECTED){
      failRetry("网络已断开，点击重试");          // 超时：回落到错误提示+自动重试
     }else if(generation==epoch&&playing){
      take();error="";give();attempts=0;          // 网络恢复：清提示/重置计数后 Range 续传
     }
    }else if(++attempts>=4)failRetry("音源连接中断，点击重试");
   }
  }
  if(generation==epoch&&received>=length)eof=true;
 }
}
}
void begin(){
 if(lock)return;lock=xSemaphoreCreateMutex();if(!lock)return;
 memoryLock=xSemaphoreCreateMutex();if(!memoryLock){fail("音乐存储初始化失败");return;}
 Preferences prefs;if(prefs.begin("music",true)){base=prefs.getString("server_url","");prefs.end();}
 // The independent endpoint shares the gateway's private path, on its own port.
 base.replace(":2233",":2244");while(base.endsWith("/"))base.remove(base.length()-1);
 Preferences saved;if(saved.begin("desk-music",true)){
  volume=std::min(saved.getUInt("volume",40),100U);
  if(memoryJournal.load(saved,rememberedState)){volume=rememberedState.volume;mode=rememberedState.mode;}
  saved.end();
 }
 ring=(uint8_t*)heap_caps_malloc(Capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!ring||base.isEmpty()){fail("音乐服务尚未配置");return;}
 if(xTaskCreatePinnedToCore(output,"music-pcm",4096,nullptr,4,&outputTask,0)!=pdPASS){fail("音乐任务启动失败");return;}
 if(xTaskCreatePinnedToCore(lyricsWorker,"music-lrc",6144,nullptr,1,&lyricsTask,0)!=pdPASS){vTaskDelete(outputTask);outputTask=nullptr;fail("歌词任务启动失败");return;}
 if(xTaskCreatePinnedToCore(network,"music-net",10240,nullptr,1,&networkTask,0)!=pdPASS){vTaskDelete(outputTask);outputTask=nullptr;vTaskDelete(lyricsTask);lyricsTask=nullptr;fail("音乐任务启动失败");}
}
void open(){begin();if(networkTask&&!ready){catalogRequested=true;take();error=WiFi.status()==WL_CONNECTED?"正在连接曲库…":"网络未连接，请先连接 Wi-Fi";give();}}
void stop(){playing=false;paused=selected>=0;loading=false;buffering=false;retryAt=0;retryCount=0;++generation;AudioBoard::endStream();markMemory();saveMemory(true);}
void play(unsigned i){playAt(i,0);}
void selectPlaylist(unsigned i){if(i<listCount&&int(i)!=selectedList.load()){selectedList=i;take();history.clear();give();markMemory();}}
void next(int direction){
 if(!ready||!listCount)return;const auto&list=lists[selectedList];if(!list.count)return;
 if(mode==2){take();int target=history.step(direction,list.tracks,list.count,selected.load(),esp_random());give();if(target>=0)play(target);return;}
 // 当前曲不在本歌单时：next 从第 0 首、previous 从最后一首开始（P1-4）。
 int current=-1;
 for(unsigned i=0;i<list.count;++i)if((int)list.tracks[i]==selected.load()){current=(int)i;break;}
 int target;
 if(current<0)target=direction>=0?0:(int)list.count-1;
 else target=(current+(int)list.count+(direction>=0?1:-1))%(int)list.count;
 play(list.tracks[target]);
}
void toggle(){
 if(!ready){open();return;}
 if(playing.load()){
  if(paused.load()){
   if(!AudioBoard::lockMediaControl())return;
   MediaControlGuard mediaGuard;RadioPlayer::stop();AudioBoard::selectStream(AudioBoard::StreamSource::Music);paused=false;markMemory();return;
  }
  paused=!paused.load();if(paused.load())AudioBoard::endStream();markMemory();
 }else if(selected.load()>=0){
  // 恢复/重试：从上次已播放位置继续；已播到结尾则从本曲开头重来（P1-1）。
  unsigned pos=position.load()/SamplesPerMs,length=duration.load();
  if(length&&pos>=length)pos=0;
  playAt((unsigned)selected.load(),pos);
 }else if(ready.load()&&lists[selectedList.load()].count)play(lists[selectedList.load()].tracks[0]);
}
void seek(unsigned ms){if(selected<0||!duration)return;bool held=paused.load();playAt(selected.load(),std::min(ms,duration.load()>100?duration.load()-100:0U),false,held);}
void setVolume(unsigned value){value=std::min(value,100U);if(volume!=value){volume=value;markMemory();}}
void setMode(unsigned value){if(value<3&&mode!=value){mode=value;take();history.clear();if(selected>=0)history.remember(selected.load());give();markMemory();}}
void cycleMode(){setMode((mode+1)%3);}
State state(){
 State s;s.ready=ready;s.playing=playing;s.paused=paused;s.loading=loading;s.position=position/SamplesPerMs;s.duration=duration;s.track=selected;s.playlist=selectedList;s.volume=volume;s.mode=mode;s.underruns=underruns;
 s.buffering=buffering;s.remembered=remembered;s.saveFailed=memoryFault;
 if(lock){take();s.error=error;s.buffered=filled;s.lyrics=lineCount>0;for(unsigned i=0;i<lineCount&&lines[i].ms<=s.position;++i)s.lyricIndex=i;give();}return s;
}
unsigned trackCount(){return ready?songs:0;}
unsigned playlistCount(){return ready?listCount:0;}
const Track &track(unsigned i){return ready&&i<songs?tracks[i]:emptyTrack;}
const Playlist &playlist(unsigned i){return ready&&i<listCount?lists[i]:emptyList;}
String lyric(int relative){
 if(!lock)return {};take();int index=-1;unsigned now=position/SamplesPerMs;
 for(unsigned i=0;i<lineCount;++i){if(lines[i].ms<=now)index=i;else break;}
 index+=relative;
 String result=index>=0&&index<(int)lineCount?lines[index].text:String();give();return result;
}
void diagnostics(JsonObject out){auto s=state();out["ready"]=s.ready;out["playing"]=s.playing;out["paused"]=s.paused;out["loading"]=s.loading;out["position_ms"]=s.position;out["duration_ms"]=s.duration;out["volume"]=s.volume;out["track"]=s.track;out["title"]=s.track>=0?track(s.track).title:String();out["playlists"]=playlistCount();out["songs"]=trackCount();out["buffered"]=s.buffered;out["underruns"]=s.underruns;out["error"]=s.error;out["track_id"]=s.track>=0?track(s.track).id:String();out["playlist"]=s.playlist;out["playlist_name"]=playlist(s.playlist).name;out["mode"]=s.mode;out["buffering"]=s.buffering;out["remembered"]=s.remembered;out["memory_save_failed"]=s.saveFailed;out["memory_pending"]=memoryRevision!=savedRevision;out["lyric_index"]=s.lyricIndex;out["has_lyrics"]=s.lyrics;}
}
#else
// Deterministic data for native LVGL layout/interaction verification.
extern bool apSuspended;
namespace MusicPlayer {
namespace {State s;Track t[134]={{"1","晴天","周杰伦"},{"2","晚星","逆时针向"},{"3","You're Not Alone (Original Mix)","CMA"}};Playlist p[2];MusicPlayback::History history;}
void begin(){s.ready=true;s.volume=40;s.track=0;s.playing=true;s.lyrics=true;s.lyricIndex=2;s.duration=269000;s.position=86000;p[0].name="酷狗我喜欢";p[1].name="依依歌单";for(unsigned i=3;i<134;++i)t[i]={String(i+1),String("歌曲 ")+String(i+1),"歌手"};p[0].count=134;p[1].count=3;for(auto&l:p)for(unsigned i=0;i<l.count;++i)l.tracks[i]=i;history.remember(0);}
void open(){} void stop(){s.playing=false;s.paused=s.track>=0;}
void play(unsigned i){s.track=i;s.playing=true;s.paused=false;s.remembered=false;s.position=0;history.remember(i);}
void selectPlaylist(unsigned i){s.playlist=i;history.clear();}
void next(int d){if(s.mode==2)play(history.step(d,p[s.playlist].tracks,p[s.playlist].count,s.track,1));else {auto&list=p[s.playlist];play(list.tracks[(s.track+list.count+(d>=0?1:-1))%list.count]);}}
void toggle(){if(!s.playing){s.playing=true;s.paused=false;s.remembered=false;}else s.paused=!s.paused;}
void seek(unsigned ms){s.position=ms;}
void setVolume(unsigned v){s.volume=std::min(v,100U);}
void setMode(unsigned v){if(v<3){s.mode=v;history.clear();if(s.track>=0)history.remember(s.track);}}
void cycleMode(){setMode((s.mode+1)%3);}
State state(){return s;}unsigned trackCount(){return 134;}unsigned playlistCount(){return 2;}
const Track&track(unsigned i){return t[i%134];}const Playlist&playlist(unsigned i){return p[i%2];}
String lyric(int n){const char*text[]={"在有风的午后","和喜欢的旋律相遇","让每一天都慢慢发光","把心事轻轻唱给你听"};if(s.track==2&&n==0)return "And every time the music plays, I remember all the little moments we shared together, wherever the winding road may lead us tomorrow.";return n>=-2&&n<=1?String(text[n+2]):String();}
void diagnostics(JsonObject){}
}
#endif
