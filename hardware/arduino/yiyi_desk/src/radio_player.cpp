#include "radio_player.h"
#include "radio_station_data.h"
#include "radio_vendor/radio_stream_core.h"
#include <algorithm>

namespace RadioPlayer {
unsigned stationCount(){RadioCatalog::begin();return RadioCatalog::count();}
Station station(unsigned i){RadioCatalog::begin();return RadioCatalog::get(i);}
uint32_t stationRevision(){RadioCatalog::begin();return RadioCatalog::revision();}
namespace {
int stationIndex(const String &id){
 if(id.isEmpty())return -1;
 for(unsigned i=0;i<stationCount();++i)if(station(i).id==id)return int(i);
 return -1;
}
bool existsFavorite(uint32_t id){
 for(unsigned i=0;i<stationCount();++i)if(RadioCore::idHash(station(i).id.c_str())==id)return true;
 return false;
}
struct CatalogChange {Station entry;int index=-1;bool removed=false,urlChanged=false;};
CatalogChange reconcileCatalog(const Station &previous){
 CatalogChange change;change.index=stationIndex(previous.id);change.removed=change.index<0;
 if(change.removed&&stationCount())change.index=0;
 if(change.index>=0)change.entry=station(unsigned(change.index));
 change.urlChanged=!change.removed&&change.entry.url!=previous.url;return change;
}
void pruneFavorites(uint32_t *ids,unsigned &count){
 unsigned kept=0;for(unsigned i=0;i<count;++i)if(existsFavorite(ids[i]))ids[kept++]=ids[i];count=kept;
}
}
}

#ifndef GIFT_HOST_TEST
#include "audio_board.h"
#include "music_player.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <new>
#include "radio_vendor/libhelix-mp3/mp3dec.h"
#undef Word64
#include "radio_vendor/libhelix-aac/aacdec.h"

