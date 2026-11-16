#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace GiftData {
bool begin();
bool hasSavedConfig();
void poll();
JsonVariantConst get(const char *key);
void snapshot(JsonDocument &out);
bool patch(JsonVariantConst value,String &error);
void setLevel(const char *key,int percent);
String today();
String storageError();
JsonVariantConst record(const String &date);
bool saveMood(const String &mood,const String &note,String &error);
bool saveMoodAt(const String &date,const String &mood,const String &note,String &error);
void addWater(int amount);
void undoWater();
void strike();
void undoStrike();
void resetKnocks();
uint32_t revision();
void readingCache(const char *kind,JsonVariantConst value);
}
