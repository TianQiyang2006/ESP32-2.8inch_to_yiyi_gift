#include "radio_catalog.h"
#include "radio_station_data.h"
#include "radio_vendor/radio_stream_core.h"
#include "gift_assets/gift_glyph_coverage.h"
#include <LittleFS.h>
#include <algorithm>
#include <ctype.h>
#include <limits.h>
#include <memory>
#include <new>
#include <stdlib.h>
#if defined(GIFT_HOST_TEST) || defined(GIFT_LOGIC_TEST)
#include <mutex>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#endif

namespace RadioCatalog {
namespace {
constexpr const char *Main="/radio.json",*Backup="/radio.bak",*Temporary="/radio.tmp";
// JSON escaping can almost double a 512-byte URL; leave room for all 20
// maximum-length entries rather than limiting the catalog by serialized size.
constexpr unsigned MaxFile=32768;
// Fixed records and transaction copies live in PSRAM. String snapshots only
// allocate the single entry needed by the decoder/UI, not the whole catalog.
struct Record { char id[32]{},name[MaxName*4+1]{},tag[MaxTag*4+1]{},url[MaxUrl+1]{}; };
struct Catalog { uint32_t revision=1,nextId=1,count=0;Record entries[MaxStations]{}; };
void clearCatalog(Catalog &value){memset(&value,0,sizeof(value));value.revision=value.nextId=1;}
struct PsAllocator:ArduinoJson::Allocator {
 void *allocate(size_t n)override{
#if defined(GIFT_HOST_TEST) || defined(GIFT_LOGIC_TEST)
  return malloc(n);
#else
  return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
 }
 void deallocate(void *p)override{free(p);}
 void *reallocate(void *p,size_t n)override{
#if defined(GIFT_HOST_TEST) || defined(GIFT_LOGIC_TEST)
  return realloc(p,n);
#else
  return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#endif
 }
} allocator;
struct DeleteCatalog {void operator()(Catalog *p)const{if(p){p->~Catalog();free(p);}}};
using CatalogPtr=std::unique_ptr<Catalog,DeleteCatalog>;
CatalogPtr makeCatalog(){
 void *memory=allocator.allocate(sizeof(Catalog));
 return CatalogPtr(memory?new(memory) Catalog:nullptr);
}
CatalogPtr current;
bool loaded=false,available=false;
String error_;
#if defined(GIFT_HOST_TEST) || defined(GIFT_LOGIC_TEST)
std::mutex mutex;
struct Lock {std::lock_guard<std::mutex> lock;Lock():lock(mutex){}bool valid()const{return true;}};
#else
SemaphoreHandle_t mutex(){static SemaphoreHandle_t value=xSemaphoreCreateMutex();return value;}
struct Lock {SemaphoreHandle_t value;bool held;Lock():value(mutex()),held(value&&xSemaphoreTake(value,portMAX_DELAY)==pdTRUE){}~Lock(){if(held)xSemaphoreGive(value);}bool valid()const{return held;}};
#endif

bool validId(const char *text){
 if(!text||!*text||strlen(text)>=sizeof(Record::id))return false;
 for(;*text;++text)if(!((*text>='a'&&*text<='z')||(*text>='A'&&*text<='Z')||(*text>='0'&&*text<='9'))&&*text!='_'&&*text!='-')return false;
 return true;
}
bool textValue(JsonVariantConst value,unsigned max,String &out,bool empty,bool glyphs=true){
 if(!value.is<const char*>())return false;
 auto json=value.as<JsonString>();const char *p=json.c_str();
 if(json.size()!=strlen(p))return false;
 unsigned count=0;const unsigned char *cursor=(const unsigned char*)p;
 while(*cursor){
  uint32_t cp=*cursor++;unsigned extra=0;
  if(cp<0x80){if(cp<32||cp==127)return false;}
  else if(cp>=0xC2&&cp<=0xDF){extra=1;cp&=31;}
  else if(cp>=0xE0&&cp<=0xEF){extra=2;cp&=15;}
  else if(cp>=0xF0&&cp<=0xF4){extra=3;cp&=7;}
  else return false;
  for(unsigned i=0;i<extra;++i){if((*cursor&0xC0)!=0x80)return false;cp=(cp<<6)|(*cursor++&63);}
  if((extra==1&&cp<0x80)||(extra==2&&cp<0x800)||(extra==3&&cp<0x10000)||
     cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF)||cp==0xFEFF||(cp>=0x80&&cp<=0x9F))return false;
  if(glyphs&&!GiftCoverage::has(cp))return false;
  ++count;
 }
 out=p;out.trim();if(!empty&&out.isEmpty())return false;
 // Count the normalized value; ordinary surrounding spaces are convenient
 // on phones, while controls have already been rejected in the original.
 count=0;for(const unsigned char *c=(const unsigned char*)out.c_str();*c;++c)if((*c&0xC0)!=0x80)++count;
 return count<=max;
}
bool canonicalUrl(JsonVariantConst value,String &out){
 if(!textValue(value,MaxUrl,out,false,false)||out.length()>MaxUrl)return false;
 for(unsigned i=0;i<out.length();++i){uint8_t c=out[i];if(c<=32||c>=127||c=='\\'||c=='#')return false;}
 int scheme=out.indexOf("://");if(scheme<0)return false;
 String protocol=out.substring(0,scheme);protocol.toLowerCase();
 if(protocol!="http"&&protocol!="https")return false;
 unsigned start=unsigned(scheme)+3,end=start;
 while(end<out.length()&&out[end]!='/'&&out[end]!='?')++end;
 if(end==start)return false;
 String host=out.substring(start,end);host.toLowerCase();
 if(host.indexOf('@')>=0)return false;
 unsigned portAt=host.length();
 if(host[0]=='['){int close=host.indexOf(']');if(close<2)return false;for(int i=1;i<close;++i)if(!isxdigit(uint8_t(host[i]))&&host[i]!=':'&&host[i]!='.')return false;if(unsigned(close+1)<host.length()){if(host[close+1]!=':')return false;portAt=unsigned(close+1);}}
 else{int colon=host.indexOf(':');if(colon>=0)portAt=unsigned(colon);if(!portAt)return false;for(unsigned i=0;i<portAt;++i)if(!isalnum(uint8_t(host[i]))&&host[i]!='.'&&host[i]!='-')return false;}
 if(portAt<host.length()){unsigned port=0;if(portAt+1==host.length())return false;for(unsigned i=portAt+1;i<host.length();++i){if(host[i]<'0'||host[i]>'9')return false;port=port*10+unsigned(host[i]-'0');if(port>65535)return false;}if(!port)return false;}
 String tail=out.substring(end);if(tail.isEmpty()||tail[0]=='?')tail=String("/")+tail;
 String path=tail;int query=path.indexOf('?');if(query>=0)path=path.substring(0,query);path.toLowerCase();
 if(path.endsWith(".m3u8")||path.endsWith(".m3u")||path.endsWith(".pls")||path.endsWith(".mpd"))return false;
 out=protocol+"://"+host+tail;
 return out.length()<=MaxUrl;
}
bool record(JsonVariantConst value,Record &out,bool checkGlyphs){
 if(!value.is<JsonObjectConst>()||value.size()!=4||!value["id"].is<const char*>())return false;
 auto id=value["id"].as<JsonString>();if(id.size()!=strlen(id.c_str())||!validId(id.c_str()))return false;
 String name,tag,url;
 if(!textValue(value["name"],MaxName,name,false,checkGlyphs)||!textValue(value["tag"],MaxTag,tag,true,checkGlyphs)||!canonicalUrl(value["url"],url))return false;
 strcpy(out.id,id.c_str());strcpy(out.name,name.c_str());strcpy(out.tag,tag.c_str());strcpy(out.url,url.c_str());return true;
}
void item(JsonObject out,const Record &entry){out["id"]=static_cast<const char*>(entry.id);out["name"]=static_cast<const char*>(entry.name);out["tag"]=static_cast<const char*>(entry.tag);out["url"]=static_cast<const char*>(entry.url);}
uint32_t checksum(const Catalog &catalog){return RadioCore::crc32(&catalog,sizeof(catalog));}
void document(JsonDocument &out,const Catalog &catalog){
 out["version"]=1;out["revision"]=catalog.revision;out["nextId"]=catalog.nextId;
 auto items=out["items"].to<JsonArray>();for(unsigned i=0;i<catalog.count;++i)item(items.add<JsonObject>(),catalog.entries[i]);
 out["crc"]=checksum(catalog);
}
bool readFile(const char *path,Catalog &out){
 File file=LittleFS.open(path,"r");if(!file||!file.size()||file.size()>MaxFile)return false;
 JsonDocument json(&allocator);auto parsed=deserializeJson(json,file);file.close();
 if(parsed||json.size()!=5||!json["version"].is<unsigned>()||json["version"].as<unsigned>()!=1||!json["revision"].is<uint32_t>()||!json["nextId"].is<uint32_t>()||!json["crc"].is<uint32_t>()||!json["items"].is<JsonArrayConst>())return false;
 if(!json["revision"].as<uint32_t>()||!json["nextId"].as<uint32_t>()||json["items"].size()>MaxStations)return false;
 clearCatalog(out);out.revision=json["revision"];out.nextId=json["nextId"];
 for(JsonVariantConst value:json["items"].as<JsonArrayConst>()){
  auto &next=out.entries[out.count];if(!record(value,next,false))return false;
  for(unsigned i=0;i<out.count;++i)if(!strcmp(out.entries[i].id,next.id)||!strcmp(out.entries[i].url,next.url))return false;
  ++out.count;
 }
 return checksum(out)==json["crc"].as<uint32_t>();
}
bool persist(const Catalog &next,String &error){
 if(!available){error="内部存储不可用，电台尚未保存";return false;}
 JsonDocument json(&allocator);document(json,next);
 File file=LittleFS.open(Temporary,"w");
 if(json.overflowed()||!file||serializeJson(json,file)!=measureJson(json)){if(file)file.close();LittleFS.remove(Temporary);error="电台保存失败，原列表保留";return false;}
 file.flush();file.close();
 auto verified=makeCatalog();if(!verified||!readFile(Temporary,*verified)||checksum(*verified)!=checksum(next)){LittleFS.remove(Temporary);error="电台写入校验失败，原列表保留";return false;}
 bool existing=LittleFS.exists(Main),validMain=existing&&readFile(Main,*verified);
 if(existing){
  bool ready=validMain?((!LittleFS.exists(Backup)||LittleFS.remove(Backup))&&LittleFS.rename(Main,Backup)):LittleFS.remove(Main);
  if(!ready){LittleFS.remove(Temporary);error="无法备份电台列表，原列表保留";return false;}
 }
 if(!LittleFS.rename(Temporary,Main)){
  if(validMain)LittleFS.rename(Backup,Main);
  LittleFS.remove(Temporary);error="无法提交电台列表，恢复副本已保留";return false;
 }
 return true;
}
void defaults(Catalog &out){
 clearCatalog(out);
 for(const auto &entry:RadioPlayer::RadioStations){auto &target=out.entries[out.count++];strcpy(target.id,entry.id);strcpy(target.name,entry.name);strcpy(target.tag,entry.tag);strcpy(target.url,entry.url);}
}
bool load(){
 if(loaded)return current!=nullptr;
 current=makeCatalog();if(!current){error_="电台列表内存不足";return false;}
 available=LittleFS.begin(false,"/littlefs",10,"spiffs");
 if(available&&readFile(Main,*current)){error_="";loaded=true;return true;}
 if(available&&readFile(Backup,*current)){error_="主电台列表损坏，已读取恢复副本";loaded=true;return true;}
 bool existed=LittleFS.exists(Main)||LittleFS.exists(Backup);
 defaults(*current);loaded=true;
 error_=!available?"内部存储不可用，电台尚未保存":existed?"电台列表损坏，暂时使用默认电台":"";
 return true;
}
}

