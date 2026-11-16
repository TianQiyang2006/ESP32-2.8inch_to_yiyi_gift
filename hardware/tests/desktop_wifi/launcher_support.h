#pragma once
#include <Arduino.h>
#include <map>
inline int networkStatus = 0, preferenceWrites = 0, bootChanges = 0, restarts = 0, bootResult = 0;
inline bool officialInstalled = true;
inline const char* officialVersion = "2.5.0";
inline const char* officialProject = "xiaozhi";
#define WL_CONNECTED 3
struct HostWifi { int status() { return networkStatus; } };
inline HostWifi WiFi;
class Preferences {
    std::string name;
public:
    static inline std::map<std::string, int> saved;
    bool begin(const char* space, bool) { name = space; return true; }
    void end() {}
    void putBool(const char* key, bool value) { putInt(key, value); }
    void putInt(const char* key, int value) { saved[name + ":" + key] = value; ++preferenceWrites; }
};
using esp_err_t = int;
#define ESP_OK 0
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 1
struct esp_partition_t {};
struct esp_app_desc_t { const char* project_name = officialProject; const char* version = officialVersion; };
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) { static esp_partition_t p; return officialInstalled ? &p : nullptr; }
inline int esp_ota_get_partition_description(const esp_partition_t*, esp_app_desc_t*) { return 0; }
inline int esp_ota_set_boot_partition(const esp_partition_t*) { ++bootChanges; return bootResult; }
inline const char* esp_err_to_name(int) { return "test-error"; }
struct HostEsp { void restart() { ++restarts; } };
inline HostEsp ESP;
struct HostSerial { void println(const char*) {} void flush() {} };
inline HostSerial Serial;
