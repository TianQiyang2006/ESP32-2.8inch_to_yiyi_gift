#pragma once
#include <stdint.h>
#include <string.h>

namespace TimerEngine {
constexpr unsigned PresetCount = 8;
constexpr uint32_t MaxSeconds = 23 * 3600 + 59 * 60 + 59;
enum class Stage : uint8_t { Ready, Running, Paused, Finished };
struct Preset {
    char name[25]{};
    uint32_t seconds = 0;
    Preset(const char *label = "", uint32_t duration = 0) : seconds(duration) {
        strncpy(name, label, sizeof(name) - 1);
    }
};
struct Settings {
    uint32_t seconds = 15 * 60;
    uint8_t tone = 1, volume = 20;
    Preset presets[PresetCount] = {{"刷牙", 120}, {"蒸蛋", 600},
                                 {"面膜", 900}, {"午睡", 1800}};
};
inline bool validSeconds(uint32_t seconds) { return seconds && seconds <= MaxSeconds; }
inline bool valid(const Settings &s) {
    if (!validSeconds(s.seconds) || s.tone >= 3 || s.volume > 100) return false;
    for (auto &p : s.presets)
        if (!memchr(p.name, 0, sizeof(p.name)) ||
            (p.seconds && (!validSeconds(p.seconds) || !p.name[0]))) return false;
    return true;
}
class Engine {
public:
    Settings settings;
    Stage stage = Stage::Ready;
    uint64_t deadline = 0, left = 0, total = 0;
    uint32_t revision = 0;
    uint8_t tone = 1, volume = 20;
    char sessionName[25] = "自定义";
    uint64_t remaining(uint64_t now) const {
        return stage == Stage::Running ? (deadline > now ? deadline - now : 0) : left;
    }
    bool start(uint64_t now, const char *name = nullptr) {
        if (stage == Stage::Running || !valid(settings)) return false;
        if (stage != Stage::Paused) {
            total = left = uint64_t(settings.seconds) * 1000;
            tone = settings.tone;
            volume = settings.volume;
            strncpy(sessionName, name && *name ? name : "自定义", sizeof(sessionName) - 1);
            sessionName[sizeof(sessionName) - 1] = 0;
        }
        deadline = now + left;
        stage = Stage::Running;
        ++revision;
        return true;
    }
    bool replay(uint64_t now) {
        if (stage != Stage::Finished || !validSeconds(uint32_t(total / 1000))) return false;
        left = total;
        deadline = now + left;
        stage = Stage::Running;
        ++revision;
        return true;
    }
    bool pause(uint64_t now) {
        if (stage != Stage::Running) return false;
        tick(now);
        if (stage != Stage::Running) return false;
        left = remaining(now);
        stage = Stage::Paused;
        ++revision;
        return true;
    }
    void reset() { stage = Stage::Ready; deadline = left = total = 0; ++revision; }
    void tick(uint64_t now) {
        if (stage == Stage::Running && now >= deadline) {
            stage = Stage::Finished;
            left = 0;
            ++revision;
        }
    }
};
} // namespace TimerEngine