bool begin(){Lock lock;return lock.valid()&&load();}
unsigned count(){Lock lock;return lock.valid()&&load()?current->count:0;}
uint32_t revision(){Lock lock;return lock.valid()&&load()?current->revision:0;}
Entry get(unsigned index){
 Lock lock;if(!lock.valid()||!load()||index>=current->count)return {};
 const auto &entry=current->entries[index];return {entry.id,entry.name,entry.tag,entry.url};
}
String storageError(){Lock lock;if(!lock.valid())return "电台列表锁初始化失败";load();return error_;}
void snapshot(JsonObject out){
 Lock lock;if(!lock.valid()){out["limit"]=MaxStations;out["revision"]=0;out["error"]="电台列表锁初始化失败";out["stations"].to<JsonArray>();return;}
 load();out["limit"]=MaxStations;out["revision"]=current?current->revision:0;out["error"]=error_;
 auto entries=out["stations"].to<JsonArray>();if(current)for(unsigned i=0;i<current->count;++i)item(entries.add<JsonObject>(),current->entries[i]);
}
bool mutate(JsonVariantConst request,String &error,unsigned *httpStatus){
 if(httpStatus)*httpStatus=400;
 Lock lock;if(!lock.valid()){if(httpStatus)*httpStatus=500;error="电台列表锁初始化失败";return false;}if(!load()){if(httpStatus)*httpStatus=500;error=error_;return false;}
 if(!request.is<JsonObjectConst>()||request.size()!=3||!request["action"].is<const char*>()||!request["revision"].is<uint32_t>()){
  error="电台操作格式错误";return false;
 }
 String action=request["action"].as<String>();
 if(request["action"].as<JsonString>().size()!=strlen(action.c_str())){error="电台操作格式错误";return false;}
 if(action!="upsert"&&action!="remove"){error="不支持的电台操作";return false;}
 if(request["revision"].as<uint32_t>()!=current->revision){if(httpStatus)*httpStatus=409;error="电台列表已更新，请刷新后重试";return false;}
 if(current->revision==UINT32_MAX){if(httpStatus)*httpStatus=500;error="电台列表版本已用完";return false;}
 auto next=makeCatalog();if(!next){if(httpStatus)*httpStatus=500;error="电台列表内存不足";return false;}*next=*current;
 JsonVariantConst incoming=action=="upsert"?request["station"]:request["id"];
 JsonVariantConst idValue=action=="upsert"?incoming["id"]:incoming;
 if(!idValue.is<const char*>()||idValue.as<JsonString>().size()!=strlen(idValue.as<const char*>())){error="电台编号无效";return false;}
 String id=idValue.as<String>();int found=-1;for(unsigned i=0;i<next->count;++i)if(id==next->entries[i].id)found=int(i);
 if(!id.isEmpty()&&found<0){if(httpStatus)*httpStatus=409;error="这条电台已不存在，请刷新后重试";return false;}
 if(action=="remove"){
  if(found<0){error="请选择要删除的电台";return false;}
  for(unsigned i=unsigned(found);i+1<next->count;++i)next->entries[i]=next->entries[i+1];
  next->entries[--next->count]=Record{};
 }else{
  if(!incoming.is<JsonObjectConst>()||incoming.size()!=4){error="请完整填写电台名称、分类和直播地址";return false;}
  if(found<0&&next->count>=MaxStations){error="最多保存20个电台，请先删除一个";return false;}
  if(found<0){if(next->nextId==UINT32_MAX){error="电台编号已用完";return false;}id=String("custom_")+String(next->nextId++);}
  JsonDocument canonical(&allocator);canonical.set(incoming);canonical["id"]=id;
  Record value;
  if(canonical.overflowed()||!record(canonical.as<JsonVariantConst>(),value,true)){
   error="请检查名称和分类（各最多24字、需设备支持），并填写512字节内的HTTP/HTTPS直播直链；不支持播放列表";return false;
  }
  for(unsigned i=0;i<next->count;++i)if(int(i)!=found&&!strcmp(next->entries[i].url,value.url)){error="这个直播地址已经添加过了";return false;}
  if(found>=0)next->entries[found]=value;else next->entries[next->count++]=value;
 }
 ++next->revision;
 if(!persist(*next,error)){if(httpStatus)*httpStatus=500;error_=error;return false;}
 current=std::move(next);error_="";error="";if(httpStatus)*httpStatus=200;return true;
}
}
