#pragma once
#include <cassert>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <cstdlib>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_OK 0
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
#define BOARD_TYPE "test"
#define BOARD_NAME "test"
#define BOARD_MANUFACTURER "yiyi"
#define ESP_TIMER_TASK 0
#define CONFIG_USE_HOTSPOT_WIFI_PROVISIONING 1
#define MATERIAL_SYMBOLS_WIFI "wifi"
#define MATERIAL_SYMBOLS_WIFI_OFF "offline"
#define MATERIAL_SYMBOLS_WIFI_2_BAR "fair"
#define MATERIAL_SYMBOLS_WIFI_1_BAR "weak"
#define ESP_MAC_WIFI_STA 0
inline int esp_read_mac(uint8_t* mac, int) { std::memset(mac, 0, 6); return ESP_OK; }
#define pdMS_TO_TICKS(x) (x)
inline int delays = 0;
inline void vTaskDelay(int) { ++delays; }
inline void vTaskDelete(void*) {}
inline void xTaskCreate(void (*fn)(void*), const char*, int, void* arg, int, void*) { fn(arg); }
struct TestTimer { void (*callback)(void*); void* arg; bool active = false; };
using esp_timer_handle_t = TestTimer*;
struct esp_timer_create_args_t { void (*callback)(void*); void* arg; int dispatch_method; const char* name; bool skip_unhandled_events; };
inline int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out) { *out = new TestTimer{args->callback, args->arg}; return 0; }
inline int esp_timer_stop(esp_timer_handle_t timer) { timer->active = false; return 0; }
inline int esp_timer_delete(esp_timer_handle_t timer) { delete timer; return 0; }
inline int esp_timer_start_once(esp_timer_handle_t timer, unsigned long long) { timer->active = true; return 0; }
enum DeviceState { kDeviceStateStarting, kDeviceStateWifiConfiguring, kDeviceStateSpeaking, kDeviceStateNotifying, kDeviceStateListening, kDeviceStateIdle };
class Application {
public:
    DeviceState state = kDeviceStateStarting;
    int resets = 0, alerts = 0;
    std::vector<std::function<void()>> pending;
    static Application& GetInstance() { static Application app; return app; }
    void Schedule(std::function<void()> fn) { pending.push_back(fn); }
    void Drain() { auto tasks = std::move(pending); pending.clear(); for (auto& fn : tasks) fn(); }
    void SetDeviceState(DeviceState next) { state = next; }
    DeviceState GetDeviceState() { return state; }
    void ResetProtocol() { ++resets; }
    void Alert(const char*, const char*, const char*, const char*) { ++alerts; }
};
struct Theme { std::string name() { return "test"; } };
class Display {
public:
    std::string status, message, notification;
    int statusWrites = 0;
    void SetStatus(const char* text) { status = text; ++statusWrites; }
    void SetChatMessage(const char*, const char* text) { message = text; }
    void ShowNotification(const char* text) { notification = text; }
    int height() { return 240; }
    Theme* GetTheme() { return nullptr; }
};
enum class NetworkEvent { Scanning, Connecting, Connected, Disconnected, WifiConfigModeEnter, WifiConfigModeExit };
using NetworkEventCallback = std::function<void(NetworkEvent, const std::string&)>;
enum class PowerSaveLevel { LOW_POWER, BALANCED, PERFORMANCE };
class NetworkInterface {};
class EspNetwork : public NetworkInterface {};
class AudioCodec { public: int output_volume() { return 53; } };
class Backlight { public: int brightness() { return 79; } };
class Board {
public:
    static inline Board* instance = nullptr;
    static Board& GetInstance() { return *instance; }
    virtual ~Board() = default;
    virtual std::string GetBoardJson() { return {}; }
    virtual std::string GetBoardType() { return {}; }
    virtual void StartNetwork() {}
    virtual NetworkInterface* GetNetwork() { return nullptr; }
    virtual void SetNetworkEventCallback(NetworkEventCallback) {}
    virtual const char* GetNetworkStateIcon() { return ""; }
    virtual void SetPowerSaveLevel(PowerSaveLevel) {}
    virtual AudioCodec* GetAudioCodec() { return nullptr; }
    virtual std::string GetDeviceStatusJson() { return {}; }
    virtual Display* GetDisplay() { return nullptr; }
    Backlight* GetBacklight() { return nullptr; }
    bool GetBatteryLevel(int&, bool&, bool&) { return false; }
    bool GetTemperature(float&) { return false; }
};
struct SsidItem { std::string ssid, password; };
class SsidManager {
public:
    std::vector<SsidItem> saved;
    int clears = 0, adds = 0;
    static SsidManager& GetInstance() { static SsidManager manager; return manager; }
    const std::vector<SsidItem>& GetSsidList() const { return saved; }
    void Clear() { saved.clear(); ++clears; }
    void AddSsid(const std::string& ssid, const std::string& password) { saved.insert(saved.begin(), {ssid, password}); ++adds; }
};
enum class WifiEvent { Scanning, Connecting, Connected, Disconnected, ConfigModeEnter, ConfigModeExit };
enum class WifiPowerSaveLevel { LOW_POWER, BALANCED, PERFORMANCE };
struct WifiManagerConfig { std::string ssid_prefix, language, station_hostname; bool show_ota_config = false, show_sleep_config = false; int station_scan_min_interval_seconds = 10, station_scan_max_interval_seconds = 300; };
class WifiManager {
public:
    bool connected = false, station = false, configMode = false;
    int starts = 0, stops = 0, apStarts = 0;
    WifiManagerConfig config;
    std::function<void(WifiEvent, const std::string&)> callback;
    static WifiManager& GetInstance() { static WifiManager manager; return manager; }
    void Initialize(WifiManagerConfig value) { config = value; }
    void SetEventCallback(std::function<void(WifiEvent, const std::string&)> fn) { callback = fn; }
    void StartStation() { station = true; ++starts; }
    void StopStation() { station = connected = false; ++stops; }
    bool IsConnected() { return connected; }
    bool IsConfigMode() { return configMode; }
    void StartConfigAp() { configMode = true; ++apStarts; }
    std::string GetSsid() { return "desktop"; }
    std::string GetIpAddress() { return "192.0.2.2"; }
    std::string GetApSsid() { return "Xiaozhi-test"; }
    std::string GetApWebUrl() { return "http://192.0.2.1"; }
    int GetRssi() { return -45; }
    int GetChannel() { return 1; }
    void SetPowerSaveLevel(WifiPowerSaveLevel) {}
    void Emit(WifiEvent event) { if (event == WifiEvent::Connected) connected = true; if (event == WifiEvent::Disconnected) connected = false; callback(event, GetSsid()); }
};
namespace Lang {
inline const char* CODE = "zh-CN";
namespace Strings { inline const char *CONNECT_TO_HOTSPOT = "connect", *ACCESS_VIA_BROWSER = "browser", *WIFI_CONFIG_MODE = "configure", *ENTERING_WIFI_CONFIG_MODE = "enter"; }
namespace Sounds { inline const char* OGG_WIFICONFIG = "sound"; }
}
struct SystemInfo { static std::string GetMacAddress() { return "00:00:00:00:00:00"; } };
struct cJSON {};
inline cJSON* cJSON_CreateObject() { return nullptr; }
inline void cJSON_AddNumberToObject(cJSON*, const char*, int) {}
inline void cJSON_AddItemToObject(cJSON*, const char*, cJSON*) {}
inline void cJSON_AddBoolToObject(cJSON*, const char*, bool) {}
inline void cJSON_AddStringToObject(cJSON*, const char*, const char*) {}
inline char* cJSON_PrintUnformatted(cJSON*) { auto text = static_cast<char*>(malloc(3)); strcpy(text, "{}"); return text; }
inline void cJSON_free(void* ptr) { free(ptr); }
inline void cJSON_Delete(cJSON*) {}
using nvs_handle_t = std::string;
inline std::map<std::string, std::map<std::string, std::string>> nvs;
#define NVS_READONLY 0
#define NVS_READWRITE 1
inline int nvs_open(const char* name, int mode, nvs_handle_t* handle) { if (!mode && !nvs.count(name)) return -1; *handle = name; return 0; }
inline int nvs_get_str(nvs_handle_t handle, const char* key, char* out, size_t* size) { if (!nvs[handle].count(key)) return -1; const auto& value = nvs[handle][key]; if (out) { assert(*size >= value.size() + 1); strcpy(out, value.c_str()); } *size = value.size() + 1; return 0; }
inline int nvs_set_str(nvs_handle_t handle, const char* key, const char* value) { nvs[handle][key] = value; return 0; }
inline int nvs_set_u8(nvs_handle_t, const char*, int) { return 0; }
inline int nvs_commit(nvs_handle_t) { return 0; }
inline void nvs_close(nvs_handle_t) {}
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 0
struct esp_partition_t {};
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) { static esp_partition_t partition; return &partition; }
inline int esp_ota_set_boot_partition(const esp_partition_t*) { return 0; }
inline void esp_restart() {}
