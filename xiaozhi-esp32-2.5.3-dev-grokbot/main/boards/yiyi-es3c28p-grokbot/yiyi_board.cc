#include "application.h"
#include "assets/lang_config.h"
#include "audio/codecs/es8311_audio_codec.h"
#include "config.h"
#include "display/grokbot_display.h"
#include "display/lvgl_display/lvgl_font.h"
#include "ssid_manager.h"
#include "system_info.h"
#include "wifi_board.h"
#include "wifi_manager.h"
#include "yiyi_battery.h"
#include "yiyi_touch_transform.h"

#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <src/misc/lv_text_private.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>

namespace {
constexpr const char* TAG = "YiyiGrokbot";
extern const uint8_t common_text_font_start[] asm("_binary_yiyi_common_text_font_start");

std::string ReadString(nvs_handle_t handle, const char* key) {
    size_t size = 0;
    if (nvs_get_str(handle, key, nullptr, &size) != ESP_OK || size < 2)
        return {};
    std::string value(size, '\0');
    if (nvs_get_str(handle, key, value.data(), &size) != ESP_OK)
        return {};
    value.resize(size - 1);
    return value;
}

void ExitToDesktop() {
    const auto* desktop =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "app0");
    if (!desktop || esp_ota_set_boot_partition(desktop) != ESP_OK) {
        Board::GetInstance().GetDisplay()->ShowNotification("桌面启动失败，请检查固件");
        return;
    }
    nvs_handle_t shared;
    if (nvs_open("yiyi-desk", NVS_READWRITE, &shared) == ESP_OK) {
        nvs_set_u8(shared, "voiceReturn", 1);
        nvs_commit(shared);
        nvs_close(shared);
    }
    ESP_LOGI(TAG, "Switching boot partition back to app0");
    esp_restart();
}

class YiyiGrokbotDisplay : public GrokbotDisplay {
    std::function<void()> setup_complete_;

public:
    YiyiGrokbotDisplay(esp_lcd_panel_io_handle_t io, esp_lcd_panel_handle_t panel,
                       std::function<void()> setup_complete)
        : GrokbotDisplay(io, panel, 320, 240, 0, 0, false, false, true, GPIO_NUM_NC, 20, 20),
          setup_complete_(std::move(setup_complete)) {}

    void SetupUI() override {
        GrokbotDisplay::SetupUI();
        // Use regular, antialiased common Chinese glyphs even with the older
        // assets pack installed. The CBIN bitmap stays mapped in app1 flash.
        auto text_font =
            std::make_shared<LvglCBinFont>(const_cast<uint8_t*>(common_text_font_start));
        ESP_ERROR_CHECK(SetTextFont(text_font) ? ESP_OK : ESP_FAIL);
        DisplayLockGuard lock(this);
        if (!lock)
            return;
        // The square animation fills the central 200 pixels. Status, subtitles
        // and the exit control occupy the narrow strips around it.
        lv_obj_set_style_pad_left(status_bar_, 0, 0);
        lv_obj_set_style_pad_right(status_bar_, 0, 0);
        lv_obj_set_style_pad_top(status_bar_, 0, 0);
        lv_obj_set_style_pad_bottom(status_bar_, 0, 0);
        lv_obj_set_size(status_bar_, 212, text_font->font()->line_height + 2);
        lv_obj_align(status_bar_, LV_ALIGN_TOP_LEFT, 30, 0);
        lv_obj_set_width(status_label_, 208);
        lv_obj_set_width(notification_label_, 208);
        lv_obj_set_style_pad_top(top_bar_, 0, 0);
        lv_obj_set_style_pad_right(top_bar_, 4, 0);
        lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_height(bottom_bar_, text_font->font()->line_height + 4);
        lv_obj_set_style_pad_top(bottom_bar_, 0, 0);
        lv_obj_set_style_pad_bottom(bottom_bar_, 0, 0);
        lv_obj_set_width(chat_message_label_, 308);
        lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        if (setup_complete_) {
            auto callback = std::move(setup_complete_);
            callback();
        }
    }

    bool SetTextFont(std::shared_ptr<LvglFont> text_font) override {
        // An app-only upgrade can retain the original 14px/1bpp assets pack.
        // Do not let that asset replace the embedded 16px/4bpp common font.
        if (!text_font || !text_font->font() || !text_font->font()->dsc ||
            static_cast<const lv_font_fmt_txt_dsc_t*>(text_font->font()->dsc)->bpp != 4) {
            return false;
        }
        return GrokbotDisplay::SetTextFont(std::move(text_font));
    }

