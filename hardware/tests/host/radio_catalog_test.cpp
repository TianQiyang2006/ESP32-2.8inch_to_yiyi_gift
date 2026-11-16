#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/radio_catalog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <atomic>

static unsigned checks=0;
static void checkAt(bool value,unsigned line){++checks;if(!value)std::cerr<<"failed check "<<checks<<" at line "<<line<<"\n";assert(value);}
#define check(value) checkAt(bool(value),__LINE__)
static std::string file(const char *name="/radio.json"){
 std::ifstream stream(LittleFS.directory+name,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};
}
static JsonDocument station(const std::string &name="自选音乐",const std::string &url="https://radio.example/stream.mp3",const std::string &id=""){
 JsonDocument request;request["action"]="upsert";request["revision"]=RadioCatalog::revision();
 auto entry=request["station"].to<JsonObject>();entry["id"]=id;entry["name"]=name;entry["tag"]="音乐";entry["url"]=url;return request;
}
static bool mutate(JsonDocument &request,String *outError=nullptr,unsigned *outStatus=nullptr){
 String error;unsigned status=0;bool result=RadioCatalog::mutate(request.as<JsonVariantConst>(),error,&status);check(result?error.isEmpty():!error.isEmpty());check(result?status==200:(status==400||status==409||status==500));if(outError)*outError=error;if(outStatus)*outStatus=status;return result;
}
static bool remove(const String &id){JsonDocument request;request["action"]="remove";request["id"]=id;request["revision"]=RadioCatalog::revision();return mutate(request);}
static void unchanged(JsonDocument &request){auto before=file();auto count=RadioCatalog::count();auto revision=RadioCatalog::revision();check(!mutate(request));check(file()==before);check(RadioCatalog::count()==count);check(RadioCatalog::revision()==revision);}
static void defaults(){
 static const char *ids[]={"beijing_music","shanghai_pop","shanghai_classic","guangdong_music","cnr_voice","beijing_news"};
 check(RadioCatalog::count()==6);check(RadioCatalog::revision()==1);for(unsigned i=0;i<6;++i)check(RadioCatalog::get(i).id==ids[i]);
 check(RadioCatalog::get(6).id.isEmpty());check(RadioCatalog::get(999).url.isEmpty());
}
static void invalid(){
 for(const char *body:{"null","[]","{}",R"({"action":"upsert","revision":0,"station":{}})",R"({"action":"remove","id":"beijing_music","revision":true})",R"({"action":"remove","id":"beijing_music","revision":1.0})",R"({"action":"remove","id":"beijing_music","revision":-1})",R"({"action":"remove","id":"beijing_music","revision":4294967296})",R"({"action":"remove","id":"beijing_music","extra":true})",R"({"action":"remove","id":"beijing_music\u0000hidden","revision":1})",R"({"action":"remove\u0000hidden","id":"beijing_music","revision":1})"}){
  JsonDocument request;check(!deserializeJson(request,body));unchanged(request);
 }
 for(const std::string &name:{std::string(""),std::string("   "),std::string(25,'a'),std::string("\n自选"),std::string("自选\t"),std::string("自\n选"),std::string("\xf0\x9f\xa6\x84"),std::string("\xc0\xaf"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\xe4\xb8"),std::string("\xef\xbb\xbf名字")}){
  auto request=station(name);unchanged(request);
 }
 auto malformed=station();malformed["station"]["name"]=std::string("自选\0隐藏",13);unchanged(malformed);
 malformed=station();malformed["station"]["tag"]=std::string(25,'a');unchanged(malformed);
 malformed=station();malformed["station"]["tag"]="音乐\r";unchanged(malformed);
 malformed=station();malformed["station"].remove("tag");unchanged(malformed);
 malformed=station();malformed["station"]["extra"]=true;unchanged(malformed);
 malformed=station();malformed["station"]["id"]="missing";unchanged(malformed);
 malformed=station();malformed["station"]["id"]=42;unchanged(malformed);
 for(const char *url:{"", "ftp://radio.example/a.mp3","file:///radio.mp3","https://","https://radio.example:0/a","https://radio.example:65536/a","https://radio.example:/a","https://radio.example:no/a","https://user@radio.example/a","https://radio.example/a#fragment","https://radio.example/live.m3u8?token=1","http://radio.example/LIST.PLS","https://radio.example/a.m3u","https://radio.example/a.mpd","https://radio.example/a b","https://radio.example\\stream","https://电台.example/a","https://radio.example/a\n","https://radio.example/%s a"}){
  auto request=station("自选",url);unchanged(request);
 }
 auto tooLong=station("自选",std::string("https://radio.example/")+std::string(500,'a'));unchanged(tooLong);
 auto duplicate=station("自选",RadioCatalog::get(0).url.c_str());unchanged(duplicate);
 JsonDocument stale;stale["action"]="remove";stale["revision"]=RadioCatalog::revision()-1;stale["id"]="beijing_music";
 String error;unsigned status=0;check(!mutate(stale,&error,&status));check(status==409&&error=="电台列表已更新，请刷新后重试");
}
static void transactions(){
 auto request=station("  自选音乐  ","HTTPS://RADIO.EXAMPLE?token=A");check(mutate(request));check(RadioCatalog::count()==7);
 auto saved=RadioCatalog::get(6);check(saved.id=="custom_1"&&saved.name=="自选音乐"&&saved.url=="https://radio.example/?token=A");
 request=station("重复","https://radio.example/?token=A");unchanged(request);
 request=station("换个名字","https://radio.example/changed",saved.id.c_str());check(mutate(request));
 check(saved.name=="自选音乐"&&RadioCatalog::get(6).name=="换个名字");
 auto original=file();auto revision=RadioCatalog::revision();
 auto edit=station("保存测试","https://radio.example/new",saved.id.c_str());
 unsigned status=0;LittleFS.failWrites=true;check(!mutate(edit,nullptr,&status));LittleFS.resetFaults();check(status==500&&file()==original&&RadioCatalog::revision()==revision);
 LittleFS.writeLimit=40;check(!mutate(edit));LittleFS.resetFaults();check(file()==original&&RadioCatalog::revision()==revision);
 LittleFS.failRemovePath="/radio.bak";check(!mutate(edit));LittleFS.resetFaults();check(file()==original&&RadioCatalog::revision()==revision);
 LittleFS.failRenameFrom="/radio.json";check(!mutate(edit));LittleFS.resetFaults();check(file()==original&&RadioCatalog::revision()==revision);
 LittleFS.failRenameFrom="/radio.tmp";check(!mutate(edit));LittleFS.resetFaults();check(file()==original&&RadioCatalog::revision()==revision);
 check(RadioCatalog::get(6).name=="换个名字");
 // Deletion preserves stable IDs and IDs are never reused by subsequent adds.
 check(remove(saved.id));request=station();check(mutate(request));check(RadioCatalog::get(6).id=="custom_2");
 request=station(std::string(24,'a'),std::string("https://radio.example/")+std::string(490,'a'));check(mutate(request));check(RadioCatalog::get(7).url.length()==512);
 request=station("直播入口","http://radio.example:65535/live");check(mutate(request));
 request=station("IPv6","http://[::1]:8000/live.aac");check(mutate(request));
 std::string chinese;for(unsigned i=0;i<24;++i)chinese+="中";
 request=station(chinese,"https://radio.example/chinese");request["station"]["tag"]="";check(mutate(request));check(RadioCatalog::get(RadioCatalog::count()-1).name.length()==72);
 while(RadioCatalog::count()<20){auto request=station("更多音乐","https://radio.example/live/"+std::to_string(RadioCatalog::count()));check(mutate(request));}
 request=station("超出上限","https://radio.example/over-limit");unchanged(request);
 request=station("满额编辑","https://radio.example/full-edit",RadioCatalog::get(0).id.c_str());check(mutate(request));check(RadioCatalog::count()==20&&RadioCatalog::get(0).id=="beijing_music");
 check(remove(RadioCatalog::get(3).id));request=station("补回一个","https://radio.example/refill");check(mutate(request));check(RadioCatalog::count()==20);
 JsonDocument snapshot;RadioCatalog::snapshot(snapshot.to<JsonObject>());check(snapshot["limit"]==20&&snapshot["stations"].size()==20&&snapshot["revision"]==RadioCatalog::revision());
 // Real mutex coverage: readers get owned snapshots while writes commit.
 std::atomic<bool> reading{true},valid{true};std::thread reader([&]{while(reading){auto item=RadioCatalog::get(0);if(item.id!="beijing_music"||item.url.isEmpty())valid=false;}});
 for(unsigned i=0;i<5;++i){request=station("并发编辑","https://radio.example/read/"+std::to_string(i),"beijing_music");check(mutate(request));}
 reading=false;reader.join();check(valid);
 check(snapshot["stations"][0]["name"]=="满额编辑");
 while(RadioCatalog::count())check(remove(RadioCatalog::get(0).id));check(RadioCatalog::count()==0);
 request=station();check(mutate(request));check(RadioCatalog::count()==1&&RadioCatalog::get(0).id!="custom_1"&&RadioCatalog::get(0).id!="custom_2");
 check(remove(RadioCatalog::get(0).id));check(RadioCatalog::count()==0);
}
int main(int argc,char **argv){
 check(argc==3);LittleFS.directory=argv[1];check(RadioCatalog::begin());std::string mode=argv[2];
 if(mode=="test"){defaults();invalid();transactions();}
 else if(mode=="empty"){check(RadioCatalog::count()==0&&RadioCatalog::revision()>1);check(RadioCatalog::storageError().isEmpty());}
 else if(mode=="seed"){
  defaults();auto request=station("自选一","https://radio.example/one");check(mutate(request));request=station("自选二","https://radio.example/two");check(mutate(request));check(RadioCatalog::revision()==3);
 }
 else if(mode=="restore"){
  check(RadioCatalog::count()==8&&RadioCatalog::revision()==3);check(RadioCatalog::get(6).id=="custom_1"&&RadioCatalog::get(7).id=="custom_2");check(RadioCatalog::storageError().isEmpty());
  auto request=station("自选三","https://radio.example/three");check(mutate(request));check(RadioCatalog::get(8).id=="custom_3");
 }
 else if(mode=="recovery"){
  check(RadioCatalog::count()==7&&RadioCatalog::revision()==2&&RadioCatalog::get(6).id=="custom_1");check(!RadioCatalog::storageError().isEmpty());
  auto request=station("恢复保存","https://radio.example/recovered");check(mutate(request));check(RadioCatalog::count()==8&&RadioCatalog::get(7).id=="custom_2"&&RadioCatalog::storageError().isEmpty());
 }
 else if(mode=="defaults-error"){defaults();check(!RadioCatalog::storageError().isEmpty());}
 else if(mode=="failed-commit"){
  check(RadioCatalog::count()==8&&RadioCatalog::revision()==3);auto before=RadioCatalog::get(7);auto request=station("无法提交","https://radio.example/fault",before.id.c_str());
  LittleFS.failRenameTo="/radio.json";check(!mutate(request));check(RadioCatalog::count()==8&&RadioCatalog::revision()==3&&RadioCatalog::get(7).name==before.name);
 }
 else if(mode=="restore-failed-commit"){
  check(RadioCatalog::count()==8&&RadioCatalog::revision()==3&&RadioCatalog::get(7).name=="自选二");check(!RadioCatalog::storageError().isEmpty());
 }
 else if(mode=="maximum-json"){
  while(RadioCatalog::count())check(remove(RadioCatalog::get(0).id));
  std::string text;for(unsigned i=0;i<24;++i)text+="中";
  for(unsigned i=0;i<20;++i){
   std::string digits=i<10?"0"+std::to_string(i):std::to_string(i);
   auto request=station(text,"http://a/"+digits+std::string(501,'"'));request["station"]["tag"]=text;check(mutate(request));
  }
  check(file().size()>24000&&file().size()<32768&&RadioCatalog::count()==20);
 }
 else if(mode=="restore-maximum-json"){
  check(RadioCatalog::count()==20&&RadioCatalog::storageError().isEmpty());
  for(unsigned i=0;i<20;++i){auto entry=RadioCatalog::get(i);check(entry.name.length()==72&&entry.tag.length()==72&&entry.url.length()==512);}
 }
 else {std::cerr<<"unknown mode";return 2;}
 std::cout<<"PASS "<<mode<<": "<<checks<<" checks\n";
}
