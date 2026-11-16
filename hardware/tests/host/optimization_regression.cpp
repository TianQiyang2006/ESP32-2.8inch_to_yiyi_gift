#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/bounded_stream.h"
#include "hardware/arduino/yiyi_desk/src/reading_cache.h"
#include "hardware/arduino/yiyi_desk/src/retry_policy.h"
#include "hardware/arduino/yiyi_desk/src/request_deadline.h"
#include "hardware/arduino/yiyi_desk/src/schedule_store.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

int64_t HostEpoch=DeskLogic::beijingEpoch(2026,10,23,12,0);
static void put(const char *path,const std::string &text){std::ofstream(LittleFS.directory+path,std::ios::binary)<<text;}
static std::string read(const char *path){std::ifstream file(LittleFS.directory+path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static void clean(){
 LittleFS.resetFaults();
 for(const char *path:{"/schedule.txt","/schedule.bak","/schedule.tmp","/gift.json","/gift.bak","/gift.tmp"})LittleFS.remove(path);
}
static bool patch(const char *text){JsonDocument json;assert(!deserializeJson(json,text));String error;return GiftData::patch(json.as<JsonVariantConst>(),error);}
static const char *oldSchedule="V1\nTERM|2026-09-01|20\nP|1|08:00|08:45\nC|1|1|2|0x123456|0xfffff|原课程|老师|教室\n";
static const char *newSchedule="V1\nTERM|2026-09-01|20\nP|1|08:00|08:45\nC|2|1|2|0x123456|0xfffff|新课程|老师|教室\n";
static bool save(const char *text){return ScheduleStore::saveText(text,strlen(text));}
static void expectOld(){assert(ScheduleStore::imported());assert(std::string(ScheduleStore::course(0).name)=="原课程");}

static void networkAndCache(){
 assert(RetryPolicy::exponential(60000,600000,0)==60000);
 assert(RetryPolicy::exponential(60000,600000,1)==60000);
 assert(RetryPolicy::exponential(60000,600000,2)==120000);
 assert(RetryPolicy::exponential(60000,600000,4)==480000);
 assert(RetryPolicy::exponential(60000,600000,5)==600000);
 assert(RetryPolicy::exponential(60000,600000,10)==600000);
 assert(!RequestDeadline::expired(999,1000));
 assert(RequestDeadline::expired(1000,1000));
 assert(!RequestDeadline::expired(0xfffffffeu,1));
 assert(RequestDeadline::expired(1,1));
 BoundedStringStream exact(5);assert(exact.write((const uint8_t*)"abc",3)==3);assert(exact.write((const uint8_t*)"de",2)==2);assert(!exact.failed()&&exact.body()=="abcde");
 assert(exact.write('f')==0&&exact.exceeded()&&exact.failed());assert(exact.write('g')==0&&exact.body().length()==5);
 BoundedStringStream chunked(4);assert(chunked.write((const uint8_t*)"abcdef",6)==4&&chunked.exceeded());assert(chunked.body()=="abcd");
 BoundedStringStream empty(0);assert(empty.write('a')==0&&empty.exceeded());
 JsonDocument json;assert(!deserializeJson(json,R"({"text":"你好，依依","date":"2028-02-29","updatedAt":123})"));assert(ReadingCache::valid("quote",json.as<JsonVariantConst>()));
 json["date"]="2026-02-29";assert(!ReadingCache::valid("quote",json.as<JsonVariantConst>()));json.remove("date");json.remove("updatedAt");assert(ReadingCache::valid("poem",json.as<JsonVariantConst>()));
 json["text"]=std::string(3001,'a');assert(!ReadingCache::valid("quote",json.as<JsonVariantConst>()));json["text"]="\xc0\xaf";assert(!ReadingCache::valid("quote",json.as<JsonVariantConst>()));
 json["text"]="有效";json["updatedAt"]=-1;assert(!ReadingCache::valid("quote",json.as<JsonVariantConst>()));json.remove("updatedAt");json["extra"]=true;assert(!ReadingCache::valid("quote",json.as<JsonVariantConst>()));
 assert(!deserializeJson(json,R"({"letter":{"title":"保留标题"},"readingCache":{"quote":{"text":"有效"},"poem":{"text":42},"unknown":{"text":"无效类型"}}})"));
 ReadingCache::sanitize(json.as<JsonObject>());assert(json["letter"]["title"]=="保留标题");assert(json["readingCache"].size()==1&&json["readingCache"]["quote"]["text"]=="有效");
 std::cout<<"PASS: bounded response sink and reading cache validation\n";
}
static void scheduleTransactions(){
 clean();assert(save(oldSchedule));const auto original=read("/schedule.txt");
 LittleFS.writeLimit=11;assert(!save(newSchedule));LittleFS.resetFaults();assert(read("/schedule.txt")==original);expectOld();
 put("/schedule.bak",original);put("/schedule.txt","corrupt main");assert(ScheduleStore::load());expectOld();
 assert(save(newSchedule));assert(read("/schedule.bak")==original);put("/schedule.txt","corrupt main again");assert(ScheduleStore::load());expectOld();
 put("/schedule.txt",original);LittleFS.failRenameFrom="/schedule.tmp";assert(!save(newSchedule));LittleFS.resetFaults();assert(ScheduleStore::load());expectOld();
 put("/schedule.bak",original);LittleFS.failRemovePath="/schedule.bak";assert(!ScheduleStore::clear());LittleFS.resetFaults();assert(ScheduleStore::load());expectOld();
 assert(ScheduleStore::clear());assert(!LittleFS.exists("/schedule.txt")&&!LittleFS.exists("/schedule.bak"));assert(!ScheduleStore::load()&&!ScheduleStore::imported());
 std::cout<<"PASS: schedule short writes, corrupt main recovery, backup retention, rollback and reset\n";
}
static void giftTransactions(){
 clean();assert(GiftData::begin());assert(patch(R"({"letter":{"title":"原留言"},"deviceSettings":{"volume":37}})"));const auto original=read("/gift.json");
 LittleFS.writeLimit=19;assert(!patch(R"({"letter":{"title":"新留言"}})"));LittleFS.resetFaults();assert(read("/gift.json")==original&&GiftData::get("letter")["title"]=="原留言");
 LittleFS.failRenameFrom="/gift.tmp";assert(!patch(R"({"letter":{"title":"新留言"}})"));LittleFS.resetFaults();assert(GiftData::begin());assert(GiftData::get("letter")["title"]=="原留言");
 put("/gift.bak",original);put("/gift.json","{corrupt");assert(GiftData::begin());assert(GiftData::get("letter")["title"]=="原留言");
 assert(patch(R"({"letter":{"title":"新留言"}})"));assert(read("/gift.bak")==original);
 put("/gift.json","{corrupt again");assert(GiftData::begin());assert(GiftData::get("letter")["title"]=="原留言");
 JsonDocument stored;assert(!deserializeJson(stored,original));stored["readingCache"]["poem"]["text"]=42;stored["readingCache"]["quote"]["text"]="有效缓存";String serialized;serializeJson(stored,serialized);put("/gift.json",serialized);
 assert(GiftData::begin());assert(GiftData::get("letter")["title"]=="原留言"&&GiftData::get("deviceSettings")["volume"]==37);assert(GiftData::get("readingCache").size()==1&&GiftData::get("readingCache")["quote"]["text"]=="有效缓存");
 std::cout<<"PASS: persistent settings survive write failures, rename failures and corrupt cache\n";
}
static void clockSettingsMigration(){
 clean();
 put("/gift.json",R"({"clockMode":{"preset":"matrix","seconds":false},"deviceSettings":{"brightness":63,"volume":37},"letter":{"title":"旧留言"}})");
 assert(GiftData::begin());
 assert(GiftData::get("clockMode")["preset"]=="matrix");
 assert(GiftData::get("deviceSettings")["brightness"]==63&&GiftData::get("deviceSettings")["volume"]==37);
 assert(GiftData::get("letter")["title"]=="旧留言");
 assert(GiftData::get("dashboardClock")["weight"]==700&&GiftData::get("dashboardClock")["seconds"]==false);
 assert(patch(R"({"clockMode":{"preset":"dashboard"},"dashboardClock":{"color":"#659fe8","weight":900,"opacity":75,"spacing":-8,"seconds":true,"hour24":false,"tilt":false,"lunar":false,"festival":false,"rain":false,"snow":false,"thunder":false}})"));
 const auto saved=read("/gift.json");
 for(const char *invalid:{R"({"clockMode":{"preset":"unknown"}})",R"({"dashboardClock":{"color":"#12gg56"}})",R"({"dashboardClock":{"weight":750}})",R"({"dashboardClock":{"weight":1000}})",R"({"dashboardClock":{"opacity":0}})",R"({"dashboardClock":{"spacing":-19}})",R"({"dashboardClock":{"seconds":1}})",R"({"dashboardClock":{"unknown":true}})"}){
  assert(!patch(invalid));assert(read("/gift.json")==saved);
 }
 assert(GiftData::begin());
 assert(GiftData::get("clockMode")["preset"]=="dashboard"&&GiftData::get("dashboardClock")["color"]=="#659fe8");
 assert(GiftData::get("dashboardClock")["seconds"]==true&&GiftData::get("dashboardClock")["hour24"]==false);
 assert(GiftData::get("deviceSettings")["volume"]==37&&GiftData::get("letter")["title"]=="旧留言");
 assert(patch(R"({"clockMode":{"preset":"minimal"}})"));
 assert(patch(R"({"clockMode":{"preset":"compact"}})"));
 std::cout<<"PASS: legacy clock migration, all dashboard fields, persistence and invalid-setting rejection\n";
}
int main(){
 LittleFS.directory="build/host-regression/store";assert(LittleFS.begin(false,"",10,""));
 networkAndCache();scheduleTransactions();giftTransactions();clockSettingsMigration();clean();
}
