#pragma once
#include <ArduinoJson.h>
#include "../desk_logic.h"
#include <string.h>
#include <stdio.h>

namespace ReadingCache {
inline bool text(JsonVariantConst value, int maximum, bool required = false) {
    if (value.isNull()) return !required;
    if (!value.is<const char *>()) return false;
    const int length = DeskLogic::utf8Length(value.as<const char *>());
    return length >= (required ? 1 : 0) && length <= maximum;
}
inline bool valid(const char *kind, JsonVariantConst cache) {
    if (!kind || (strcmp(kind, "quote") && strcmp(kind, "poem")) || !cache.is<JsonObjectConst>()) return false;
    if (!text(cache["text"], 3000, true) || !text(cache["author"], 80) ||
        !text(cache["title"], 80) || !text(cache["dynasty"], 80) ||
        !text(cache["source"], 32) || !text(cache["provider"], 32) || !text(cache["date"], 10)) return false;
    if (!cache["date"].isNull()) {
        const char *date = cache["date"].as<const char *>();
        if (*date) {
            int y, m, d, end = 0;
            if (strlen(date) != 10 || sscanf(date, "%d-%d-%d%n", &y, &m, &d, &end) != 3 ||
                end != 10 || !DeskLogic::validDate(y, m, d)) return false;
        }
    }
    for (JsonPairConst field : cache.as<JsonObjectConst>()) {
        const char *key = field.key().c_str();
        if (!strcmp(key, "updatedAt")) {
            if (!field.value().is<int64_t>() || field.value().as<int64_t>() < 0) return false;
        } else if (strcmp(key, "text") && strcmp(key, "author") && strcmp(key, "title") &&
                   strcmp(key, "dynasty") && strcmp(key, "source") && strcmp(key, "provider") && strcmp(key, "date")) return false;
    }
    return true;
}
// A corrupt disposable cache must never discard persistent settings or records.
// Returns true when anything was dropped/replaced, so the caller can persist the
// repaired copy once instead of re-sanitising it on every boot.
inline bool sanitize(JsonObject saved) {
    if (!saved.containsKey("readingCache")) return false;
    if (!saved["readingCache"].is<JsonObject>()) { saved["readingCache"].to<JsonObject>(); return true; }
    bool changed = false;
    JsonObject cache = saved["readingCache"].as<JsonObject>();
    for (auto entry = cache.begin(); entry != cache.end();) {
        auto current = entry;
        ++entry;
        if (!valid(current->key().c_str(), current->value())) { cache.remove(current); changed = true; }
    }
    return changed;
}
}
