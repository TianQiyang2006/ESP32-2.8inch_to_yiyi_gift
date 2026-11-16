#include "wifi_board.h"
#include "desktop_bridge.h"
#include <iostream>

class TestBoard : public WifiBoard {
public:
    bool desktop;
    Display screen;
    explicit TestBoard(bool managed) : desktop(managed) { Board::instance = this; }
    bool UsesDesktopWifi() const override { return desktop; }
    Display* GetDisplay() override { return &screen; }
    void Timeout() { OnWifiConnectTimeout(this); }
    void Config() { StartWifiConfigMode(); }
    void Retry() { TryWifiConnect(); }
    bool TimerActive() const { return connect_timer_->active; }
};

void Reset() {
    WifiManager::GetInstance() = WifiManager{};
    SsidManager::GetInstance() = SsidManager{};
    Application::GetInstance() = Application{};
    delays = 0;
}

int main() {
    auto& wifi = WifiManager::GetInstance();
    auto& app = Application::GetInstance();
    auto& saved = SsidManager::GetInstance();
    Reset();
    {
        TestBoard board(true);
        board.StartNetwork(); app.Drain();
        board.Timeout(); board.Config(); board.EnterWifiConfigMode(); app.Drain();
        assert(wifi.apStarts == 0 && wifi.stops == 0 && wifi.starts == 0);
        assert(app.state == kDeviceStateStarting && app.alerts == 0 && delays == 0);
        assert(board.screen.message.find("桌面设置") != std::string::npos);
    }
    Reset();
    {
        saved.saved = {{"desktop", "fixture-secret"}};
        TestBoard board(true);
        int connectedEvents = 0, disconnectedEvents = 0;
        board.SetNetworkEventCallback([&](NetworkEvent event, const std::string&) {
            connectedEvents += event == NetworkEvent::Connected;
            disconnectedEvents += event == NetworkEvent::Disconnected;
        });
        board.StartNetwork();
        assert(wifi.station && board.TimerActive());
        assert(wifi.config.station_scan_min_interval_seconds == 5 && wifi.config.station_scan_max_interval_seconds == 15);
        board.Timeout(); app.Drain();
        assert(wifi.station && wifi.stops == 0 && wifi.apStarts == 0);
        assert(app.state == kDeviceStateStarting && board.screen.message.find("重连") != std::string::npos);
        board.Timeout(); // Connection succeeds before the scheduled hint runs.
        wifi.Emit(WifiEvent::Connected);
        int writes = board.screen.statusWrites;
        app.Drain(); board.Timeout(); board.Retry();
        assert(board.screen.statusWrites == writes && !board.TimerActive());
        assert(connectedEvents == 1 && wifi.starts == 1 && wifi.stops == 0);
        app.state = kDeviceStateListening;
        board.Config(); board.EnterWifiConfigMode(); app.Drain();
        assert(app.state == kDeviceStateListening && app.resets == 0 && wifi.apStarts == 0);
        wifi.Emit(WifiEvent::Disconnected);
        board.Timeout(); app.Drain();
        assert(disconnectedEvents == 1 && wifi.station && wifi.stops == 0);
        wifi.Emit(WifiEvent::Connected);
        assert(connectedEvents == 2 && wifi.connected && wifi.apStarts == 0);
    }
    Reset();
    {
        TestBoard board(false);
        board.StartNetwork(); app.Drain();
        assert(wifi.apStarts == 1 && app.state == kDeviceStateWifiConfiguring && app.alerts == 1);
    }
    Reset();
    {
        saved.saved = {{"official", "fixture"}};
        TestBoard board(false);
        board.StartNetwork(); wifi.Emit(WifiEvent::Connected); board.Timeout();
        assert(wifi.stops == 0 && wifi.apStarts == 0);
        wifi.Emit(WifiEvent::Disconnected); board.Timeout(); app.Drain();
        assert(wifi.stops == 1 && wifi.apStarts == 1);
    }
    Reset(); nvs.clear();
    saved.saved = {{"old-official", "old"}, {"desktop", "old"}};
    nvs["yiyi-desk"] = {{"ssid", "desktop"}, {"pass", "new-fixture"}};
    nvs["yiyi-voice"]["uuid"] = "fixture-uuid";
    ImportDesktopIdentity();
    assert(saved.saved.size() == 1 && saved.saved[0].ssid == "desktop" && saved.saved[0].password == "new-fixture");
    assert(nvs["board"]["uuid"] == "fixture-uuid");
    int clears = saved.clears, adds = saved.adds;
    ImportDesktopIdentity();
    assert(saved.clears == clears && saved.adds == adds); // No flash wear on unchanged credentials.
    nvs["board"]["uuid"] = "existing-uuid";
    nvs["yiyi-desk"] = {{"ssid", "replacement"}, {"pass", ""}};
    ImportDesktopIdentity();
    assert(saved.saved.size() == 1 && saved.saved[0].ssid == "replacement" && saved.saved[0].password.empty());
    assert(nvs["board"]["uuid"] == "existing-uuid");
    nvs["yiyi-desk"]["ssid"] = ""; ImportDesktopIdentity();
    assert(saved.saved.empty());
    saved.saved = {{"stale", "fixture"}}; nvs.erase("yiyi-desk"); ImportDesktopIdentity();
    assert(saved.saved.empty());
    std::cout << "PASS: desktop-only provisioning, timeout/connection race, reconnect, official default, credential replacement/removal, UUID preservation\n";
}