    void PrintFontStatus() {
        DisplayLockGuard lock(this);
        if (!lock)
            return;
        lv_obj_update_layout(lv_screen_active());
        const auto* font = lv_obj_get_style_text_font(chat_message_label_, LV_PART_MAIN);
        unsigned missing_glyphs = 0;
        const char* text = lv_label_get_text(chat_message_label_);
        for (uint32_t offset = 0; text[offset] != '\0';) {
            const uint32_t codepoint = lv_text_encoded_next(text, &offset);
            lv_font_glyph_dsc_t glyph = {};
            if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.is_placeholder)
                ++missing_glyphs;
        }
        lv_area_t label, bar;
        lv_obj_get_coords(chat_message_label_, &label);
        lv_obj_get_content_coords(bottom_bar_, &bar);
        printf(
            "{\"fontSize\":%d,\"fontBpp\":%u,\"lineHeight\":%d,"
            "\"subtitleY\":%d,\"subtitleBottom\":%d,\"barY\":%d,\"barBottom\":%d,"
            "\"subtitleWidth\":%d,\"subtitleVisible\":%s,\"missingGlyphs\":%u}\n",
            TEXT_FONT_SIZE, unsigned(static_cast<const lv_font_fmt_txt_dsc_t*>(font->dsc)->bpp),
            int(font->line_height), int(label.y1), int(label.y2), int(bar.y1), int(bar.y2),
            int(lv_obj_get_width(chat_message_label_)),
            lv_obj_has_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN) ? "false" : "true", missing_glyphs);
        fflush(stdout);
    }

    void SetChatMessage(const char* role, const char* content) override {
        // Keep the startup board/version identifier out of the product UI.
        if (role && content && strcmp(role, "system") == 0 && SystemInfo::GetUserAgent() == content)
            return;
        GrokbotDisplay::SetChatMessage(role, content);
    }
};
}  // namespace

class YiyiGrokbotBoard : public WifiBoard {
    i2c_master_bus_handle_t i2c_ = nullptr;
    i2c_master_dev_handle_t touch_ = nullptr;
    YiyiGrokbotDisplay* display_ = nullptr;
    TouchTransform::Affine affine_;
    uint8_t touch_mode_ = 0;
    bool calibrated_ = false;
    bool two_point_ = false;
    int16_t x_min_ = 0, x_max_ = 319, y_min_ = 0, y_max_ = 239;
    std::atomic<int> injected_phase_{0};
    int injected_x_ = 0, injected_y_ = 0;
    YiyiBattery battery_;

    void ImportDesktopWifi() {
        nvs_handle_t shared;
        std::string ssid, password;
        if (nvs_open("yiyi-desk", NVS_READONLY, &shared) == ESP_OK) {
            ssid = ReadString(shared, "ssid");
            password = ReadString(shared, "pass");
            nvs_close(shared);
        }
        auto& manager = SsidManager::GetInstance();
        const auto& saved = manager.GetSsidList();
        if (ssid.empty()) {
            if (!saved.empty())
                manager.Clear();
        } else if (saved.size() != 1 || saved[0].ssid != ssid || saved[0].password != password) {
            manager.Clear();
            manager.AddSsid(ssid, password);
        }
    }

