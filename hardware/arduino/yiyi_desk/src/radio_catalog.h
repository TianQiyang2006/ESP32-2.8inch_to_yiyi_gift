#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace RadioCatalog {
constexpr unsigned MaxStations=20,MaxName=24,MaxTag=24,MaxUrl=512;
struct Entry { String id,name,tag,url; };
// Call at startup, after LittleFS has been mounted. Safe to call repeatedly.
bool begin();
unsigned count();
uint32_t revision();
// Every caller owns the returned strings; a concurrent edit cannot invalidate them.
Entry get(unsigned index);
void snapshot(JsonObject out);
String storageError();
// {action:"upsert",station:{id:""|existing,name,tag,url},revision:N}
// {action:"remove",id:existing,revision:N}; rejects stale revisions.
// Optional per-call HTTP status: 200, 400 validation, 409 stale, 500 storage.
bool mutate(JsonVariantConst request,String &error,unsigned *httpStatus=nullptr);
}
