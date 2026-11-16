#include "desktop_bridge.h"
#include "ssid_manager.h"
#include "board.h"
#include "display.h"
#include <nvs.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_log.h>
#include <string>

// 双应用桥接（移植自官方 2.5.0 版 hardware/esp-idf/yiyi_xiaozhi/main/boards/yiyi-es3c28p/desktop_bridge.cc）
// 桌面是 Wi-Fi 与 UUID 身份的唯一来源；app1 只做只读导入。

namespace {
std::string ReadString(nvs_handle_t handle, const char* key) {
    size_t size = 0;
    if (nvs_get_str(handle, key, nullptr, &size) != ESP_OK || size == 0) return {};
    std::string result(size, '\0');
    if (nvs_get_str(handle, key, result.data(), &size) != ESP_OK) return {};
    result.resize(size - 1);
    return result;
}
}  // namespace

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
    // 桌面是网络凭据的唯一来源（包括删除）
    auto& manager = SsidManager::GetInstance();
    const auto& saved = manager.GetSsidList();
    if (ssid.empty()) {
        if (!saved.empty()) manager.Clear();
    } else if (saved.size() != 1 || saved[0].ssid != ssid || saved[0].password != password) {
        manager.Clear();
        manager.AddSsid(ssid, password);
    }
    ESP_LOGI("DesktopBridge", "Imported desktop identity, ssid=%s", ssid.c_str());
}

void ExitToDesktop() {
    auto desktop = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
    if (!desktop || esp_ota_set_boot_partition(desktop) != ESP_OK) {
        Board::GetInstance().GetDisplay()->ShowNotification("桌面启动失败，请检查固件");
        return;
    }
    // 桌面消费一次这个标志，恢复退出前的应用列表页
    nvs_handle_t shared;
    if (nvs_open("yiyi-desk", NVS_READWRITE, &shared) == ESP_OK) {
        nvs_set_u8(shared, "voiceReturn", 1);
        nvs_commit(shared);
        nvs_close(shared);
    }
    ESP_LOGI("DesktopBridge", "Switching boot partition back to app0");
    esp_restart();
}
