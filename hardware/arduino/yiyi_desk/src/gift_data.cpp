#include "gift_data.h"
#include "app_state.h"
#include "display.h"
#include "audio_board.h"
#include "reading_cache.h"
#include "clock_source.h"
#include "gift_assets/gift_glyph_coverage.h"
#include "pages/days_logic.h"
#include "../desk_logic.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

namespace GiftData {
namespace {
JsonDocument data;
bool mounted=false,dirty=false,loaded=false;
uint32_t dirtyAt=0,rev=0;
// N1: failed-save retry backoff. 1200 ms after a change; doubles on every failed
// persist (1200/2400/.../60000 cap) so a persistent storage fault cannot rewrite
// the whole store every 1.2 s. A new changed() or a successful save resets it.
uint32_t saveRetryMs=1200;
String error_;
void changed(){dirty=true;dirtyAt=millis();saveRetryMs=1200;++rev;}
bool validSaved(JsonVariantConst saved);
// Returns true when the file is a valid store. `sanitized` (optional) reports
// whether the disposable readingCache had to be repaired; persist() ignores it.
bool validFile(const char *path, bool *sanitized=nullptr) {
 File file=LittleFS.open(path,"r");if(!file||!file.size()||file.size()>65536)return false;
 JsonDocument saved;if(deserializeJson(saved,file))return false;
 const bool sanitizedNow=ReadingCache::sanitize(saved.as<JsonObject>());
 if(sanitized)*sanitized=sanitizedNow;
 return validSaved(saved.as<JsonVariantConst>());
}
bool persist(const JsonDocument &value){
 if(!mounted){error_="内部存储不可用，设置尚未保存";return false;}
 File file=LittleFS.open("/gift.tmp","w");
 if(!file || serializeJson(value,file)!=measureJson(value)){if(file)file.close();error_="保存失败，原记录保留";return false;}
 file.flush();file.close();
 if(!validFile("/gift.tmp")){LittleFS.remove("/gift.tmp");error_="配置写入校验失败，原记录保留";return false;}
 const bool existing=LittleFS.exists("/gift.json");
 const bool validMain=existing&&validFile("/gift.json");
 if(existing){
  const bool prepared=validMain
   ? ((!LittleFS.exists("/gift.bak")||LittleFS.remove("/gift.bak"))&&LittleFS.rename("/gift.json","/gift.bak"))
   : LittleFS.remove("/gift.json");
  if(!prepared){LittleFS.remove("/gift.tmp");error_="无法备份原配置";return false;}
 }
 if(!LittleFS.rename("/gift.tmp","/gift.json")){
  if(validMain)LittleFS.rename("/gift.bak","/gift.json");
  LittleFS.remove("/gift.tmp");error_="无法提交配置，恢复副本已保留";return false;
 }
 error_="";return true;
}
bool str(JsonVariantConst v,int max){if(!v.is<const char*>())return false;int length=DeskLogic::utf8Length(v.as<const char*>());return length>=0&&length<=max;}
bool integer(JsonVariantConst v,int low,int high){return v.is<int>()&&v.as<int>()>=low&&v.as<int>()<=high;}
bool color(JsonVariantConst v){if(!str(v,7))return false;const char *s=v.as<const char*>();if(strlen(s)!=7||s[0]!='#')return false;for(int i=1;i<7;i++)if(!isxdigit(s[i]))return false;return true;}
// Every visible codepoint of user text must exist in at least one shipped F-d
// font (gift_glyph_coverage is generated from all font cmaps). Layout whitespace
// is skipped because it is not drawn as a glyph. This mirrors the parser in
// gift_glyph_coverage.c so a 0x20-0x7E-only check never rejects the newline.
bool glyphsCovered(const char *utf8){
 if(!utf8)return true;
 for(const unsigned char *p=(const unsigned char*)utf8;*p;){
  uint32_t cp;const unsigned char c=*p++;
  if(c<0x80)cp=c;
  else if((c&0xE0)==0xC0){if((*p&0xC0)!=0x80)return false;cp=((uint32_t)(c&0x1F)<<6)|(*p++&0x3F);}
  else if((c&0xF0)==0xE0){if((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80)return false;cp=((uint32_t)(c&0x0F)<<12)|((uint32_t)(p[0]&0x3F)<<6)|(p[1]&0x3F);p+=2;}
  else if((c&0xF8)==0xF0){if((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80||(p[2]&0xC0)!=0x80)return false;cp=((uint32_t)(c&0x07)<<18)|((uint32_t)(p[0]&0x3F)<<12)|((uint32_t)(p[1]&0x3F)<<6)|(p[2]&0x3F);p+=3;}
  else return false;
  if(cp=='\n'||cp=='\r'||cp=='\t')continue;
  if(!GiftCoverage::has(cp))return false;
 }
 return true;
}
// Fields that are rendered as free text (everything else is dates/colours/enums).
bool glyphField(const char *key,const char *name){
 if(!strcmp(key,"letter")||!strcmp(key,"keepsake"))return !strcmp(name,"text")||!strcmp(name,"title")||!strcmp(name,"signature");
 if(!strcmp(key,"reading"))return !strcmp(name,"quoteText")||!strcmp(name,"poemText")||!strcmp(name,"quoteAuthor")||!strcmp(name,"poemTitle")||!strcmp(name,"poemAuthor")||!strcmp(name,"poemDynasty");
 if(!strcmp(key,"countdown"))return !strcmp(name,"name");
 if(!strcmp(key,"mood"))return !strcmp(name,"note");
 if(!strcmp(key,"holiday"))return !strcmp(name,"label");
 return false;
}
// A8: strict zero-padded date/time. "2026-1-1" is rejected: only zero-padded
// YYYY-MM-DD (10 chars) and YYYY-MM-DDTHH:MM (16 chars) are accepted here;
// callers that allow an empty value check strlen()==0 before calling.
bool date(const char *s,bool time){
 if(!s)return false;
 const size_t length=strlen(s);
 if(length!=(time?16u:10u))return false;
 for(size_t i=0;i<length;++i){
  if(i==4||i==7||(time&&(i==10||i==13))){
   const char expected=(i==4||i==7)?'-':(i==10?'T':':');
   if(s[i]!=expected)return false;
  }else if(s[i]<'0'||s[i]>'9')return false;
 }
 int y,m,d,h=0,min=0,n=0;int fields=time?sscanf(s,"%d-%d-%dT%d:%d%n",&y,&m,&d,&h,&min,&n):sscanf(s,"%d-%d-%d%n",&y,&m,&d,&n);
 return fields==(time?5:3)&&n==(int)length&&DeskLogic::validDate(y,m,d,h,min);
}
bool validate(const char *key,JsonVariantConst object,String &error,bool checkGlyphs=true){
 if(!strcmp(key,"countdown")&&object.isNull())return true;
 if(!object.is<JsonObjectConst>()){error="设置格式错误";return false;}
 if(!strcmp(key,"countdownDays")){
  if(object.size()!=1||!object["items"].is<JsonArrayConst>()||object["items"].size()>DaysLogic::MaxItems){error="倒数日列表格式错误，最多20条";return false;}
  int ids[DaysLogic::MaxItems]{};unsigned count=0;
  for(JsonVariantConst item:object["items"].as<JsonArrayConst>()){
   DaysLogic::Date parsed;
   const bool hasMode=item.containsKey("mode");
   const bool hasCategory=item.containsKey("category");
   const bool hasStyle=item.containsKey("style");
   const bool hasTextColor=item.containsKey("textColor");
   if(!item.is<JsonObjectConst>()||item.size()!=5u+hasMode+hasCategory+hasStyle+hasTextColor||!integer(item["id"],1,INT32_MAX)||
      !str(item["name"],DaysLogic::MaxName)||item["name"].as<JsonString>().size()!=strlen(item["name"].as<const char*>())||
      !item["date"].is<const char*>()||item["date"].as<JsonString>().size()!=10||
      !DaysLogic::parseDate(item["date"].as<const char*>(),parsed)||!item["yearly"].is<bool>()||!integer(item["color"],0,3)){
    error="倒数日字段无效，请检查名称、日期和配色";return false;
   }
   if(hasMode&&(!item["mode"].is<const char*>()||
      item["mode"].as<JsonString>().size()!=strlen(item["mode"].as<const char*>())||
      (item["mode"]!="countdown"&&item["mode"]!="countup"))){
    error="日子类型无效，请选择倒数日或计时日";return false;
   }
   if(hasCategory&&(!item["category"].is<const char*>()||
      item["category"].as<JsonString>().size()!=strlen(item["category"].as<const char*>())||
      (item["category"]!="general"&&item["category"]!="birthday"&&item["category"]!="love"))){
    error="日子分类无效，请选择普通、生日或相恋";return false;
   }
   if(hasStyle&&(!item["style"].is<const char*>()||
      item["style"].as<JsonString>().size()!=strlen(item["style"].as<const char*>())||
      (item["style"]!="classic"&&item["style"]!="laurel"&&item["style"]!="ink"&&item["style"]!="heart"&&item["style"]!="dots"))){
    error="日子样式无效，请重新选择";return false;
   }
   if(hasTextColor){
    bool valid=item["textColor"].is<const char*>()&&item["textColor"].as<JsonString>().size()==7;
    const char *hex=valid?item["textColor"].as<const char*>():"";
    valid=valid&&strlen(hex)==7&&hex[0]=='#';
    for(unsigned i=1;valid&&i<7;++i)
     valid=(hex[i]>='0'&&hex[i]<='9')||(hex[i]>='A'&&hex[i]<='F')||(hex[i]>='a'&&hex[i]<='f');
    if(!valid){error="字体颜色无效，请使用 #RRGGBB 格式";return false;}
   }
   if(item["mode"]=="countup"&&item["yearly"].as<bool>()){
    error="计时日从开始日期累计，不支持每年重复";return false;
   }
   String name=item["name"].as<String>(),trimmed=name;trimmed.trim();
   if(name.isEmpty()||name!=trimmed){error="倒数日名称不能为空或含首尾空格";return false;}
   const int id=item["id"].as<int>();
   for(unsigned i=0;i<count;++i)if(ids[i]==id){error="倒数日编号重复";return false;}
   ids[count++]=id;
   if(checkGlyphs&&!glyphsCovered(name.c_str())){error="包含设备字库不支持的生僻字，请修改";return false;}
  }
  return true;
 }
 for(JsonPairConst field:object.as<JsonObjectConst>()){
  String name=field.key().c_str();auto v=field.value();bool ok=false;
  if(!strcmp(key,"deviceSettings"))ok=name=="brightness"?integer(v,10,100):name=="volume"?integer(v,0,100):name=="autoHome"&&v.is<bool>();
  else if(!strcmp(key,"power"))ok=(name=="batteryInstalled"||name=="externalPower")&&v.is<bool>();
  else if(!strcmp(key,"clockMode"))ok=(name=="seconds"||name=="flipDark"||name=="barbarSeconds")?v.is<bool>():name=="preset"&&str(v,12)&&(v=="landscape"||v=="matrix"||v=="flip"||v=="dashboard"||v=="minimal"||v=="compact"||v=="barbar");
  else if(!strcmp(key,"dashboardClock")){
   if(name=="color")ok=color(v);
   else if(name=="weight")ok=integer(v,100,900)&&v.as<int>()%100==0;
   else if(name=="opacity")ok=integer(v,10,100);
   else if(name=="spacing")ok=integer(v,-18,-8);
   else ok=(name=="seconds"||name=="hour24"||name=="tilt"||name=="lunar"||name=="festival"||name=="rain"||name=="snow"||name=="thunder")&&v.is<bool>();
  }
  else if(!strcmp(key,"letter")||!strcmp(key,"keepsake")){
   ok=name=="text"?str(v,3000):(name=="title"||name=="signature")?str(v,80):name=="date"&&str(v,10)&&(strlen(v.as<const char*>())==0||date(v.as<const char*>(),false));
  }else if(!strcmp(key,"daily"))ok=name=="waterGoal"?integer(v,200,10000):name=="sound"?v.is<bool>():(name=="background"||name=="cardColor")&&color(v);
  else if(!strcmp(key,"holiday"))ok=name=="weekday"?integer(v,0,6):name=="label"?str(v,4):color(v)&&(name=="background"||name=="cardColor"||name=="textColor"||name=="numberColor"||name=="unitColor");
  else if(!strcmp(key,"reading"))ok=name=="autoUpdate"?v.is<bool>():(name=="quoteMode"||name=="poemMode")?str(v,10)&&(v=="api"||v=="manual"):(name=="background"||name=="quoteColor"||name=="quoteInk"||name=="poemColor"||name=="poemInk")?color(v):(name=="quoteText"||name=="poemText")?str(v,600):(name=="quoteAuthor"||name=="poemTitle"||name=="poemAuthor"||name=="poemDynasty")&&str(v,80);
  else if(!strcmp(key,"colors"))ok=(name=="bg"||name=="header"||name=="ink"||name=="accent"||name=="muted"||name=="surface")&&color(v);
  else if(!strcmp(key,"countdown"))ok=name=="name"?str(v,40)&&strlen(v.as<const char*>())>0:name=="date"&&str(v,16)&&date(v.as<const char*>(),true);
  else if(!strcmp(key,"mood"))ok=name=="mood"?str(v,10)&&(v=="happy"||v=="calm"||v=="neutral"||v=="tired"||v=="anxious"||v=="sad"):name=="note"&&str(v,600);
  if(!ok){error="设置字段无效："+String(key)+"."+name;return false;}
  // Reject writes whose text would render as tofu on the F-d font set. Loading
  // existing data skips this check (validSaved passes false) so a firmware update
  // never marks previously stored content as corrupted.
  if(checkGlyphs&&glyphField(key,name.c_str())&&!glyphsCovered(v.as<const char*>())){error="包含设备字库不支持的生僻字，请修改";return false;}
 }
 return true;
}
void levels(){settings.brightness=((data["deviceSettings"]["brightness"].as<int>()*255)+50)/100;Display::setBrightness(settings.brightness);AudioBoard::setVolume(data["deviceSettings"]["volume"]|45);}
void countdown(){
 JsonVariantConst cd=data["countdown"];settings.hasTarget=!cd.isNull();
 if(settings.hasTarget){int y,m,d,h,min;sscanf(cd["date"].as<const char*>(),"%d-%d-%dT%d:%d",&y,&m,&d,&h,&min);settings.target=DeskLogic::beijingEpoch(y,m,d,h,min);settings.countdownName=cd["name"].as<String>();}
 else{settings.target=0;settings.countdownName="";}
}
JsonObject day(const String &key=today()){JsonObject records=data["dailyRecords"].as<JsonObject>();if(records[key].isNull()){auto d=records[key].to<JsonObject>();d["mood"]="";d["note"]="";d["knocks"]=0;d["water"].to<JsonArray>();}return records[key].as<JsonObject>();}
void prune(){auto records=data["dailyRecords"].as<JsonObject>();while(records.size()>31||(records.size()>1&&measureJson(data)>60000)){String earliest="9999-99-99";for(JsonPair item:records)if(String(item.key().c_str())<earliest)earliest=item.key().c_str();records.remove(earliest);}}
bool validSaved(JsonVariantConst saved){
 if(!saved.is<JsonObjectConst>())return false;String error;
 for(JsonPairConst item:saved.as<JsonObjectConst>()){
  const char *key=item.key().c_str();auto value=item.value();
  if(!strcmp(key,"dailyRecords")){
   if(!value.is<JsonObjectConst>()||value.size()>31)return false;
   for(JsonPairConst entry:value.as<JsonObjectConst>()){
    auto day=entry.value();if(!date(entry.key().c_str(),false)||!day.is<JsonObjectConst>()||!str(day["note"],600)||!str(day["mood"],10)||!integer(day["knocks"],0,999999)||!day["water"].is<JsonArrayConst>()||day["water"].size()>64)return false;
    String mood=day["mood"]|"";if(!mood.isEmpty()&&mood!="happy"&&mood!="calm"&&mood!="neutral"&&mood!="tired"&&mood!="anxious"&&mood!="sad")return false;
    for(JsonVariantConst water:day["water"].as<JsonArrayConst>())if(!integer(water["amount"],1,2000)||!water["at"].is<int64_t>())return false;
   }
  }else if(!strcmp(key,"readingCache")){
   if(!value.is<JsonObjectConst>())return false;
   for(JsonPairConst cache:value.as<JsonObjectConst>())if(!ReadingCache::valid(cache.key().c_str(),cache.value()))return false;
  }else if(!data.containsKey(key)||!validate(key,value,error,false))return false;
  if(!strcmp(key,"countdown")&&!value.isNull()&&(!value["name"].is<const char*>()||!value["date"].is<const char*>()))return false;
 }
 return true;
}
}
bool begin(){
 loaded=false;dirty=false;error_="";
 deserializeJson(data,R"json({"deviceSettings":{"brightness":100,"volume":45,"autoHome":true},"power":{"batteryInstalled":true,"externalPower":true},"clockMode":{"preset":"landscape","seconds":true,"flipDark":false,"barbarSeconds":true},"letter":{"title":"写给依依的小纸条","text":"把小小的快乐，装进每一个普通的日子。\n愿你抬头时，总有小黑陪着你。","signature":"给最特别的依依"},"keepsake":{"title":"留个小纪念","text":"","signature":"","date":""},"daily":{"waterGoal":1000,"sound":true,"background":"#a4ccac","cardColor":"#fff8dc"},"dailyRecords":{},"holiday":{"weekday":5,"label":"放假","background":"#f2f2f2","cardColor":"#ffffff","textColor":"#403c39","numberColor":"#a19894","unitColor":"#62483e"},"reading":{"autoUpdate":true,"quoteMode":"api","poemMode":"api","background":"#a4ccac","quoteColor":"#ffffff","quoteInk":"#292929","poemColor":"#b80e10","poemInk":"#ffffff","quoteText":"虽然辛苦，\n我还是会选择那种滚烫的人生。","quoteAuthor":"北野武","poemText":"人生得意须尽欢，\n莫使金樽空对月。","poemTitle":"将进酒","poemAuthor":"李白","poemDynasty":"唐"},"readingCache":{},"colors":{"bg":"#f4f7ef","header":"#deebd5","ink":"#244432","accent":"#619b78","muted":"#7c9275","surface":"#ffffff"},"countdown":null,"countdownDays":{"items":[]}})json");
 // Merge older stores into these defaults; the existing clock preset and all
 // unrelated fields retain their saved values. New clock options live in their
 // own object because the three earlier faces always display seconds.
 JsonDocument clockDefaults;
 deserializeJson(clockDefaults,R"json({"color":"#ffffff","weight":700,"opacity":90,"spacing":-18,"seconds":false,"hour24":true,"tilt":true,"lunar":true,"festival":true,"rain":true,"snow":true,"thunder":true})json");
 data["dashboardClock"].set(clockDefaults.as<JsonVariantConst>());
 Preferences meta;const bool metadata=meta.begin("gift-store",false);mounted=LittleFS.begin(false,"/littlefs",10,"spiffs");
 // This partition was unused by all earlier desk versions. Initialise only once;
 // never reformat a previously initialised or damaged store.
 if(!mounted&&metadata&&!meta.getBool("initialised",false)){mounted=LittleFS.format()&&LittleFS.begin(false,"/littlefs",10,"spiffs");}
 if(mounted){meta.putBool("initialised",true);bool cacheRepaired=false;for(const char *path:{"/gift.json","/gift.bak"}){File f=LittleFS.open(path,"r");if(!f||f.size()>65536)continue;JsonDocument saved;if(deserializeJson(saved,f))continue;const bool repaired=ReadingCache::sanitize(saved.as<JsonObject>());if(!validSaved(saved.as<JsonVariantConst>()))continue;for(JsonPair item:saved.as<JsonObject>()){if(!strcmp(item.key().c_str(),"dailyRecords")||!strcmp(item.key().c_str(),"readingCache")||!strcmp(item.key().c_str(),"countdown"))data[item.key()]=item.value();else for(JsonPair field:item.value().as<JsonObject>())data[item.key()][field.key()]=field.value();}loaded=true;cacheRepaired=repaired;break;}
  // A7: a repaired readingCache is written back through the normal delayed path
  // (changed() only marks dirty; poll() persists it after boot, no blocking I/O).
  if(cacheRepaired)changed();}
 else error_="内部存储不可用，原数据分区未重置";
 meta.end();
 if(!loaded){
  if(settings.brightness>0)data["deviceSettings"]["brightness"]=constrain(((int)settings.brightness*100+127)/255,10,100);
  if(settings.hasTarget){auto d=DeskLogic::beijingDate(settings.target);char value[17];snprintf(value,sizeof(value),"%04d-%02d-%02dT%02d:%02d",d.year,d.month,d.day,d.hour,d.minute);if(date(value,true)){data["countdown"]["name"]=settings.countdownName.isEmpty()?"期待的日子":settings.countdownName;data["countdown"]["date"]=value;}}
  if(mounted&&(LittleFS.exists("/gift.json")||LittleFS.exists("/gift.bak")))error_="配置损坏，已保留原文件，请导出后检查";
 }
 // The original GIF, pixel and flip clocks always show seconds. Barbar has
 // its own persisted visibility preference; dashboard clocks use dashboardClock.
 data["clockMode"]["seconds"]=true;
 prune();levels();countdown();return mounted;
}
bool hasSavedConfig(){return loaded||(mounted&&(LittleFS.exists("/gift.json")||LittleFS.exists("/gift.bak")));}
void poll(){
 if(!dirty)return;
 const uint32_t now=millis();
 // Wrap-safe deadline: a failed retry pushes dirtyAt forward and grows the delay.
 if((int32_t)(now-(dirtyAt+saveRetryMs))<0)return;
 prune();
 if(persist(data)){dirty=false;saveRetryMs=1200;return;}
 dirtyAt=now;
 saveRetryMs=saveRetryMs<30000u?saveRetryMs*2u:60000u;
}
JsonVariantConst get(const char *key){return data[key];}
void snapshot(JsonDocument &out){out.set(data);out["revision"]=rev;out["storageError"]=error_;out["today"]=today();}
bool patch(JsonVariantConst value,String &error){
 if(!value.is<JsonObjectConst>()){error="请提供设置对象";return false;}
 JsonDocument next;next.set(data);
 for(JsonPairConst item:value.as<JsonObjectConst>()){
  const char *key=item.key().c_str();
  if(!data.containsKey(key)||!strcmp(key,"dailyRecords")||!strcmp(key,"readingCache")){error="不支持的设置字段";return false;}
  if(!validate(key,item.value(),error))return false;
  if(!strcmp(key,"countdown")&&item.value().isNull()){next[key]=nullptr;continue;}
  for(JsonPairConst field:item.value().as<JsonObjectConst>())next[key][field.key()]=field.value();
 }
 if(!next["countdown"].isNull()&&(!next["countdown"]["date"].is<const char*>()||!next["countdown"]["name"].is<const char*>())){error="倒计时需要名称和完整日期";return false;}
 next["clockMode"]["seconds"]=true;
 if(measureJson(next)>60000){error="记录空间已满，请先导出记录";return false;}
 if(!persist(next)){error=error_;return false;}
 data=std::move(next);dirty=false;++rev;levels();
 countdown();
 return true;
}
void setLevel(const char *key,int percent){if(strcmp(key,"brightness")&&strcmp(key,"volume"))return;data["deviceSettings"][key]=constrain(percent,!strcmp(key,"brightness")?10:0,100);levels();changed();}
String today(){int64_t epoch=ClockSource::now();if(!DeskLogic::timeValid(epoch))return "";auto t=DeskLogic::beijingDate(epoch);char buf[11];snprintf(buf,sizeof(buf),"%04d-%02d-%02d",t.year,t.month,t.day);return buf;}
String storageError(){return error_;}
JsonVariantConst record(const String &date){return data["dailyRecords"][date];}
bool saveMood(const String &mood,const String &note,String &error){
 return saveMoodAt(today(),mood,note,error);
}
bool saveMoodAt(const String &date_,const String &mood,const String &note,String &error){
 if(today().isEmpty()){error="请先联网校时";return false;}
 auto earliest=DeskLogic::beijingDate(ClockSource::now()-30*86400);char minimum[11];snprintf(minimum,sizeof(minimum),"%04d-%02d-%02d",earliest.year,earliest.month,earliest.day);
 if(!date(date_.c_str(),false)||date_>today()||date_<String(minimum)){error="请选择今天或最近 31 天的有效日期";return false;}
 JsonDocument v;v["mood"]=mood;v["note"]=note;if(!validate("mood",v.as<JsonVariantConst>(),error))return false;
 JsonDocument previous;previous.set(data);auto d=day(date_);d["mood"]=mood;d["note"]=note;prune();
 if(!persist(data)){data=std::move(previous);error=error_;return false;}dirty=false;++rev;return true;
}
void addWater(int amount){if(today().isEmpty()||amount<1||amount>2000)return;auto water=day()["water"].as<JsonArray>();if(water.size()>=64)return;auto item=water.add<JsonObject>();item["amount"]=amount;item["at"]=ClockSource::now()*1000;prune();changed();}
void undoWater(){if(today().isEmpty())return;auto water=day()["water"].as<JsonArray>();if(water.size())water.remove(water.size()-1);changed();}
void strike(){if(today().isEmpty())return;auto d=day();d["knocks"]=std::min(999999,(d["knocks"]|0)+1);prune();changed();}
void undoStrike(){if(today().isEmpty())return;auto d=day();d["knocks"]=std::max(0,(d["knocks"]|0)-1);changed();}
void resetKnocks(){if(today().isEmpty())return;day()["knocks"]=0;changed();}
uint32_t revision(){return rev;}
void readingCache(const char *kind,JsonVariantConst value){
 if(!ReadingCache::valid(kind,value))return;
 // net.cpp filters network text before this point; checking again keeps every
 // write entry consistent (a future caller cannot introduce tofu through here).
 if(!glyphsCovered(value["text"]|"")||!glyphsCovered(value["author"]|"")||!glyphsCovered(value["title"]|"")||!glyphsCovered(value["dynasty"]|""))return;
 data["readingCache"][kind]=value;prune();changed();}
}
