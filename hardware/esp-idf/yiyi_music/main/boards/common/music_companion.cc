#include "music_companion.h"
#include "esp32_music.h"
#include "board.h"
#include "application.h"
#include "display.h"
#include "settings.h"
#include "assets/lang_config.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cmath>
#include <cstring>

static const char* TAG = "MusicCompanion";

static std::string string_field(cJSON* root, const char* name) {
    auto item = cJSON_GetObjectItem(root, name);
    return cJSON_IsString(item) ? item->valuestring : "";
}

void MusicCompanion::Start() {
    Settings settings("music", false);
    base_ = settings.GetString("server_url", "");
    if (base_.empty()) { return; }
    last_command_ = settings.GetInt("last_cmd", 0);
    xTaskCreate([](void* arg) {
        static_cast<MusicCompanion*>(arg)->Run();
    }, "companion", 6144, this, 2, nullptr);
}

std::string MusicCompanion::Request(const std::string& url, const std::string& body, size_t limit) {
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 6000;
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    auto http = esp_http_client_init(&config);
    if (!http) { return ""; }
    esp_http_client_set_method(http, body.empty() ? HTTP_METHOD_GET : HTTP_METHOD_POST);
    esp_http_client_set_header(http, "Content-Type", "application/json");
    std::string result;
    if (esp_http_client_open(http, body.size()) == ESP_OK) {
        size_t sent = 0;
        while (sent < body.size()) {
            int n = esp_http_client_write(http, body.data() + sent, body.size() - sent);
            if (n <= 0) { break; }
            sent += n;
        }
        auto length = esp_http_client_fetch_headers(http);
        if (sent == body.size() && esp_http_client_get_status_code(http) == 200 && length >= 0 && size_t(length) <= limit) {
            result.reserve(length);
            char chunk[768];
            int count;
            while ((count = esp_http_client_read(http, chunk, sizeof(chunk))) > 0) {
                if (result.size() + count > limit) { result.clear(); break; }
                result.append(chunk, count);
            }
            if (count < 0 || !esp_http_client_is_complete_data_received(http)) { result.clear(); }
        }
    }
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    return result;
}

std::string MusicCompanion::ListPlaylists() {
    return base_.empty() ? "" : Request(base_ + "/device/ui/playlists", "", 8192);
}

std::string MusicCompanion::PlayPlaylist(const std::string& name) {
    if (base_.empty()) { return ""; }
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "name", name.c_str());
    cJSON_AddStringToObject(body, "mode", "sequence");
    char* encoded = cJSON_PrintUnformatted(body);
    std::string response = Request(base_ + "/device/ui/playlists", encoded ? encoded : "{}");
    cJSON_free(encoded);
    cJSON_Delete(body);
    return response;
}

std::string MusicCompanion::PlayerControl(const std::string& action) {
    if (base_.empty()) { return ""; }
    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "action", action.c_str());
    char* encoded = cJSON_PrintUnformatted(body);
    std::string response = Request(base_ + "/device/ui/control", encoded ? encoded : "{}");
    cJSON_free(encoded);
    cJSON_Delete(body);
    return response;
}

void MusicCompanion::Remind(const std::string& text, const std::string& audio, const std::string& base) {
    auto music = static_cast<Esp32Music*>(Board::GetInstance().GetMusic());
    auto& app = Application::GetInstance();
    music->SetAlertActive(true);
    auto& service = app.GetAudioService();
    bool wake = service.IsWakeWordRunning();
    service.EnableWakeWordDetection(false);
    Board::GetInstance().GetDisplay()->SetChatMessage("system", text.c_str());
    app.PlaySound(Lang::Sounds::P3_EXCLAMATION);
    if (audio.size() == 32 && std::all_of(audio.begin(), audio.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    })) {
        auto data = Request(base + "/alerts/" + audio + ".p3", "", 160000);
        // Validate packet boundaries before using the existing P3 decoder.
        size_t offset = 0;
        bool valid = !data.empty();
        while (offset + 4 <= data.size()) {
            unsigned length = (uint8_t(data[offset + 2]) << 8) | uint8_t(data[offset + 3]);
            if (!length || length > 4096 || offset + 4 + length > data.size()) { valid = false; break; }
            offset += 4 + length;
        }
        if (valid && offset == data.size()) {
            app.PlaySound(std::string_view(data.data(), data.size()));
            ESP_LOGI(TAG, "Reminder voice queued: bytes=%u", (unsigned)data.size());
        } else {
            ESP_LOGW(TAG, "Reminder voice unavailable; chime and text delivered");
        }
    }
    for (int i = 0; i < 700; ++i) {
        if (service.IsIdle()) { vTaskDelay(pdMS_TO_TICKS(120)); break; }
        if (app.GetDeviceState() != kDeviceStateIdle) { break; }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    music->SetAlertActive(false);
    if (wake && app.GetDeviceState() == kDeviceStateIdle) { service.EnableWakeWordDetection(true); }
    ESP_LOGI(TAG, "Reminder delivered");
}