namespace RadioPlayer {
namespace {
constexpr unsigned CompressedCapacity=65536,InitialBuffer=49152,ResumeBuffer=16384,ReceiveTimeout=15000,WorkerStack=10240;
constexpr unsigned MaxFavorites=32;
SemaphoreHandle_t stateLock=nullptr,saveLock=nullptr;
TaskHandle_t workerHandle=nullptr;
std::atomic<bool> initialized{false},wanted{false},audible{false},connecting{false},buffering{false},saveFailed{false},outputBusy{false};
std::atomic<unsigned> generation{0},volume{35},buffered{0},retries{0},revision{0},savedRevision{0},saveAt{0};
std::atomic<int> selected{0};
std::atomic<uint32_t> networkBytes{0},decodedFrames{0},pcmSamples{0},decodeErrors{0},sourceRate{0};
uint32_t favoriteIds[MaxFavorites]{};unsigned favoriteCount=0;
Station selectedStation;
String currentTitle,currentError,currentCodec;
struct Memory {uint32_t magic=0x314d4659,version=1,percent=35,recent=0,favoriteCount=0,ids[MaxFavorites]{},crc=0;};
void take(){xSemaphoreTake(stateLock,portMAX_DELAY);}
void give(){xSemaphoreGive(stateLock);}
void setError(const String &s){take();currentError=s;give();}
bool active(unsigned epoch){return wanted.load()&&generation.load()==epoch;}
void changed(){saveAt=millis();++revision;}
void persist(bool force=false){
 if(!initialized||revision.load()==savedRevision.load()||(!force&&millis()-saveAt.load()<750))return;
 if(!saveLock||xSemaphoreTake(saveLock,0)!=pdTRUE)return;
 unsigned before=revision.load();Memory m;m.percent=volume.load();
 take();m.recent=RadioCore::idHash(selectedStation.id.c_str());
 m.favoriteCount=favoriteCount;for(unsigned i=0;i<favoriteCount;++i)m.ids[i]=favoriteIds[i];give();
 m.crc=RadioCore::crc32(&m,offsetof(Memory,crc));
 Preferences prefs;bool ok=prefs.begin("gift-radio",false);
 if(ok){ok=prefs.putBytes("settings",&m,sizeof(m))==sizeof(m);prefs.end();}
 saveFailed=!ok;if(ok)savedRevision=before;else saveAt=millis();xSemaphoreGive(saveLock);
}
void loadMemory(){
 Memory m;Preferences prefs;if(!prefs.begin("gift-radio",true))return;
 bool valid=prefs.getBytesLength("settings")==sizeof(m)&&prefs.getBytes("settings",&m,sizeof(m))==sizeof(m);prefs.end();
 if(!valid||m.magic!=0x314d4659||m.version!=1||m.percent>100||m.favoriteCount>MaxFavorites||m.crc!=RadioCore::crc32(&m,offsetof(Memory,crc)))return;
 volume=m.percent;favoriteCount=0;
 for(unsigned i=0;i<stationCount();++i){
  uint32_t id=RadioCore::idHash(station(i).id.c_str());if(id==m.recent)selected=int(i);
  for(unsigned j=0;j<m.favoriteCount;++j)if(id==m.ids[j]&&favoriteCount<MaxFavorites){favoriteIds[favoriteCount++]=id;break;}
 }
}
String metadataTitle(const char *text){
 const char *p=strstr(text,"StreamTitle=");if(!p)return {};
 p+=12;char quote=(*p=='\''||*p=='"')?*p++:0;String value;value.reserve(192);
 while(*p&&value.length()<240){if((quote&&*p==quote)||(!quote&&*p==';'))break;if(uint8_t(*p)>=32)value+=*p;++p;}
 value.trim();return value;
}

// HTTPClient owns redirects and TLS connection setup. This bounded reader owns
// chunked transfer framing and then ICY metadata framing, in that order.
struct LiveInput {
 HTTPClient &http;WiFiClient *socket;unsigned epoch;
 const std::atomic<bool> &cancelled;
 RadioCore::Chunked chunks;RadioCore::Icy icy;
 bool chunked=false,ended=false,malformed=false;
 uint32_t lastData=0;
 explicit LiveInput(HTTPClient &h,unsigned g,uint32_t meta,bool chunk,const std::atomic<bool> &cancel):http(h),socket(h.getStreamPtr()),epoch(g),cancelled(cancel),icy(meta),chunked(chunk),lastData(millis()){}
 unsigned readAvailable(uint8_t *dest,unsigned space){
  unsigned written=0,received=0;uint8_t bytes[768];uint32_t started=micros();
  while(space&&received<2048&&active(epoch)&&!cancelled.load()&&socket){
   int count=socket->available();if(count<=0)break;
   count=std::min(count,int(std::min(2048-received,std::min(space,unsigned(sizeof(bytes))))));
   int read=socket->read(bytes,count);if(read<=0)break;lastData=millis();networkBytes+=read;
   received+=unsigned(read);
   for(int i=0;i<read;++i){
    if(chunked){auto result=chunks.put(bytes[i]);if(result==RadioCore::Chunked::Error){malformed=true;ended=true;return written;}if(result==RadioCore::Chunked::End){ended=true;return written;}if(result!=RadioCore::Chunked::Data)continue;}
    bool updated=false;
    if(icy.put(bytes[i],updated)){*dest++=bytes[i];--space;++written;}
    if(updated){String title=metadataTitle(icy.title());if(!title.isEmpty()){take();if(active(epoch))currentTitle=title;give();}}
   }
   // Finish processing every byte already removed from the socket before
   // yielding; chunk/ICY parser state is carried into the next bounded read.
   if(micros()-started>=2000)break;
  }
  if(!socket||(socket->available()==0&&!socket->connected()))ended=true;
  return written;
 }
};

// One task owns the socket and protocol parsers. The decoder only consumes
// published audio bytes, so I2S writes cannot stop the TCP receive window from
// being drained. Release/acquire indices protect the PSRAM SPSC ring.
struct NetworkFeed {
 static constexpr unsigned Capacity=65536,Stack=4096;
 std::atomic<bool> cancelled{false},ended{false},malformed{false};
 std::atomic<uint32_t> produced{0},consumed{0},lastData{0};
 LiveInput input;
 uint8_t *ring=nullptr;
 SemaphoreHandle_t finished=nullptr;
 TaskHandle_t task=nullptr;
 explicit NetworkFeed(HTTPClient &h,unsigned epoch,uint32_t meta,bool chunk):input(h,epoch,meta,chunk,cancelled){lastData=input.lastData;}
 NetworkFeed(const NetworkFeed&)=delete;
 NetworkFeed &operator=(const NetworkFeed&)=delete;
 ~NetworkFeed(){stop();}
 bool begin(){
  ring=static_cast<uint8_t*>(heap_caps_malloc(Capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!ring)return false;
  finished=xSemaphoreCreateBinary();
  if(!finished){free(ring);ring=nullptr;return false;}
  if(xTaskCreatePinnedToCore(run,"radio-network",Stack,this,2,&task,0)!=pdPASS){
   task=nullptr;vSemaphoreDelete(finished);finished=nullptr;free(ring);ring=nullptr;return false;
  }
  return true;
 }
 static void run(void *argument){
  auto &feed=*static_cast<NetworkFeed*>(argument);
  while(!feed.cancelled.load()&&active(feed.input.epoch)){
   uint32_t write=feed.produced.load(std::memory_order_relaxed);
   unsigned used=write-feed.consumed.load(std::memory_order_acquire);
   unsigned index=write%Capacity;
   unsigned space=std::min(Capacity-used,Capacity-index);
   unsigned count=space?feed.input.readAvailable(feed.ring+index,space):0;
   if(count)feed.produced.store(write+count,std::memory_order_release);
   feed.lastData.store(feed.input.lastData,std::memory_order_release);
   if(feed.input.ended){
    feed.malformed.store(feed.input.malformed,std::memory_order_relaxed);
    feed.ended.store(true,std::memory_order_release);break;
   }
   // Always give the decoder and IDLE task a turn, including a CDN burst or
   // full ring. The socket reader itself never waits for future audio bytes.
   vTaskDelay(pdMS_TO_TICKS(count?1:3));
  }
  // Nothing touches feed after this signal. The session joins before freeing
  // its context, semaphore, ring, HTTPClient or TLS socket.
  xSemaphoreGive(feed.finished);
  vTaskDelete(nullptr);
 }
 unsigned available()const{
  return produced.load(std::memory_order_acquire)-consumed.load(std::memory_order_relaxed);
 }
 unsigned readAvailable(uint8_t *dest,unsigned space){
  uint32_t read=consumed.load(std::memory_order_relaxed);
  unsigned count=std::min(space,produced.load(std::memory_order_acquire)-read);
  unsigned index=read%Capacity,first=std::min(count,Capacity-index);
  if(first)memcpy(dest,ring+index,first);
  if(count>first)memcpy(dest+first,ring,count-first);
  if(count)consumed.store(read+count,std::memory_order_release);
  return count;
 }
 bool drained()const{return ended.load(std::memory_order_acquire)&&available()==0;}
 void stop(){
  cancelled=true;
  if(task){xSemaphoreTake(finished,portMAX_DELAY);task=nullptr;}
  if(finished){vSemaphoreDelete(finished);finished=nullptr;}
  free(ring);ring=nullptr;
 }
};

struct Decoder {
 HMP3Decoder mp3=nullptr;HAACDecoder aac=nullptr;bool isAac=false;
 ~Decoder(){if(mp3)MP3FreeDecoder(mp3);if(aac)AACFreeDecoder(aac);}
 bool begin(bool adts){isAac=adts;if(isAac)aac=AACInitDecoder();else mp3=MP3InitDecoder();return isAac?aac!=nullptr:mp3!=nullptr;}
 int sync(uint8_t *data,unsigned length){return isAac?AACFindSyncWord(data,int(length)):MP3FindSyncWord(data,int(length));}
 int decode(uint8_t *&data,int &left,int16_t *pcm,unsigned &rate,unsigned &channels,unsigned &samples){
  int result=isAac?AACDecode(aac,&data,&left,pcm):MP3Decode(mp3,&data,&left,pcm,0);
  if(result)return result;
  if(isAac){AACFrameInfo info{};AACGetLastFrameInfo(aac,&info);rate=info.sampRateOut;channels=info.nChans;samples=info.outputSamps;}
  else{MP3FrameInfo info{};MP3GetLastFrameInfo(mp3,&info);rate=info.samprate;channels=info.nChans;samples=info.outputSamps;}
  return 0;
 }
};

struct PcmOutput {
 unsigned epoch,count=0;int16_t block[256]{};bool fault=false;
 RadioCore::Resampler resampler;
 explicit PcmOutput(unsigned g):epoch(g){}
 bool flush(){
  if(!count)return true;
  if(!active(epoch)||AudioBoard::playing())return false;
  outputBusy=true;
  bool ok=active(epoch)&&!AudioBoard::playing()&&AudioBoard::streamRadioPcm(block,count,volume.load(),32000);
  outputBusy=false;
  if(!ok){if(active(epoch)&&!AudioBoard::playing()&&!AudioBoard::error().isEmpty())fault=true;return false;}
  take();bool current=active(epoch);
  if(current){pcmSamples+=count;count=0;audible=true;connecting=false;buffering=false;}
  give();return current;
 }
 bool sample(int16_t value){block[count++]=value;return count<256||flush();}
 bool write(const int16_t *pcm,unsigned samples,unsigned channels,unsigned rate){
  if((channels!=1&&channels!=2)||samples>4096||samples%channels||!resampler.setRate(rate)){fault=true;return false;}
  for(unsigned i=0;i<samples;i+=channels){
   if((i&127)==0&&(!active(epoch)||AudioBoard::playing()))return false;
   int16_t mono=channels==1?pcm[i]:int16_t((int32_t(pcm[i])+pcm[i+1])/2);
   if(!resampler.put(mono,[this](int16_t x){return sample(x);}))return false;
  }
  return true;
 }
};

enum class SessionResult { Cancelled, Lost, Preempted, Unsupported, Failed };
SessionResult session(const Station &entry,unsigned epoch){
 WiFiClient plain;WiFiClientSecure tls;HTTPClient http;
 String url=entry.url;
 if(url.indexOf(".m3u8")>=0){setError("此电台格式暂不支持，请换台");return SessionResult::Unsupported;}
 WiFiClient *client=&plain;
 if(url.startsWith("https://")){tls.setInsecure();tls.setHandshakeTimeout(4);client=&tls;}
 // Use the HTTP/1.1 live-stream path validated on this board; LiveInput
 // decodes its chunked transfer framing before ICY metadata.
 http.setConnectTimeout(3500);http.setTimeout(1500);http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);http.setRedirectLimit(5);http.useHTTP10(false);http.setReuse(false);
 if(!http.begin(*client,url)){setError("电台地址无效，请换台");return SessionResult::Failed;}
 const char *headers[]={"Content-Type","icy-metaint","icy-name","Transfer-Encoding"};http.collectHeaders(headers,4);
 http.setUserAgent("YiyiRadio/1.0");http.addHeader("Icy-MetaData","0");
 int status=http.GET();
 if(!active(epoch)){http.end();return SessionResult::Cancelled;}
 if(status!=200){setError(status>0?String("电台连接失败（")+status+"）":String("暂时连接不上，正在重试"));http.end();return SessionResult::Lost;}
 String type=http.header("Content-Type");type.toLowerCase();
 if(type.indexOf("mpegurl")>=0||type.indexOf("html")>=0||type.indexOf("json")>=0||type.indexOf("video/")>=0){setError("电台未返回可播放的音频");http.end();return SessionResult::Unsupported;}
 String encoding=http.header("Transfer-Encoding");encoding.toLowerCase();
 uint32_t metaint=uint32_t(http.header("icy-metaint").toInt());if(metaint>16*1024*1024)metaint=0;
 uint8_t *compressed=static_cast<uint8_t*>(heap_caps_malloc(CompressedCapacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 int16_t *pcm=static_cast<int16_t*>(heap_caps_malloc(4096*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!compressed||!pcm){free(compressed);free(pcm);http.end();setError("播放内存不足，请稍后重试");return SessionResult::Failed;}
 NetworkFeed input(http,epoch,metaint,encoding.indexOf("chunked")>=0);
 if(!input.begin()){free(compressed);free(pcm);http.end();setError("网络缓冲或接收任务启动失败");return SessionResult::Failed;}
 unsigned head=0,end=0;bool detected=false,primed=false;
 Decoder decoder;PcmOutput output(epoch);SessionResult result=SessionResult::Lost;uint32_t lastDecoded=millis();
 buffering=true;
 while(active(epoch)){
  if(AudioBoard::playing()){result=SessionResult::Preempted;break;}
  if(WiFi.status()!=WL_CONNECTED){setError("网络已断开，等待重新连接");break;}
  if(CompressedCapacity-end<4096&&head){memmove(compressed,compressed+head,end-head);end-=head;head=0;}
  end+=input.readAvailable(compressed+end,CompressedCapacity-end);buffered=end-head+input.available();
  unsigned available=end-head;
  bool inputEnded=input.drained();
  if(!primed){
   unsigned target=audible.load()?ResumeBuffer:InitialBuffer;
   if(available<target&&!inputEnded){if(millis()-input.lastData.load()>ReceiveTimeout){setError("电台响应超时，正在重连");break;}vTaskDelay(pdMS_TO_TICKS(5));continue;}
   primed=true;lastDecoded=millis();
  }
  if(available<2048&&!inputEnded){primed=false;buffering=true;if(millis()-input.lastData.load()>ReceiveTimeout){setError("直播连接中断，正在重连");break;}vTaskDelay(pdMS_TO_TICKS(5));continue;}
  if(available<8&&inputEnded){setError(input.malformed.load()?"音频传输异常，正在重连":"直播连接中断，正在重连");break;}
  if(!detected){
   bool adts=type.indexOf("aac")>=0;int mp3=MP3FindSyncWord(compressed+head,int(available)),aac=AACFindSyncWord(compressed+head,int(available));
   if(!adts&&mp3<0&&aac>=0)adts=true;
   if(!decoder.begin(adts)){setError("音频解码内存不足");result=SessionResult::Failed;break;}
   detected=true;take();currentCodec=adts?"AAC":"MP3";give();
  }
  int sync=decoder.sync(compressed+head,available);
  if(sync<0){head=end?end-1:0;if(millis()-lastDecoded>12000){setError("此电台音频格式暂不支持");result=SessionResult::Unsupported;break;}continue;}
  head+=unsigned(sync);available=end-head;
  uint8_t *cursor=compressed+head;int left=int(available);unsigned rate=0,channels=0,samples=0;
  int code=decoder.decode(cursor,left,pcm,rate,channels,samples);
  unsigned consumed=unsigned(cursor-(compressed+head));
  if(code==-1){
   if(inputEnded){setError("音频数据不完整，正在重连");break;}
   // A partial AAC frame may exceed 2048 bytes. It must share the receive
   // watchdog instead of retrying the same incomplete bytes indefinitely.
   if(millis()-input.lastData.load()>ReceiveTimeout||millis()-lastDecoded>12000){setError("音频数据超时，正在重连");break;}
   if(available<8192){vTaskDelay(pdMS_TO_TICKS(3));continue;}
  }
  if(code){head+=consumed?consumed:1;++decodeErrors;if(millis()-lastDecoded>12000){setError("此电台音频暂时无法播放");break;}continue;}
  head+=consumed;sourceRate=rate;++decodedFrames;lastDecoded=millis();
  if(!output.write(pcm,samples,channels,rate)){
   if(!active(epoch))result=SessionResult::Cancelled;
   else if(AudioBoard::playing())result=SessionResult::Preempted;
   else if(output.fault){setError(AudioBoard::error().isEmpty()?"音频输出失败，请重试":AudioBoard::error());result=SessionResult::Failed;}
   break;
  }
  if(audible){take();if(active(epoch))currentError="";give();}
  // Yield after each decoded frame, including low sample-rate streams.
  vTaskDelay(1);
 }
 if(!active(epoch))result=SessionResult::Cancelled;
 input.stop();free(compressed);free(pcm);http.end();buffered=0;
 return result;
}

void worker(void*){
 for(;;){
  if(!wanted.load()){
   take();if(!wanted.load()){workerHandle=nullptr;give();vTaskDelete(nullptr);return;}give();
  }
  take();unsigned epoch=generation.load();Station entry=selectedStation;give();
  if(entry.id.isEmpty()){wanted=false;continue;}
  if(!active(epoch))continue;
  if(AudioBoard::playing()){connecting=false;buffering=false;vTaskDelay(pdMS_TO_TICKS(30));continue;}
  if(WiFi.status()!=WL_CONNECTED){connecting=true;buffering=false;setError("网络未连接，请先连接 Wi-Fi");vTaskDelay(pdMS_TO_TICKS(200));continue;}
  connecting=true;buffering=false;
  auto result=session(entry,epoch);
  if(!active(epoch))continue;
  audible=false;buffering=false;AudioBoard::endRadioStream();
  if(result==SessionResult::Preempted){connecting=false;continue;}
  if(result==SessionResult::Failed||result==SessionResult::Unsupported){wanted=false;connecting=false;continue;}
  unsigned attempts=++retries;
  unsigned delayMs=std::min(30000U,2000U<<(std::min(attempts,5U)-1));
  uint32_t until=millis()+delayMs;
  while(active(epoch)&&int32_t(millis()-until)<0)vTaskDelay(pdMS_TO_TICKS(20));
 }
}
}

void begin(){
 if(initialized)return;
 if(!stateLock)stateLock=xSemaphoreCreateMutex();if(!stateLock)return;
 if(!saveLock)saveLock=xSemaphoreCreateMutex();if(!saveLock)return;
 RadioCatalog::begin();selected=stationCount()?0:-1;loadMemory();selectedStation=selected>=0?station(unsigned(selected.load())):Station{};initialized=true;
}
void open(){begin();}
void stop(){
 if(!initialized)return;
 wanted=false;++generation;
 take();audible=false;connecting=false;buffering=false;buffered=0;retries=0;give();
 // Cancellation never waits on a DNS/TLS/socket operation. At most the current
 // <=256-sample I2S write must finish; AudioBoard's owner gate excludes new music.
 uint32_t start=millis();while(outputBusy.load()&&millis()-start<160)vTaskDelay(1);
 AudioBoard::endRadioStream();persist(true);
}
void play(unsigned i){
 begin();if(!initialized||i>=stationCount())return;
 if(!AudioBoard::lockMediaControl())return;
 struct UnlockControl{~UnlockControl(){AudioBoard::unlockMediaControl();}} unlockControl;
 MusicPlayer::stop();stop();
 if(!AudioBoard::ready()){setError(AudioBoard::error().isEmpty()?"音频设备尚未就绪":AudioBoard::error());return;}
 AudioBoard::selectStream(AudioBoard::StreamSource::Radio);
 take();selectedStation=station(i);selected=int(i);changed();++generation;wanted=true;connecting=true;retries=0;networkBytes=0;decodedFrames=0;decodeErrors=0;pcmSamples=0;sourceRate=0;
 currentError="";currentTitle="";currentCodec="";
 if(!workerHandle&&xTaskCreatePinnedToCore(worker,"radio-live",WorkerStack,nullptr,3,&workerHandle,0)!=pdPASS){workerHandle=nullptr;wanted=false;connecting=false;currentError="播放任务启动失败";}
 give();
}
void toggle(){if(wanted.load())stop();else if(stationCount())play(unsigned(std::max(0,selected.load())));else{begin();setError("还没有电台，请连接手机添加");}}
void next(int direction){unsigned n=stationCount();if(n)play(unsigned((std::max(0,selected.load())+int(n)+(direction>=0?1:-1))%int(n)));}
void setVolume(unsigned percent){begin();percent=std::min(percent,100U);if(volume.exchange(percent)!=percent)changed();}
void toggleFavorite(){
 begin();if(!initialized||selected.load()<0)return;take();uint32_t id=RadioCore::idHash(selectedStation.id.c_str());
 unsigned found=0;while(found<favoriteCount&&favoriteIds[found]!=id)++found;
 if(found<favoriteCount){for(unsigned j=found+1;j<favoriteCount;++j)favoriteIds[j-1]=favoriteIds[j];--favoriteCount;}
 else if(favoriteCount<MaxFavorites)favoriteIds[favoriteCount++]=id;
 give();changed();
}
bool isFavorite(unsigned i){
 if(!initialized||i>=stationCount())return false;uint32_t id=RadioCore::idHash(station(i).id.c_str());
 take();bool yes=false;for(unsigned j=0;j<favoriteCount;++j)if(favoriteIds[j]==id){yes=true;break;}give();return yes;
}
void catalogChanged(){
 if(!initialized)return;
 take();Station previous=selectedStation;give();
 auto change=reconcileCatalog(previous);bool requested=wanted.load();
 if(change.removed)stop();
 take();selected=change.index;selectedStation=change.entry;
 pruneFavorites(favoriteIds,favoriteCount);
 if(change.removed){currentTitle="";currentCodec="";currentError="";}
 give();changed();persist(true);
 if(change.urlChanged&&requested)play(unsigned(change.index));
}
State state(){
 State s;s.ready=initialized;bool requested=wanted.load();
 s.playing=requested&&audible.load();s.buffering=requested&&buffering.load();
 // The request remains active during reconnect backoff or a reminder. Keep
 // the UI's stop action consistent with toggle(), which cancels that request.
 s.connecting=requested&&(connecting.load()||(!s.playing&&!s.buffering));
 s.saveFailed=saveFailed;s.wifi=WiFi.status()==WL_CONNECTED;
 s.station=selected;s.volume=volume;s.buffered=buffered;s.retries=retries;s.favorite=s.station>=0&&isFavorite(unsigned(s.station));
 if(stateLock){take();s.title=currentTitle;s.error=currentError;give();}return s;
}
void tick(){persist();}
void diagnostics(JsonObject out){
 auto s=state();out["ready"]=s.ready;out["playing"]=s.playing;out["connecting"]=s.connecting;out["buffering"]=s.buffering;out["wifi"]=s.wifi;
 out["station"]=s.station;out["station_count"]=stationCount();out["station_id"]=s.station>=0?station(s.station).id:"";out["station_name"]=s.station>=0?station(s.station).name:"";
 out["title"]=s.title;out["error"]=s.error;out["volume"]=s.volume;out["favorite"]=s.favorite;out["save_failed"]=s.saveFailed;out["buffered"]=s.buffered;out["retries"]=s.retries;
 out["network_bytes"]=networkBytes.load();out["decoded_frames"]=decodedFrames.load();out["decode_errors"]=decodeErrors.load();out["pcm_samples"]=pcmSamples.load();out["source_rate"]=sourceRate.load();out["output_rate"]=32000;
 if(stateLock){take();out["codec"]=currentCodec;out["worker_active"]=workerHandle!=nullptr;if(workerHandle)out["stack_watermark"]=uxTaskGetStackHighWaterMark(workerHandle);give();}
 else{out["codec"]="";out["worker_active"]=false;}
}
}
#else
// Deterministic host-only state for the native LVGL integration harness. The
// production branch sets playing only after real PCM has reached AudioBoard.
namespace RadioPlayer {
namespace {State snapshot;Station selectedStation;uint32_t favorites[32]{};unsigned favoriteCount=0;}
void begin(){RadioCatalog::begin();snapshot.ready=true;snapshot.wifi=true;if(snapshot.station<0&&stationCount())snapshot.station=0;if(selectedStation.id.isEmpty()&&snapshot.station>=0)selectedStation=station(snapshot.station);}
void open(){begin();}
void stop(){snapshot.playing=false;snapshot.connecting=false;snapshot.buffering=false;}
void play(unsigned i){begin();if(i<stationCount()){selectedStation=station(i);snapshot.station=int(i);snapshot.playing=true;snapshot.connecting=false;snapshot.error="";snapshot.favorite=isFavorite(i);}}
void toggle(){if(snapshot.playing||snapshot.connecting)stop();else play(unsigned(std::max(0,snapshot.station)));}
void next(int direction){unsigned n=stationCount();if(n)play(unsigned((std::max(0,snapshot.station)+int(n)+(direction>=0?1:-1))%int(n)));}
void setVolume(unsigned value){snapshot.volume=std::min(100U,value);}
void toggleFavorite(){if(snapshot.station<0)return;uint32_t id=RadioCore::idHash(selectedStation.id.c_str());unsigned found=0;while(found<favoriteCount&&favorites[found]!=id)++found;if(found<favoriteCount){for(unsigned j=found+1;j<favoriteCount;++j)favorites[j-1]=favorites[j];--favoriteCount;}else if(favoriteCount<32)favorites[favoriteCount++]=id;snapshot.favorite=isFavorite(unsigned(snapshot.station));}
bool isFavorite(unsigned i){if(i>=stationCount())return false;uint32_t id=RadioCore::idHash(station(i).id.c_str());for(unsigned j=0;j<favoriteCount;++j)if(favorites[j]==id)return true;return false;}
void catalogChanged(){
 if(!snapshot.ready)return;
 auto change=reconcileCatalog(selectedStation);bool requested=snapshot.playing||snapshot.connecting;
 if(change.removed){stop();snapshot.title="";snapshot.error="";}
 selectedStation=change.entry;snapshot.station=change.index;pruneFavorites(favorites,favoriteCount);
 if(change.urlChanged&&requested)play(unsigned(change.index));
 snapshot.favorite=change.index>=0&&isFavorite(unsigned(change.index));
}
State state(){return snapshot;}
void diagnostics(JsonObject){}
void tick(){}
}
#endif
