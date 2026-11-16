#pragma once
#include <Arduino.h>

// Desktop-only local feedback. Voice capture/playback lives in the Xiaozhi app.
namespace AudioBoard {
enum class Reminder { Alarm, Pomodoro, Timer };
bool begin();
void setVolume(int percent);
void knock();
bool playing();
bool ready();
String error();
// Music can use 32 kHz; reminder tones retain their original 16 kHz clock.
bool streamPcm(const int16_t *samples,unsigned count,unsigned percent,unsigned rate=16000);
void endStream();
// One media source owns I2S at a time. Late work from a stopped source cannot
// write samples or mute the source that replaced it; reminders retain priority.
enum class StreamSource { Music, Radio };
void selectStream(StreamSource source);
bool streamSelected(StreamSource source);
// Serializes user media handoff against background music EOF/retry decisions.
// This is separate from the PCM/output mutex; stop() must not acquire it again.
bool lockMediaControl();
void unlockMediaControl();
bool streamRadioPcm(const int16_t *samples,unsigned count,unsigned percent,unsigned rate=32000);
void endRadioStream();
// Priority: preview=1, pomodoro/timer=2, alarm=3. Higher priority preempts lower.
bool ringtone(unsigned tone,unsigned percent,uint32_t durationMs,unsigned priority,unsigned fadeMs=0,Reminder source=Reminder::Alarm);
// Alarm/Pomodoro use saved masters; Timer uses its independent request volume.
void setReminderVolume(Reminder source,unsigned percent);
void stopRingtone(unsigned priority);
// Only stop this app's sound when two reminders share a priority.
void stopRingtone(unsigned priority,Reminder source);
void protect(bool soundAllowed,unsigned volumeCap=100);
}
