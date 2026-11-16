#include "LittleFS.h"
#include "hardware/arduino/yiyi_desk/src/gift_data.h"
#include "hardware/arduino/yiyi_desk/src/pages/days_store.h"
#include "hardware/arduino/yiyi_desk/src/pages/days_logic.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int64_t HostEpoch=DeskLogic::beijingEpoch(2026,11,14,23,59);
static unsigned checks=0;
static void check(bool value){++checks;assert(value);}
static std::string file(){std::ifstream stream(LittleFS.directory+"/gift.json",std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
static void put(const char *path,const std::string &text){std::ofstream(LittleFS.directory+path,std::ios::binary)<<text;}
static bool mutate(const char *body){JsonDocument request;check(!deserializeJson(request,body));String error;const bool result=DaysStore::mutate(request.as<JsonVariantConst>(),error);if(!result)check(!error.isEmpty());return result;}
static bool patch(JsonDocument &request){String error;return GiftData::patch(request.as<JsonVariantConst>(),error);}
static JsonArrayConst items(){return GiftData::get("countdownDays")["items"].as<JsonArrayConst>();}
static const char *add=R"({"action":"upsert","item":{"id":0,"name":"  依依生日  ","date":"2028-02-29","yearly":true,"color":1}})";
static const char *edit=R"({"action":"upsert","item":{"id":1,"name":"一起旅行","date":"2027-01-01","yearly":false,"color":2}})";
static const char *addCountup=R"({"action":"upsert","item":{"id":0,"name":"  在一起的日子  ","date":"2024-02-29","yearly":false,"color":3,"mode":"countup"}})";
static const char *addExplicitCountdown=R"({"action":"upsert","item":{"id":0,"name":"新的期待","date":"2027-01-01","yearly":false,"color":0,"mode":"countdown"}})";
static const char *addBirthday=R"({"action":"upsert","item":{"id":0,"name":"  小太阳的生日  ","date":"2001-10-03","yearly":true,"color":1,"category":"birthday"}})";
static const char *addLove=R"({"action":"upsert","item":{"id":0,"name":"我们的第一天","date":"2023-09-20","yearly":false,"color":3,"mode":"countup","category":"love"}})";
static const char *styles[]={"classic","laurel","ink","heart","dots"};
static void styledRequest(JsonDocument &request,int id,const char *style,unsigned options,int color){
 check(!deserializeJson(request,edit));auto item=request["item"].as<JsonObject>();
 item["id"]=id;item["style"]=style;item["color"]=color;
 if(options&1)item["mode"]="countup";
 if(options&2)item["category"]="love";
}
static bool mutateDocument(JsonDocument &request){std::string body;serializeJson(request,body);return mutate(body.c_str());}
static void dates(){
 auto value=DaysLogic::evaluate("2026-11-15",false,"2026-11-14");check(value.valid&&value.days==1);
 value=DaysLogic::evaluate("2026-11-14",false,"2026-11-14");check(value.valid&&value.days==0);
 value=DaysLogic::evaluate("2026-11-13",false,"2026-11-14");check(value.valid&&value.days==-1);
 check(DaysLogic::evaluate("2027-01-01",false,"2026-12-31").days==1);
 check(DaysLogic::evaluate("2028-03-01",false,"2028-02-28").days==2);
 check(DaysLogic::evaluate("2027-03-01",false,"2027-02-28").days==1);
 check(!DaysLogic::evaluate("2026-02-29",false,"2026-11-14").valid);
 check(!DaysLogic::evaluate("2026-1-14",false,"2026-11-14").valid);
 check(!DaysLogic::evaluate("2026-11-14x",false,"2026-11-14").valid);
 check(!DaysLogic::evaluate("2026-11-14",false,"").valid);
 value=DaysLogic::evaluate("2024-02-29",true,"2027-02-28");check(value.valid&&value.days==0&&std::string(value.target)=="2027-02-28");
 value=DaysLogic::evaluate("2024-02-29",true,"2027-03-01");check(value.valid&&std::string(value.target)=="2028-02-29"&&value.days==365);
 value=DaysLogic::evaluate("2028-02-29",true,"2026-11-14");check(value.valid&&std::string(value.target)=="2028-02-29");
 value=DaysLogic::evaluate("1900-01-01",false,"2099-12-31");check(value.valid&&value.days==-73048);
 value=DaysLogic::evaluate("1900-01-01",true,"2099-12-31");check(value.valid&&std::string(value.target)=="2100-01-01"&&value.days==1);
 // Countup measures whole natural dates, retaining the original start date.
 value=DaysLogic::evaluate("2026-11-14",false,"2026-11-14",true);check(value.valid&&value.days==0);
 value=DaysLogic::evaluate("2026-11-14",false,"2026-11-15",true);check(value.valid&&value.days==1);
 value=DaysLogic::evaluate("2026-11-15",false,"2026-11-14",true);check(value.valid&&value.days==-1);
 value=DaysLogic::evaluate("2024-02-29",false,"2027-03-01",true);
 check(value.valid&&value.days==1096&&std::string(value.target)=="2024-02-29");
 check(value.year==2024&&value.month==2&&value.day==29);
 check(DaysLogic::evaluate("2028-02-28",false,"2028-03-01",true).days==2);
 check(DaysLogic::evaluate("2027-02-28",false,"2027-03-01",true).days==1);
 check(DaysLogic::evaluate("2026-12-31",false,"2027-01-01",true).days==1);
 check(DaysLogic::evaluate("1900-01-01",false,"2099-12-31",true).days==73048);
 check(DaysLogic::evaluate("2099-12-31",false,"1900-01-01",true).days==-73048);
 check(!DaysLogic::evaluate("2024-02-29",true,"2027-02-28",true).valid);
 check(!DaysLogic::evaluate("2026-02-29",false,"2026-11-14",true).valid);
 check(!DaysLogic::evaluate("2026-11-14",false,"",true).valid);
 // Device dates change at Beijing midnight even when just one second elapsed.
 HostEpoch=DeskLogic::beijingEpoch(2026,11,14,23,59)+59;
 check(GiftData::today()=="2026-11-14");
 check(DaysLogic::evaluate("2026-11-14",false,GiftData::today().c_str(),true).days==0);
 ++HostEpoch;check(GiftData::today()=="2026-11-15");
 check(DaysLogic::evaluate("2026-11-14",false,GiftData::today().c_str(),true).days==1);
 HostEpoch=0;check(!DaysLogic::evaluate("2026-11-14",false,GiftData::today().c_str(),true).valid);
 HostEpoch=DeskLogic::beijingEpoch(2026,11,14,23,59);
}
static void invalidMutations(){
 const auto before=file();const uint32_t revision=GiftData::revision();
 for(const char *body:{"null","[]","{}",R"({"action":"bad","id":1})",R"({"action":"remove","id":1,"extra":true})",R"({"action":"remove","id":100})",R"({"action":"upsert","item":{"id":100,"name":"新条目","date":"2027-01-01","yearly":false,"color":0}})",R"({"action":"upsert","item":{"id":0,"name":"","date":"2027-01-01","yearly":false,"color":0}})",R"({"action":"upsert","item":{"id":0,"name":"日期错误","date":"2026-02-29","yearly":false,"color":0}})",R"({"action":"upsert","item":{"id":0,"name":"空字符日期","date":"2027-01-01\u0000hidden","yearly":false,"color":0}})",R"({"action":"upsert","item":{"id":0,"name":"空字符\u0000隐藏名称","date":"2027-01-01","yearly":false,"color":0}})",R"({"action":"upsert","item":{"id":0,"name":"配色错误","date":"2027-01-01","yearly":false,"color":4}})",R"({"action":"upsert","item":{"id":0,"name":"类型错误","date":"2027-01-01","yearly":1,"color":0}})",R"({"action":"upsert","item":{"id":0,"name":"遗漏字段","date":"2027-01-01","yearly":false}})",R"({"action":"upsert","item":{"id":0,"name":"多余字段","date":"2027-01-01","yearly":false,"color":0,"extra":true}})",R"({"action":"upsert","item":{"id":0,"name":"emoji \ud83e\udd84","date":"2027-01-01","yearly":false,"color":0}})"}){
  check(!mutate(body));check(file()==before);check(GiftData::revision()==revision);
 }
 JsonDocument request;auto list=request["countdownDays"]["items"].to<JsonArray>();
 list.add(items()[0]);list.add(items()[0]);check(!patch(request));check(file()==before);
 request["countdownDays"]["items"][1]["id"]=2;
 request["countdownDays"]["items"][1]["name"]=std::string(25,'a');check(!patch(request));check(file()==before);
 request["countdownDays"]["items"][1]["name"]=" 合法名称";check(!patch(request));check(file()==before);
 // Direct list replacement must also reject hidden JSON suffixes, independent
 // of the endpoint's name normalization.
 for(const char *body:{R"({"countdownDays":{"items":[{"id":1,"name":"有效名称","date":"2027-01-01\u0000hidden","yearly":false,"color":0}]}})",
                      R"({"countdownDays":{"items":[{"id":1,"name":"有效\u0000隐藏名称","date":"2027-01-01","yearly":false,"color":0}]}})"}){
  JsonDocument direct;check(!deserializeJson(direct,body));check(!patch(direct));check(file()==before);check(GiftData::revision()==revision);
 }
}
static void transactions(){
 check(mutate(add));check(items().size()==1&&items()[0]["id"]==1&&items()[0]["name"]=="依依生日");
 check(GiftData::begin());check(items().size()==1&&items()[0]["date"]=="2028-02-29");
 invalidMutations();
 const auto original=file();
 LittleFS.failWrites=true;check(!mutate(edit));LittleFS.resetFaults();check(file()==original&&items()[0]["name"]=="依依生日");
 LittleFS.writeLimit=40;check(!mutate(edit));LittleFS.resetFaults();check(file()==original&&items()[0]["name"]=="依依生日");
 LittleFS.failRenameFrom="/gift.tmp";check(!mutate(edit));LittleFS.resetFaults();check(GiftData::begin());check(items()[0]["name"]=="依依生日");
 check(mutate(edit));check(items()[0]["name"]=="一起旅行");
 put("/gift.json","{broken");check(GiftData::begin());check(items()[0]["name"]=="依依生日");
 // Old stores acquire the empty list without losing unrelated content.
 put("/gift.json",R"({"letter":{"title":"旧留言"},"clockMode":{"preset":"matrix"}})");
 check(GiftData::begin());check(items().size()==0&&GiftData::get("letter")["title"]=="旧留言"&&GiftData::get("clockMode")["preset"]=="matrix");
 check(mutate(add));
 for(unsigned i=1;i<DaysLogic::MaxItems;++i)check(mutate(add));
 check(items().size()==20);
 const auto full=file();check(!mutate(add));check(file()==full);
 check(mutate(edit));check(items().size()==20&&items()[0]["name"]=="一起旅行");
 check(mutate(R"({"action":"remove","id":1})"));check(items().size()==19&&items()[0]["id"]==2);
 check(!mutate(edit));check(mutate(add));check(items().size()==20&&items()[19]["id"]==21);
 // Loading keeps previously accepted text even if a new font omits a glyph.
 JsonDocument old;check(!deserializeJson(old,file()));old["countdownDays"]["items"][0]["name"]="旧字 \xf0\x9f\xa6\x84";
 std::string serialized;serializeJson(old,serialized);put("/gift.json",serialized);
 check(GiftData::begin());check(items().size()==20&&items()[0]["name"]=="旧字 \xf0\x9f\xa6\x84");
}
static void invalidModes(){
 const auto before=file();const uint32_t revision=GiftData::revision();
 // Both public write paths reject every invalid type, unknown value and hidden
 // suffix. A rejected mode cannot replace the valid mixed list in RAM or flash.
 for(const char *mode:{"null","true","1","[]","{}",R"("")",R"("Countup")",R"("elapsed")",R"("countup ")",R"("countup\u0000hidden")",R"("countdown\u0000hidden")"}){
  JsonDocument parsed;check(!deserializeJson(parsed,mode));
  JsonDocument endpoint;check(!deserializeJson(endpoint,addCountup));
  endpoint["item"]["id"]=2;endpoint["item"]["mode"].set(parsed.as<JsonVariantConst>());
  std::string body;serializeJson(endpoint,body);check(!mutate(body.c_str()));
  check(file()==before&&GiftData::revision()==revision&&items()[1]["mode"]=="countup");
  JsonDocument direct;direct["countdownDays"]["items"].set(items());
  direct["countdownDays"]["items"][1]["mode"].set(parsed.as<JsonVariantConst>());
  check(!patch(direct));check(file()==before&&GiftData::revision()==revision&&items()[1]["mode"]=="countup");
 }
 for(const char *body:{R"({"action":"upsert","item":{"id":2,"name":"在一起的日子","date":"2024-02-29","yearly":true,"color":3,"mode":"countup"}})",
                      R"({"action":"upsert","item":{"id":2,"name":"在一起的日子","date":"2024-02-29","yearly":false,"color":3,"mode":"countup","extra":true}})",
                      R"({"action":"upsert","item":{"id":2,"name":"在一起的日子","date":"2024-02-29","yearly":false,"mode":"countup"}})"}){
  check(!mutate(body));check(file()==before&&GiftData::revision()==revision);
 }
 JsonDocument direct;direct["countdownDays"]["items"].set(items());
 direct["countdownDays"]["items"][1]["yearly"]=true;
 check(!patch(direct));check(file()==before&&GiftData::revision()==revision);
 direct["countdownDays"]["items"][1]["yearly"]=false;
 direct["countdownDays"]["items"][1]["extra"]=true;
 check(!patch(direct));check(file()==before&&GiftData::revision()==revision);
}
static void countupTransactions(){
 // Reset the unsupported-font fixture left by the older-store tests.
 put("/gift.json",R"({"letter":{"title":"旧留言"}})");check(GiftData::begin());
 check(items().size()==0);check(mutate(add));check(mutate(addCountup));check(mutate(addExplicitCountdown));
 check(items().size()==3&&!items()[0].containsKey("mode"));
 check(items()[1]["id"]==2&&items()[1]["name"]=="在一起的日子"&&items()[1]["mode"]=="countup"&&items()[1]["yearly"]==false);
 check(items()[2]["id"]==3&&items()[2]["mode"]=="countdown");
 check(GiftData::begin());check(items().size()==3&&items()[1]["date"]=="2024-02-29"&&items()[1]["mode"]=="countup");
 check(GiftData::get("letter")["title"]=="旧留言");
 invalidModes();
 const auto before=file();const uint32_t revision=GiftData::revision();
 const char *switchMode=R"({"action":"upsert","item":{"id":2,"name":"新的纪念","date":"2027-01-01","yearly":true,"color":2,"mode":"countdown"}})";
 LittleFS.failWrites=true;check(!mutate(switchMode));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[1]["mode"]=="countup");
 LittleFS.writeLimit=40;check(!mutate(switchMode));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[1]["date"]=="2024-02-29");
 LittleFS.failRenameFrom="/gift.tmp";check(!mutate(switchMode));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[1]["mode"]=="countup");
 check(GiftData::begin());check(items().size()==3&&items()[1]["mode"]=="countup");
 check(mutate(switchMode));check(items()[1]["mode"]=="countdown"&&items()[1]["yearly"]==true);
 check(GiftData::begin());check(items()[1]["mode"]=="countdown"&&items()[1]["date"]=="2027-01-01");
 check(mutate(R"({"action":"upsert","item":{"id":2,"name":"在一起的日子","date":"2024-02-29","yearly":false,"color":3,"mode":"countup"}})"));
 check(items()[1]["mode"]=="countup"&&items()[1]["yearly"]==false);
 // A legacy five-field update deliberately selects countdown again.
 check(mutate(R"({"action":"upsert","item":{"id":2,"name":"新的纪念","date":"2027-01-01","yearly":true,"color":2}})"));
 check(!items()[1].containsKey("mode")&&items()[1]["yearly"]==true);
 JsonDocument direct;direct["countdownDays"]["items"].set(items());
 direct["countdownDays"]["items"][1]["mode"]="countup";direct["countdownDays"]["items"][1]["yearly"]=false;
 check(patch(direct));check(GiftData::begin());
 check(!items()[0].containsKey("mode")&&items()[1]["mode"]=="countup"&&items()[2]["mode"]=="countdown");
 check(mutate(R"({"action":"remove","id":2})"));
 check(items().size()==2&&items()[0]["id"]==1&&items()[1]["id"]==3);
 check(GiftData::begin());check(items().size()==2&&items()[1]["mode"]=="countdown");
}
static void invalidCategories(){
 const auto before=file();const uint32_t revision=GiftData::revision();
 for(const char *category:{"null","false","2","[]","{}",R"("")",R"("Birthday")",R"("anniversary")",R"("love ")",R"("general\u0000hidden")",R"("birthday\u0000hidden")",R"("love\u0000hidden")"}){
  JsonDocument parsed;check(!deserializeJson(parsed,category));
  JsonDocument endpoint;check(!deserializeJson(endpoint,addLove));
  endpoint["item"]["id"]=3;endpoint["item"]["category"].set(parsed.as<JsonVariantConst>());
  std::string body;serializeJson(endpoint,body);check(!mutate(body.c_str()));
  check(file()==before&&GiftData::revision()==revision&&items()[2]["category"]=="love");
  JsonDocument direct;direct["letter"]["title"]="不应保存";
  direct["countdownDays"]["items"].set(items());
  direct["countdownDays"]["items"][2]["category"].set(parsed.as<JsonVariantConst>());
  check(!patch(direct));check(file()==before&&GiftData::revision()==revision&&items()[2]["category"]=="love");
  check(GiftData::get("letter")["title"]=="旧留言");
 }
 for(const char *body:{R"({"action":"upsert","item":{"id":3,"name":"我们的第一天","date":"2023-09-20","yearly":false,"color":3,"mode":"countup","category":"love","extra":true}})",
                      R"({"action":"upsert","item":{"id":3,"name":"我们的第一天","date":"2023-09-20","yearly":false,"color":3,"category":"love","extra":true}})",
                      R"({"action":"upsert","item":{"id":3,"name":"我们的第一天","date":"2023-09-20","yearly":false,"mode":"countup","category":"love"}})",
                      R"({"action":"upsert","item":{"id":3,"name":"我们的第一天","date":"2023-09-20","yearly":true,"color":3,"mode":"countup","category":"love"}})"}){
  check(!mutate(body));check(file()==before&&GiftData::revision()==revision);
 }
 JsonDocument direct;direct["countdownDays"]["items"].set(items());
 direct["countdownDays"]["items"][2]["extra"]=true;
 check(!patch(direct));check(file()==before&&GiftData::revision()==revision);
}
static void categoryTransactions(){
 put("/gift.json",R"({"letter":{"title":"旧留言"}})");check(GiftData::begin());
 check(mutate(add));check(mutate(addBirthday));check(mutate(addLove));
 check(items().size()==3&&items()[0].size()==5&&items()[1].size()==6&&items()[2].size()==7);
 check(items()[0]["category"].isNull()&&items()[0]["mode"].isNull());
 check(items()[1]["category"]=="birthday"&&items()[1]["mode"].isNull()&&items()[1]["name"]=="小太阳的生日"&&items()[1]["date"]=="2001-10-03");
 check(items()[2]["category"]=="love"&&items()[2]["mode"]=="countup"&&items()[2]["name"]=="我们的第一天"&&items()[2]["date"]=="2023-09-20");
 check(GiftData::begin());check(items()[0]["category"].isNull()&&items()[1]["category"]=="birthday"&&items()[2]["category"]=="love");
 invalidCategories();
 // Category selects the presentation only; every valid mode/repeat combination
 // remains available for a customised name and date in each category.
 for(const char *category:{"general","birthday","love"}){
  for(const char *mode:{"","countdown","countup"}){
   for(bool yearly:{false,true}){
    if(std::string(mode)=="countup"&&yearly)continue;
    JsonDocument endpoint;check(!deserializeJson(endpoint,edit));
    endpoint["item"]["category"]=category;endpoint["item"]["yearly"]=yearly;
    if(*mode)endpoint["item"]["mode"]=mode;
    std::string body;serializeJson(endpoint,body);check(mutate(body.c_str()));
    check(items()[0]["name"]=="一起旅行"&&items()[0]["date"]=="2027-01-01"&&items()[0]["category"]==category&&items()[0]["yearly"]==yearly);
    if(*mode)check(items()[0]["mode"]==mode);else check(items()[0]["mode"].isNull());
   }
  }
 }
 check(mutate(R"({"action":"upsert","item":{"id":2,"name":"依依的新名字","date":"2002-05-06","yearly":false,"color":0,"mode":"countup","category":"birthday"}})"));
 check(mutate(R"({"action":"upsert","item":{"id":3,"name":"一起看日出","date":"2027-06-01","yearly":true,"color":2,"mode":"countdown","category":"love"}})"));
 check(GiftData::begin());
 check(items()[1]["category"]=="birthday"&&items()[1]["name"]=="依依的新名字"&&items()[1]["date"]=="2002-05-06"&&items()[1]["mode"]=="countup");
 check(items()[2]["category"]=="love"&&items()[2]["name"]=="一起看日出"&&items()[2]["date"]=="2027-06-01"&&items()[2]["mode"]=="countdown");
 const auto before=file();const uint32_t revision=GiftData::revision();
 const char *changeCategory=R"({"action":"upsert","item":{"id":2,"name":"再一次相遇","date":"2025-02-03","yearly":false,"color":3,"mode":"countup","category":"love"}})";
 LittleFS.failWrites=true;check(!mutate(changeCategory));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[1]["category"]=="birthday");
 LittleFS.writeLimit=40;check(!mutate(changeCategory));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[1]["name"]=="依依的新名字");
 LittleFS.failRenameFrom="/gift.tmp";check(!mutate(changeCategory));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[1]["category"]=="birthday");
 check(GiftData::begin());check(items()[1]["category"]=="birthday"&&items()[1]["date"]=="2002-05-06");
 check(mutate(changeCategory));check(GiftData::begin());
 check(items()[1]["category"]=="love"&&items()[1]["name"]=="再一次相遇"&&items()[1]["date"]=="2025-02-03");
 // Omitting category remains a legacy/general item without a migration write.
 check(mutate(edit));check(GiftData::begin());
 check(items()[0]["category"].isNull()&&items()[0]["mode"].isNull());
}
static void invalidStyles(){
 const auto before=file();const uint32_t revision=GiftData::revision();
 for(const char *style:{"null","false","2","[]","{}",R"("")",R"("Classic")",R"("custom")",R"("ink ")",R"("classic\u0000hidden")",R"("laurel\u0000hidden")",R"("ink\u0000hidden")",R"("heart\u0000hidden")",R"("dots\u0000hidden")"}){
  JsonDocument parsed;check(!deserializeJson(parsed,style));
  JsonDocument endpoint;endpoint["action"]="upsert";endpoint["item"].set(items()[0]);
  endpoint["item"]["style"].set(parsed.as<JsonVariantConst>());check(!mutateDocument(endpoint));
  check(file()==before&&GiftData::revision()==revision&&items()[0]["style"]=="dots");
  JsonDocument direct;direct["letter"]["title"]="不应保存";direct["countdownDays"]["items"].set(items());
  direct["countdownDays"]["items"][0]["style"].set(parsed.as<JsonVariantConst>());
  check(!patch(direct));check(file()==before&&GiftData::revision()==revision&&items()[0]["style"]=="dots");
  check(GiftData::get("letter")["title"]=="旧留言");
 }
 for(const char *body:{R"({"action":"upsert","item":{"id":1,"name":"一起旅行","date":"2027-01-01","yearly":false,"color":3,"mode":"countup","category":"love","style":"dots","extra":true}})",
                      R"({"action":"upsert","item":{"id":1,"name":"一起旅行","date":"2027-01-01","yearly":false,"color":3,"style":"dots","extra":true}})",
                      R"({"action":"upsert","item":{"id":1,"name":"一起旅行","date":"2027-01-01","yearly":false,"mode":"countup","category":"love","style":"dots"}})",
                      R"({"action":"upsert","item":{"id":1,"name":"一起旅行","date":"2027-01-01","yearly":true,"color":3,"mode":"countup","category":"love","style":"dots"}})"}){
  check(!mutate(body));check(file()==before&&GiftData::revision()==revision);
 }
 JsonDocument direct;direct["countdownDays"]["items"].set(items());
 direct["countdownDays"]["items"][0]["extra"]=true;
 check(!patch(direct));check(file()==before&&GiftData::revision()==revision);
}
static void styleTransactions(){
 put("/gift.json",R"({"letter":{"title":"旧留言"}})");check(GiftData::begin());
 check(mutate(add));check(mutate(addBirthday));check(mutate(addLove));
 check(GiftData::begin());
 check(items()[0]["style"].isNull()&&items()[1]["style"].isNull()&&items()[2]["style"].isNull());
 // A style can be stored alone, with either optional field, or with both. All
 // four saved colours remain valid, including for the two-colour dots UI.
 for(const char *style:styles){
  for(unsigned options=0;options<4;++options){
   JsonDocument request;styledRequest(request,1,style,options,(int)options);
   check(mutateDocument(request));check(GiftData::begin());
   check(items()[0].size()==6u+(options&1)+((options>>1)&1));
   check(items()[0]["style"]==style&&items()[0]["color"]==(int)options&&items()[0]["name"]=="一起旅行"&&items()[0]["date"]=="2027-01-01");
   check((options&1)?items()[0]["mode"]=="countup":items()[0]["mode"].isNull());
   check((options&2)?items()[0]["category"]=="love":items()[0]["category"].isNull());
  }
  // Direct settings updates accept every style without changing the birthday
  // classification, annual repeat, name or date.
  JsonDocument direct;direct["countdownDays"]["items"].set(items());
  direct["countdownDays"]["items"][1]["style"]=style;
  check(patch(direct));check(GiftData::begin());
  check(items()[1]["style"]==style&&items()[1]["category"]=="birthday"&&items()[1]["yearly"]==true&&items()[1]["name"]=="小太阳的生日"&&items()[1]["date"]=="2001-10-03");
 }
 invalidStyles();
 const auto before=file();const uint32_t revision=GiftData::revision();
 JsonDocument request;styledRequest(request,1,"laurel",3,1);request["item"]["name"]="新的相遇";
 LittleFS.failWrites=true;check(!mutateDocument(request));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[0]["style"]=="dots");
 LittleFS.writeLimit=40;check(!mutateDocument(request));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[0]["name"]=="一起旅行");
 LittleFS.failRenameFrom="/gift.tmp";check(!mutateDocument(request));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[0]["style"]=="dots");
 check(GiftData::begin());check(items()[0]["style"]=="dots"&&items()[0]["color"]==3);
 check(mutateDocument(request));check(GiftData::begin());
 check(items()[0]["style"]=="laurel"&&items()[0]["category"]=="love"&&items()[0]["name"]=="新的相遇");
 // Removing style restores category-selected presentation without writing an
 // inferred default into old data or changing neighbouring records.
 check(mutate(edit));check(GiftData::begin());
 check(items()[0]["style"].isNull()&&items()[0]["category"].isNull()&&items()[1]["style"]=="dots"&&items()[2]["style"].isNull());
}
static void invalidTextColors(){
 const auto before=file();const uint32_t revision=GiftData::revision();
 for(const char *color:{"null","true","123456","[]","{}",R"("")",R"("auto")",R"("#fff")",R"("#12345")",R"("#1234567")",R"("1234567")",R"("#GG0000")",R"("#12345g")",R"("#12345 ")",R"(" #123456")",R"("#１２３４５６")",R"("#123\u000056")",R"("#123456\u0000hidden")"}){
  JsonDocument parsed;check(!deserializeJson(parsed,color));
  JsonDocument endpoint;endpoint["action"]="upsert";endpoint["item"].set(items()[0]);
  endpoint["item"]["textColor"].set(parsed.as<JsonVariantConst>());check(!mutateDocument(endpoint));
  check(file()==before&&GiftData::revision()==revision&&items()[0]["textColor"]=="#aBc123");
  JsonDocument direct;direct["letter"]["title"]="不应保存";direct["countdownDays"]["items"].set(items());
  direct["countdownDays"]["items"][0]["textColor"].set(parsed.as<JsonVariantConst>());
  check(!patch(direct));check(file()==before&&GiftData::revision()==revision&&items()[0]["textColor"]=="#aBc123");
  check(GiftData::get("letter")["title"]=="旧留言");
 }
 JsonDocument endpoint;endpoint["action"]="upsert";endpoint["item"].set(items()[0]);
 endpoint["item"]["extra"]=true;check(!mutateDocument(endpoint));
 check(file()==before&&GiftData::revision()==revision);
 JsonDocument direct;direct["countdownDays"]["items"].set(items());
 direct["countdownDays"]["items"][0]["extra"]=true;check(!patch(direct));
 check(file()==before&&GiftData::revision()==revision);
 endpoint["item"].remove("extra");endpoint["item"]["yearly"]=true;
 check(!mutateDocument(endpoint));check(file()==before&&GiftData::revision()==revision);
}
static void textColorTransactions(){
 put("/gift.json",R"({"letter":{"title":"旧留言"}})");check(GiftData::begin());
 check(mutate(add));check(mutate(addBirthday));check(mutate(addLove));check(GiftData::begin());
 for(JsonVariantConst item:items())check(item["textColor"].isNull());
 // Every subset of the four optional fields remains valid. A missing colour
 // stays absent, so automatic text colour can follow the selected palette.
 for(unsigned options=0;options<16;++options){
  JsonDocument endpoint;check(!deserializeJson(endpoint,edit));auto item=endpoint["item"].as<JsonObject>();
  unsigned fields=5;
  if(options&1){item["mode"]="countup";++fields;}
  if(options&2){item["category"]="birthday";++fields;}
  if(options&4){item["style"]="heart";++fields;}
  if(options&8){item["textColor"]="#aBc123";++fields;}
  check(mutateDocument(endpoint));check(GiftData::begin());check(items()[0].size()==fields);
  check((options&1)?items()[0]["mode"]=="countup":items()[0]["mode"].isNull());
  check((options&2)?items()[0]["category"]=="birthday":items()[0]["category"].isNull());
  check((options&4)?items()[0]["style"]=="heart":items()[0]["style"].isNull());
  check((options&8)?items()[0]["textColor"]=="#aBc123":items()[0]["textColor"].isNull());
  check(items()[0]["name"]=="一起旅行"&&items()[0]["date"]=="2027-01-01"&&items()[0]["yearly"]==false);
 }
 invalidTextColors();
 for(const char *color:{"#000000","#FFFFFF","#abcdef","#ABCDEF","#aBc123"}){
  JsonDocument direct;direct["countdownDays"]["items"].set(items());
  direct["countdownDays"]["items"][0]["textColor"]=color;check(patch(direct));check(GiftData::begin());
  check(items()[0]["textColor"]==color&&items()[0]["mode"]=="countup"&&items()[0]["category"]=="birthday"&&items()[0]["style"]=="heart");
 }
 const auto before=file();const uint32_t revision=GiftData::revision();
 JsonDocument endpoint;endpoint["action"]="upsert";endpoint["item"].set(items()[0]);
 endpoint["item"]["textColor"]="#FFFFFF";
 LittleFS.failWrites=true;check(!mutateDocument(endpoint));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[0]["textColor"]=="#aBc123");
 LittleFS.writeLimit=40;check(!mutateDocument(endpoint));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[0]["textColor"]=="#aBc123");
 LittleFS.failRenameFrom="/gift.tmp";check(!mutateDocument(endpoint));LittleFS.resetFaults();
 check(file()==before&&GiftData::revision()==revision&&items()[0]["textColor"]=="#aBc123");
 check(GiftData::begin());check(items()[0]["textColor"]=="#aBc123");
 check(mutateDocument(endpoint));check(GiftData::begin());check(items()[0]["textColor"]=="#FFFFFF");
 endpoint["item"].remove("textColor");const auto custom=file();
 LittleFS.failWrites=true;check(!mutateDocument(endpoint));LittleFS.resetFaults();
 check(file()==custom&&items()[0]["textColor"]=="#FFFFFF");
 check(mutateDocument(endpoint));check(GiftData::begin());
 check(items()[0].size()==8&&items()[0]["textColor"].isNull()&&items()[0]["style"]=="heart"&&items()[0]["category"]=="birthday");
 endpoint["item"]["textColor"]="#123aBc";check(mutateDocument(endpoint));
 JsonDocument direct;direct["countdownDays"]["items"].set(items());
 direct["countdownDays"]["items"][0].as<JsonObject>().remove("textColor");
 check(patch(direct));check(GiftData::begin());check(items()[0]["textColor"].isNull());
}
int main(int argc,char **argv){
 check(argc>=2);LittleFS.directory=argv[1];check(LittleFS.begin(false,"",10,""));
 const std::string mode=argc>2?argv[2]:"test";
 if(mode=="seed"){
  check(GiftData::begin());check(mutate(add));check(mutate(addCountup));check(mutate(addExplicitCountdown));
  check(mutate(addBirthday));check(mutate(addLove));
  for(unsigned i=0;i<5;++i){JsonDocument request;styledRequest(request,0,styles[i],i%4,i%4);check(mutateDocument(request));}
  JsonDocument request;styledRequest(request,0,"heart",3,2);request["item"]["textColor"]="#aBc123";check(mutateDocument(request));
  std::cout<<"seed "<<checks<<" checks\n";return 0;
 }
 if(mode=="restore"){
  check(GiftData::begin());check(items().size()==11&&items()[0]["name"]=="依依生日"&&items()[0]["yearly"]==true&&!items()[0].containsKey("mode")&&items()[0]["category"].isNull());
  check(items()[1]["name"]=="在一起的日子"&&items()[1]["date"]=="2024-02-29"&&items()[1]["yearly"]==false&&items()[1]["mode"]=="countup");
  check(items()[2]["name"]=="新的期待"&&items()[2]["mode"]=="countdown");
  check(items()[3]["category"]=="birthday"&&items()[3]["mode"].isNull()&&items()[3]["name"]=="小太阳的生日"&&items()[3]["date"]=="2001-10-03");
  check(items()[4]["category"]=="love"&&items()[4]["mode"]=="countup"&&items()[4]["name"]=="我们的第一天"&&items()[4]["date"]=="2023-09-20");
  for(unsigned i=0;i<5;++i)check(items()[i]["style"].isNull());
  for(unsigned i=0;i<5;++i){
   auto item=items()[i+5];const unsigned options=i%4;
   check(item["style"]==styles[i]&&item["color"]==(int)options&&item["name"]=="一起旅行"&&item["date"]=="2027-01-01");
   check((options&1)?item["mode"]=="countup":item["mode"].isNull());
   check((options&2)?item["category"]=="love":item["category"].isNull());
  }
  for(unsigned i=0;i<10;++i)check(items()[i]["textColor"].isNull());
  check(items()[10].size()==9&&items()[10]["textColor"]=="#aBc123"&&items()[10]["style"]=="heart"&&items()[10]["category"]=="love"&&items()[10]["mode"]=="countup"&&items()[10]["name"]=="一起旅行"&&items()[10]["date"]=="2027-01-01");
  std::cout<<"restore "<<checks<<" checks\n";return 0;
 }
 check(GiftData::begin());dates();transactions();countupTransactions();categoryTransactions();styleTransactions();textColorTransactions();std::cout<<"PASS: "<<checks<<" countdown/countup/category/style/textColor backend checks\n";
}