    void AddDesktopControls() {
        auto screen = lv_screen_active();
        auto exit = lv_button_create(screen);
        lv_obj_set_size(exit, 62, 28);
        lv_obj_align(exit, LV_ALIGN_TOP_RIGHT, -3, 24);
        lv_obj_set_style_radius(exit, 7, 0);
        lv_obj_set_style_bg_color(exit, lv_color_hex(0x252525), 0);
        lv_obj_set_style_text_color(exit, lv_color_white(), 0);
        LV_FONT_DECLARE(yiyi_exit_font_20);
        lv_obj_set_style_text_font(exit, &yiyi_exit_font_20, 0);
        auto label = lv_label_create(exit);
        lv_label_set_text(label, "退出");
        lv_obj_center(label);
        lv_obj_add_event_cb(
            exit, [](lv_event_t*) { Application::GetInstance().Schedule(ExitToDesktop); },
            LV_EVENT_CLICKED, nullptr);
        auto tap = [](lv_event_t*) { Application::GetInstance().ToggleChatState(); };
        // Route touches on the background and the animation through LVGL.
        lv_obj_add_event_cb(screen, tap, LV_EVENT_CLICKED, nullptr);
        // Child objects receive clicks themselves; bubbling reaches the screen.
        for (uint32_t i = 0; i < lv_obj_get_child_count(screen); ++i) {
            auto child = lv_obj_get_child(screen, i);
            if (child != exit)
                lv_obj_add_flag(child, LV_OBJ_FLAG_EVENT_BUBBLE);
        }
        lv_obj_move_foreground(exit);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t bus = {};
        bus.i2c_port = I2C_NUM_0;
        bus.sda_io_num = AUDIO_CODEC_I2C_SDA_PIN;
        bus.scl_io_num = AUDIO_CODEC_I2C_SCL_PIN;
        bus.clk_source = I2C_CLK_SRC_DEFAULT;
        bus.glitch_ignore_cnt = 7;
        bus.flags.enable_internal_pullup = true;
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &i2c_));
        gpio_config_t reset = {};
        reset.pin_bit_mask = 1ULL << TOUCH_RST_PIN;
        reset.mode = GPIO_MODE_OUTPUT;
        ESP_ERROR_CHECK(gpio_config(&reset));
        gpio_set_level(TOUCH_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(15));
        gpio_set_level(TOUCH_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        i2c_device_config_t touch = {};
        touch.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        touch.device_address = 0x38;
        touch.scl_speed_hz = 100000;
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_, &touch, &touch_));
    }

    void InitializeDisplay() {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = DISPLAY_MOSI_PIN;
        bus.miso_io_num = DISPLAY_MISO_PIN;
        bus.sclk_io_num = DISPLAY_CLK_PIN;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 320 * 240 * 2;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
        esp_lcd_panel_io_spi_config_t io = {};
        io.cs_gpio_num = DISPLAY_CS_PIN;
        io.dc_gpio_num = DISPLAY_DC_PIN;
        io.pclk_hz = 20000000;
        io.trans_queue_depth = 10;
        io.lcd_cmd_bits = 8;
        io.lcd_param_bits = 8;
        esp_lcd_panel_io_handle_t panel_io;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &panel_io));
        esp_lcd_panel_dev_config_t config = {};
        config.reset_gpio_num = GPIO_NUM_NC;
        config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        config.bits_per_pixel = 16;
        esp_lcd_panel_handle_t panel;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &config, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        // Electrical setup already verified on this particular ES3C28P IPS panel.
        auto command = [panel_io](int cmd, std::initializer_list<uint8_t> bytes) {
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io, cmd, bytes.begin(), bytes.size()));
        };
        command(0xef, {0x03, 0x80, 0x02});
        command(0xcf, {0x00, 0xc1, 0x30});
        command(0xed, {0x64, 0x03, 0x12, 0x81});
        command(0xe8, {0x85, 0x00, 0x78});
        command(0xcb, {0x39, 0x2c, 0x00, 0x34, 0x02});
        command(0xf7, {0x20});
        command(0xea, {0x00, 0x00});
        command(0xc0, {0x23});
        command(0xc1, {0x10});
        command(0xc5, {0x3e, 0x28});
        command(0xc7, {0x86});
        command(0xb1, {0x00, 0x13});
        command(0xb6, {0x08, 0x82, 0x27});
        command(0xf2, {0x00});
        command(0x26, {0x01});
        command(0xe0, {0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e,
                       0x09, 0x00});
        command(0xe1, {0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31,
                       0x36, 0x0f});
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
        display_ = new YiyiGrokbotDisplay(panel_io, panel, [this] { AddDesktopControls(); });
    }

    void InitializeTouch() {
        nvs_handle_t shared;
        if (nvs_open("yiyi-desk", NVS_READONLY, &shared) == ESP_OK) {
            uint8_t enabled = 0, version = 0, two_point = 0;
            nvs_get_u8(shared, "affok", &enabled);
            nvs_get_u8(shared, "affver", &version);
            nvs_get_u8(shared, "touch", &touch_mode_);
            if (enabled && version == 2) {
                const char* keys[] = {"affax", "affbx", "affcx", "affay", "affby", "affcy"};
                float* values[] = {&affine_.ax, &affine_.bx, &affine_.cx,
                                   &affine_.ay, &affine_.by, &affine_.cy};
                bool valid = true;
                for (int i = 0; i < 6; ++i) {
                    size_t size = sizeof(float);
                    valid &= nvs_get_blob(shared, keys[i], values[i], &size) == ESP_OK &&
                             size == sizeof(float);
                }
                affine_.valid = valid && TouchTransform::finite(affine_);
            }
            nvs_get_u8(shared, "calok", &two_point);
            nvs_get_i16(shared, "calx0", &x_min_);
            nvs_get_i16(shared, "calx1", &x_max_);
            nvs_get_i16(shared, "caly0", &y_min_);
            nvs_get_i16(shared, "caly1", &y_max_);
            two_point_ = two_point && x_max_ > x_min_ && y_max_ > y_min_;
            nvs_close(shared);
        }
        calibrated_ = affine_.valid || two_point_;
        lvgl_port_lock(0);
        auto input = lv_indev_create();
        lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(input, this);
        lv_indev_set_read_cb(input, [](lv_indev_t* input, lv_indev_data_t* data) {
            static_cast<YiyiGrokbotBoard*>(lv_indev_get_user_data(input))->ReadTouch(data);
        });
        lvgl_port_unlock();
    }

    void ReadTouch(lv_indev_data_t* data) {
        int phase = injected_phase_.load();
        if (phase) {
            data->point = {injected_x_, injected_y_};
            data->state = phase == 2 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            injected_phase_.store(phase - 1);
            return;
        }
        data->state = LV_INDEV_STATE_RELEASED;
        uint8_t reg = 0x02, bytes[5] = {};
        if (i2c_master_transmit_receive(touch_, &reg, 1, bytes, sizeof(bytes), 20) != ESP_OK ||
            !(bytes[0] & 15) || (bytes[0] & 15) > 2 || (bytes[1] >> 6) == 1)
            return;
        int rx = ((bytes[1] & 15) << 8) | bytes[2];
        int ry = ((bytes[3] & 15) << 8) | bytes[4];
        int16_t x, y;
        if (affine_.valid) {
            if (!TouchTransform::map(affine_, rx, ry, x, y))
                return;
        } else {
            if (rx > 239 || ry > 319)
                return;
            x = ry;
            y = 239 - rx;
        }
        if (touch_mode_ & 1)
            x = 319 - x;
        if (touch_mode_ & 2)
            y = 239 - y;
        if (!affine_.valid && two_point_) {
            x = std::clamp((x - x_min_) * 319 / (x_max_ - x_min_), 0, 319);
            y = std::clamp((y - y_min_) * 239 / (y_max_ - y_min_), 0, 239);
        }
        data->point = {x, y};
        data->state = LV_INDEV_STATE_PRESSED;
    }

    void HandleCommand(const std::string& command) {
        auto& app = Application::GetInstance();
        if (command == "status") {
            const auto* running = esp_ota_get_running_partition();
            auto battery = battery_.Get();
            printf(
                "{\"implementation\":\"grokbot-2.5.3\",\"version\":\"%s\",\"partition\":\"%s\","
                "\"uptimeMs\":%u,\"state\":%d,\"wifi\":%s,\"touchCalibrated\":%s,"
                "\"heapFree\":%u,\"psramFree\":%u,\"wakeWordRunning\":%s,"
                "\"batteryMv\":%d,\"batteryPercent\":%d}\n",
                esp_app_get_description()->version, running ? running->label : "unknown",
                unsigned(esp_timer_get_time() / 1000), int(app.GetDeviceState()),
                WifiManager::GetInstance().IsConnected() ? "true" : "false",
                calibrated_ ? "true" : "false",
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                app.GetAudioService().IsWakeWordRunning() ? "true" : "false", battery.millivolts,
                battery.percent);
            fflush(stdout);
        } else if (command == "back") {
            app.Schedule(ExitToDesktop);
        } else if (command == "talk") {
            app.ToggleChatState();
        } else if (command.starts_with("tap ")) {
            int x, y;
            if (sscanf(command.c_str(), "tap %d %d", &x, &y) == 2 && x >= 0 && x < 320 && y >= 0 &&
                y < 240 && !injected_phase_.load()) {
                injected_x_ = x;
                injected_y_ = y;
                injected_phase_.store(2);
            }
        } else if (command.starts_with("emotion ")) {
            app.Schedule([this, emotion = command.substr(8)] {
                display_->SetStatus(Lang::Strings::STANDBY);
                display_->SetEmotion(emotion.c_str());
            });
        } else if (command == "fontcheck") {
            app.Schedule([this] { display_->PrintFontStatus(); });
        } else if (command == "text" || command.starts_with("text ")) {
            app.Schedule([this, text = command.size() > 4 ? command.substr(5) : std::string()] {
                display_->SetChatMessage("assistant", text.c_str());
            });
        } else if (command.starts_with("notify ")) {
            app.Schedule(
                [this, text = command.substr(7)] { display_->ShowNotification(text, 10000); });
        } else if (command == "screenshot") {
            std::string jpeg;
            esp_log_level_set("*", ESP_LOG_NONE);
            if (display_->SnapshotToJpeg(jpeg, 90)) {
                printf("JPEG %u\n", unsigned(jpeg.size()));
                fflush(stdout);
                for (size_t offset = 0; offset < jpeg.size();) {
                    int written = usb_serial_jtag_write_bytes(
                        jpeg.data() + offset, std::min(size_t(512), jpeg.size() - offset),
                        pdMS_TO_TICKS(5000));
                    if (written <= 0)
                        break;
                    offset += written;
                }
                printf("\nEND\n");
                fflush(stdout);
            }
            esp_log_level_set("*", ESP_LOG_INFO);
        }
    }

    void InitializeDiagnostics() {
        usb_serial_jtag_driver_config_t config = {};
        config.tx_buffer_size = 4096;
        config.rx_buffer_size = 256;
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
        usb_serial_jtag_vfs_use_driver();
        xTaskCreate(
            [](void* argument) {
                auto self = static_cast<YiyiGrokbotBoard*>(argument);
                std::string command;
                int64_t next_battery = 0;
                for (;;) {
                    if (esp_timer_get_time() >= next_battery) {
                        self->battery_.Poll();
                        next_battery = esp_timer_get_time() + 2000000;
                    }
                    char ch;
                    if (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100)) <= 0 || ch == '\r')
                        continue;
                    if (ch != '\n') {
                        if (command.size() < 80)
                            command += ch;
                        continue;
                    }
                    self->HandleCommand(command);
                    command.clear();
                }
            },
            "yiyi_diag", 6144, this, 1, nullptr);
    }

