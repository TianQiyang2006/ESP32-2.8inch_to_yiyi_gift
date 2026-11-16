#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "radio_catalog.h"

namespace RadioPlayer {
using Station = RadioCatalog::Entry;
struct State {
 bool ready=false,playing=false,connecting=false,buffering=false,favorite=false,saveFailed=false,wifi=false;
 unsigned volume=35,buffered=0,retries=0;
 int station=-1;
 String title,error;
};
void begin();
void open();
void stop();
void play(unsigned index);
void toggle();
void next(int direction=1);
void setVolume(unsigned percent);
void toggleFavorite();
State state();
unsigned stationCount();
Station station(unsigned index);
uint32_t stationRevision();
// Reconcile stable selection/favorite IDs after a committed phone edit.
void catalogChanged();
bool isFavorite(unsigned index);
void diagnostics(JsonObject out);
// Persistence is coalesced here; call from the desktop's normal update loop.
void tick();
}
