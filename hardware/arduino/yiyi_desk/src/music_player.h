#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace MusicPlayer {
struct Track { String id,title,artist; };
struct Playlist { String name; uint16_t tracks[200]{}; unsigned count=0; };
struct State {
 bool ready=false,playing=false,paused=false,loading=false,buffering=false,remembered=false,saveFailed=false,lyrics=false;
 unsigned position=0,duration=0,buffered=0,underruns=0,volume=40,mode=0;
 int track=-1,playlist=0,lyricIndex=-1; String error;
};
void begin();
void open();
void stop();
void play(unsigned track);
void selectPlaylist(unsigned index);
void next(int direction=1);
void toggle();
void seek(unsigned milliseconds);
void setVolume(unsigned value);
void cycleMode();
void setMode(unsigned value);
State state();
unsigned trackCount();
unsigned playlistCount();
const Track &track(unsigned index);
const Playlist &playlist(unsigned index);
String lyric(int relative=0);
void diagnostics(JsonObject out);
}