public:
    YiyiGrokbotBoard() {
        InitializeI2c();
        InitializeDisplay();
        InitializeTouch();
        battery_.Begin();
        GetBacklight()->RestoreBrightness();
        InitializeDiagnostics();
    }

    void StartNetwork() override {
        auto& wifi = WifiManager::GetInstance();
        WifiManagerConfig config;
        config.ssid_prefix = "Xiaozhi";
        config.language = Lang::CODE;
        config.station_scan_min_interval_seconds = 5;
        config.station_scan_max_interval_seconds = 15;
        wifi.Initialize(config);
        ImportDesktopWifi();
        wifi.SetEventCallback([this](WifiEvent event, const std::string& data) {
            switch (event) {
                case WifiEvent::Scanning:
                    OnNetworkEvent(NetworkEvent::Scanning);
                    break;
                case WifiEvent::Connecting:
                    OnNetworkEvent(NetworkEvent::Connecting, data);
                    break;
                case WifiEvent::Connected:
                    OnNetworkEvent(NetworkEvent::Connected, data);
                    break;
                case WifiEvent::Disconnected:
                    OnNetworkEvent(NetworkEvent::Disconnected);
                    break;
                default:
                    break;
            }
        });
        if (SsidManager::GetInstance().GetSsidList().empty()) {
            display_->ShowNotification("请退出，在桌面设置中连接 Wi-Fi", 30000);
            return;
        }
        // No AP provisioning or timeout fallback: credentials belong to app0.
        wifi.StartStation();
    }

    AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec codec(
            i2c_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, ES8311_CODEC_DEFAULT_ADDR, true, true);
        return &codec;
    }
    Display* GetDisplay() override { return display_; }
    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, false);
        return &backlight;
    }
    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        auto reading = battery_.Get();
        level = reading.percent;
        charging = false;
        discharging = false;
        return reading.valid;
    }
};

DECLARE_BOARD(YiyiGrokbotBoard);
