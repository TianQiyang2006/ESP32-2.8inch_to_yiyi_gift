#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
namespace XiaozhiVoice {
enum class Stage { Idle, Connecting, Activation, Listening, Thinking, Speaking, Error };
struct Snapshot { Stage stage=Stage::Idle;String message,code,heard,answer;bool connected=false,continuous=false,sleeping=false;int meter=0; };
void begin();
void poll();
// Queue a switch to the independently built official application.
void launch();
void talk();
void stopListening();
void close();
Snapshot get();
bool busy();
void settings(JsonDocument &out);
// Connection diagnostics only; never includes tokens, URLs, or conversation text.
void diagnostics(JsonDocument &out);
bool configure(JsonVariantConst input,String &error);
}
