#pragma once

// Public streams checked on 2026-11-14; see stations-verification.json.
// Persistent favorites/recent selection use hashes of the stable station IDs.
namespace RadioPlayer {
struct DefaultStation { const char *id,*name,*tag,*url; };
static const DefaultStation RadioStations[] = {
    {"beijing_music", "北京音乐广播", "音乐 · FM97.4", "http://lhttp.qingting.fm/live/332/64k.mp3"},
    {"shanghai_pop", "动感101", "流行 · FM101.7", "http://lhttp.qingting.fm/live/274/64k.mp3"},
    {"shanghai_classic", "经典947", "古典 · FM94.7", "http://lhttp.qingting.fm/live/267/64k.mp3"},
    {"guangdong_music", "广东音乐之声", "音乐 · FM99.3", "http://lhttp.qtfm.cn/live/1260/64k.mp3"},
    {"cnr_voice", "中国之声", "新闻 · 综合", "http://lhttp.qtfm.cn/live/15318317/64k.mp3"},
    {"beijing_news", "北京新闻广播", "新闻 · FM94.5", "http://lhttp.qtfm.cn/live/339/64k.mp3"},
};
}
