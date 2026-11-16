#include "desktop_bridge.h"
#include "ssid_manager.h"
#include "board.h"
#include "display.h"
#include <nvs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_log.h>
#include <string>

namespace {
std::string ReadString(nvs_handle_t handle, const char* key) {
    size_t size = 0;
    if (nvs_get_str(handle, key, nullptr, &size) != ESP_OK || size == 0) return {};
    std::string result(size, '\0');
    if (nvs_get_str(handle, key, result.data(), &size) != ESP_OK) return {};
    result.resize(size - 1);
    return result;
}
}

void ImportDesktopIdentity() {
    nvs_handle_t source, target;
    if (nvs_open("yiyi-voice", NVS_READONLY, &source) == ESP_OK) {
        auto uuid = ReadString(source, "uuid");
        nvs_close(source);
        if (!uuid.empty() && nvs_open("board", NVS_READWRITE, &target) == ESP_OK) {
            if (ReadString(target, "uuid").empty()) {
                ESP_ERROR_CHECK(nvs_set_str(target, "uuid", uuid.c_str()));
                ESP_ERROR_CHECK(nvs_commit(target));
            }
            nvs_close(target);
        }
    }
    std::string ssid, password;
    if (nvs_open("yiyi-desk", NVS_READONLY, &source) == ESP_OK) {
        ssid = ReadString(source, "ssid");
        password = ReadString(source, "pass");
        nvs_close(source);
    }
    // The desktop is the sole source of network credentials, including removal.
    auto& manager = SsidManager::GetInstance();
    const auto& saved = manager.GetSsidList();
    if (ssid.empty()) {
        if (!saved.empty()) manager.Clear();
    } else if (saved.size() != 1 || saved[0].ssid != ssid || saved[0].password != password) {
        manager.Clear();
        manager.AddSsid(ssid, password);
    }
}

void ExitToDesktop() {
    auto desktop = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
    if (!desktop || esp_ota_set_boot_partition(desktop) != ESP_OK) {
        Board::GetInstance().GetDisplay()->ShowNotification("桌面启动失败，请检查固件");
        return;
    }
    // The desktop consumes this flag once, restoring the existing app launcher.
    nvs_handle_t shared;
    if (nvs_open("yiyi-desk", NVS_READWRITE, &shared) == ESP_OK) {
        nvs_set_u8(shared, "voiceReturn", 1);
        nvs_commit(shared);
        nvs_close(shared);
    }
    esp_restart();
}