std::string MusicCompanion::Execute(cJSON* command, double server_time) {
    auto music = static_cast<Esp32Music*>(Board::GetInstance().GetMusic());
    if (!music) { return "no_player"; }
    std::string action = string_field(command, "action");
    auto payload = cJSON_GetObjectItem(command, "payload");
    if (action == "pause") { return music->PauseStreaming() ? "paused" : "no_active_song"; }
    if (action == "resume") { return music->ResumeStreaming() ? "resumed" : "no_active_song"; }
    if (action == "stop") {
        music->SetSleepTimer(0);
        music->StopStreaming();
        return "stopped";
    }
    if (action == "sleep") {
        auto deadline = cJSON_GetObjectItem(payload, "deadline");
        if (!cJSON_IsNumber(deadline)) { return "invalid_timer"; }
        int remaining = std::max(1, int(std::ceil(deadline->valuedouble - server_time)));
        music->SetSleepTimer(std::min(remaining, 86400));
        return "sleep_timer_set";
    }
    if (action == "cancel_sleep") { music->SetSleepTimer(0); return "sleep_timer_cancelled"; }
    if (action == "play") {
        return music->PlayPrepared(string_field(payload, "song"), string_field(payload, "token")) ? "playing" : "play_failed";
    }
    if (action == "remind") {
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) { return "defer"; }
        Remind(string_field(payload, "text"), string_field(payload, "audio"), base_);
        return "reminder_delivered";
    }
    return "unknown_action";
}

void MusicCompanion::Run() {
    std::string boot = std::to_string(esp_random());
    auto music = static_cast<Esp32Music*>(Board::GetInstance().GetMusic());
    // Plain HTTP uses 1 KB RX/TX buffers and needs no TLS handshake. Playback
    // normally leaves ~17 KB of internal RAM, so the TLS-sized 22 KB reserve
    // would suppress every sync and prevent queued track controls from running.
    const size_t sync_reserve = base_.compare(0, 7, "http://") == 0 ? 12000 : 22000;
    vTaskDelay(pdMS_TO_TICKS(10000));
    while (true) {
        music->CheckSleepTimer();
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() < kDeviceStateIdle || heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < sync_reserve) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        auto state = music->PlaybackStatus();
        cJSON_AddStringToObject(state, "boot", boot.c_str());
        cJSON_AddBoolToObject(state, "idle", app.GetDeviceState() == kDeviceStateIdle);
        cJSON_AddNumberToObject(state, "ack", last_command_);
        cJSON_AddStringToObject(state, "result", last_result_.c_str());
        char* encoded = cJSON_PrintUnformatted(state);
        std::string request = encoded ? encoded : "{}";
        cJSON_free(encoded);
        cJSON_Delete(state);
        auto response = Request(base_ + "/device/sync", request);
        auto root = cJSON_Parse(response.c_str());
        if (root) {
            auto command = cJSON_GetObjectItem(root, "command");
            auto id = cJSON_GetObjectItem(command, "id");
            auto now = cJSON_GetObjectItem(root, "server_time");
            if (cJSON_IsNumber(id) && cJSON_IsNumber(now) && unsigned(id->valuedouble) > last_command_) {
                std::string result = Execute(command, now->valuedouble);
                if (result != "defer") {
                    last_command_ = unsigned(id->valuedouble);
                    last_result_ = result;
                    Settings settings("music", true);
                    settings.SetInt("last_cmd", last_command_);
                    ESP_LOGI(TAG, "Command applied: id=%u action=%s result=%s", last_command_, string_field(command, "action").c_str(), result.c_str());
                }
            }
            cJSON_Delete(root);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
