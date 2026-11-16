#include "net.h"
#include "../desk_logic.h"
#include "gift_ca.h"
#include "text_util.h"
#include "weather_api.h"
#include "bounded_stream.h"
#include "reading_cache.h"
#include "gift_assets/gift_glyph_coverage.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <time.h>

namespace Net {
namespace {

using namespace Jobs;

// The same threshold the rest of the firmware uses: a clock that has not been set
// by NTP would produce a wrong "updated at" and a wrong forecast day.
bool clockReady() { return DeskLogic::timeValid((int64_t)time(nullptr)); }

constexpr int MaxBodyBytes = 32768;

// Cloud text is only accepted when every visible codepoint exists in a shipped
// F-d font; otherwise it would render as tofu boxes. Newlines/tabs are layout,
// not glyphs, so they are skipped (gift_coverage_text_supported is 0x20-0x7E only).
bool glyphsCovered(const char *utf8) {
    if (!utf8) return true;
    for (const unsigned char *p=(const unsigned char*)utf8; *p;) {
        uint32_t cp; const unsigned char c=*p++;
        if (c<0x80) cp=c;
        else if ((c&0xE0)==0xC0) { if ((*p&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x1F)<<6)|(*p++&0x3F); }
        else if ((c&0xF0)==0xE0) { if ((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x0F)<<12)|((uint32_t)(p[0]&0x3F)<<6)|(p[1]&0x3F); p+=2; }
        else if ((c&0xF8)==0xF0) { if ((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80||(p[2]&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x07)<<18)|((uint32_t)(p[0]&0x3F)<<12)|((uint32_t)(p[1]&0x3F)<<6)|(p[2]&0x3F); p+=3; }
        else return false;
        if (cp=='\n'||cp=='\r'||cp=='\t') continue;
        if (!GiftCoverage::has(cp)) return false;
    }
    return true;
}

}  // namespace

bool getJson(const String &url,JsonDocument &json,String &error,int *httpStatus) {
    if (httpStatus) *httpStatus=0;
    if (WiFi.status()!=WL_CONNECTED) { error="Wi-Fi 未连接"; return false; }
    if (!clockReady()) { error="等待联网校时"; return false; }
    // TLS handshakes can exceed five seconds on the board even with good RSSI.
    // Networking runs in ioWorker, so this allowance does not block LVGL input.
    // A10: reuse one TLS client for every request. getJson() is only called from
    // the single ioWorker task (serialised), so a function-local static is safe;
    // the CA bundle and handshake timeout are configured once. stop() first so a
    // socket left behind by a failed handshake can never be reused.
    static WiFiClientSecure client;
    static bool caSet=false;
    if(!caSet){client.setCACert(GiftRootCa);client.setHandshakeTimeout(15);caSet=true;}
    client.stop();
    HTTPClient http; http.setConnectTimeout(4000); http.setTimeout(5000);
    http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // Release the TCP+TLS context in end(): with a reused static client, HTTP/1.1
    // keep-alive would otherwise pin ~30 KB of internal heap between fetches.
    http.setReuse(false);
    if (!http.begin(client,url)) { error="无法建立接口连接"; return false; }
    int status=http.GET();
    if (httpStatus) *httpStatus=status;
    if (status!=200) {
        // Keep error pages out of memory; status is enough to diagnose the API.
        error=String("接口服务暂不可用 (")+String(status)+")";
        http.end(); return false;
    }
    if (http.getSize()>MaxBodyBytes) { error="接口响应过大"; http.end(); return false; }
    BoundedStringStream body(MaxBodyBytes);
    const int received=http.writeToStream(&body);
    http.end();
    if (body.exceeded()) { error="接口响应过大"; return false; }
    if (received<0 || body.failed()) { error="接口响应读取失败"; return false; }
    if (deserializeJson(json,body.body())) { error="接口返回格式错误"; return false; }
    return true;
}

void fetchWeather(const Job &job,Result &r) {
    r.weather.query=job.text; r.generation=job.generation;
    JsonDocument json; int status=0;
    if (!getJson(WeatherApi::url(job.text),json,r.error,&status)) {
        if(status==404)r.error="未找到城市，请检查中文城市名称";
        if(status==429)r.error="天气更新较频繁，请稍后重试";
        return;
    }
    if(!WeatherApi::parse(json.as<JsonVariantConst>(),r.weather,r.error,TextUtil::formatDate(time(nullptr),"%Y-%m-%d")))return;
    // The cloud may label the condition/wind/AQI with a character the fonts do not
    // carry; clear only that optional label so the UI falls back to its own text.
    if(!glyphsCovered(r.weather.condition.c_str()))r.weather.condition="";
    if(!glyphsCovered(r.weather.windText.c_str()))r.weather.windText="";
    if(!glyphsCovered(r.weather.aqiCategory.c_str()))r.weather.aqiCategory="";
    r.weather.fetchedAt=time(nullptr); r.success=true;
}

void fetchReading(const Job &job,Result &r){
 JsonDocument options;deserializeJson(options,job.text);JsonDocument caches;
 for(const char *kind:{"quote","poem"}){
  if(!(options[kind]|false))continue;
  JsonDocument json;String error;
  const char *url=!strcmp(kind,"quote")?"https://uapis.cn/api/v1/saying":"https://poetry.palemoky.com/api/poems/random?lang=zh-Hans";
  if(!getJson(url,json,error)){r.error+=String(kind)+"："+error+" ";continue;}
  String text,author,title,dynasty;
  if(!strcmp(kind,"quote")){if(!json["text"].is<const char*>()){r.error+=String("一言接口返回格式错误")+"；";continue;}text=json["text"].as<String>();}
  else{if(!json["data"]["content"].is<JsonArray>()){r.error+=String("诗句接口返回格式错误")+"；";continue;}for(JsonVariant line:json["data"]["content"].as<JsonArray>())if(line.is<const char*>()){if(!text.isEmpty())text+='\n';text+=line.as<String>();}author=json["data"]["author"]["name"]|"";title=json["data"]["title"]|"";dynasty=json["data"]["dynasty"]["name"]|"";}
  text.trim();
  // Reject the whole item instead of truncating: the old cache/example stays on
  // screen (readingSources shows 示例/缓存) and the reason is returned in r.error.
  if(!glyphsCovered(text.c_str())||!glyphsCovered(author.c_str())||!glyphsCovered(title.c_str())||!glyphsCovered(dynasty.c_str())){
   const char *label=!strcmp(kind,"quote")?"名言":"诗词";
   caches.remove(kind);
   r.error+=String(label)+"：含设备字库不支持的生僻字，已保留旧内容；";
   continue;
  }
  auto cache=caches[kind].to<JsonObject>();cache["text"]=text;cache["author"]=author;cache["title"]=title;cache["dynasty"]=dynasty;cache["date"]=options["date"];cache["source"]="api";cache["provider"]=!strcmp(kind,"quote")?"uapis":"palemoky";cache["updatedAt"]=(int64_t)time(nullptr)*1000;
  if(!ReadingCache::valid(kind,cache)){caches.remove(kind);r.error="阅读内容为空、过长或格式错误";continue;}
 }
 r.success=caches.size()>0;serializeJson(caches,r.text);
}

}
