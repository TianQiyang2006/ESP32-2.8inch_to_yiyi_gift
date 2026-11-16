#include "wifi_board.h"
#include "audio/codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "desktop_bridge.h"
#include "yiyi_touch_transform.h"
#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>
#include <driver/spi_master.h>
#include <driver/i2c_master.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <nvs.h>
#include <wifi_manager.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <initializer_list>
#include <cstdio>
#include <atomic>
#include <functional>
#include <utility>

class YiyiLcdDisplay : public SpiLcdDisplay {
    std::function<void()> setup_complete_;

public:
    YiyiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy, std::function<void()> setup_complete)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                        swap_xy),
          setup_complete_(std::move(setup_complete)) {}

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        if (setup_complete_) {
            auto setup_complete = std::move(setup_complete_);
            setup_complete();
        }
    }
};

class YiyiBoard : public WifiBoard {
    bool UsesDesktopWifi() const override { return true; }
    i2c_master_bus_handle_t i2c_ = nullptr;
    i2c_master_dev_handle_t touch_ = nullptr;
    SpiLcdDisplay* display_ = nullptr;
    TouchTransform::Affine affine_;
    uint8_t touch_mode_ = 0;
    lv_obj_t* exit_button_ = nullptr;
    std::atomic<int> injected_phase_{0};
    int injected_x_ = 0, injected_y_ = 0;

    void AddDesktopControls() {
        if (!lvgl_port_lock(1000)) {
            ESP_LOGW("YiyiBoard", "Unable to lock LVGL to add desktop controls");
            return;
        }

        auto screen = lv_screen_active();
        exit_button_ = lv_button_create(screen);
        lv_obj_set_size(exit_button_, 60, 32);
        lv_obj_align(exit_button_, LV_ALIGN_TOP_RIGHT, -6, 32);
        lv_obj_set_style_radius(exit_button_, 8, 0);
        lv_obj_set_style_bg_color(exit_button_, lv_color_hex(0xeeeeee), 0);
        lv_obj_set_style_bg_opa(exit_button_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(exit_button_, lv_color_hex(0x333333), 0);
        LV_FONT_DECLARE(yiyi_exit_font_20);
        lv_obj_set_style_text_font(exit_button_, &yiyi_exit_font_20, 0);
        auto label = lv_label_create(exit_button_);
        lv_label_set_text(label, "退出");
        lv_obj_center(label);
        lv_obj_add_event_cb(exit_button_, [](lv_event_t*) {
            Application::GetInstance().Schedule([] { ExitToDesktop(); });
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_move_foreground(exit_button_);

        lv_obj_add_event_cb(screen, [](lv_event_t*) {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) app.ToggleChatState();
        }, LV_EVENT_CLICKED, nullptr);
        lvgl_port_unlock();
    }

    void InitializeI2c() {
        i2c_master_bus_config_t config = {};
        config.i2c_port = I2C_NUM_0;
        config.sda_io_num = GPIO_NUM_16;
        config.scl_io_num = GPIO_NUM_15;
        config.clk_source = I2C_CLK_SRC_DEFAULT;
        config.glitch_ignore_cnt = 7;
        config.flags.enable_internal_pullup = true;
        ESP_ERROR_CHECK(i2c_new_master_bus(&config, &i2c_));
        gpio_config_t reset = {};
        reset.pin_bit_mask = 1ULL << 18;
        reset.mode = GPIO_MODE_OUTPUT;
        ESP_ERROR_CHECK(gpio_config(&reset));
        gpio_set_level(GPIO_NUM_18, 0);
        vTaskDelay(pdMS_TO_TICKS(15));
        gpio_set_level(GPIO_NUM_18, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = 0x38;
        device.scl_speed_hz = 100000;
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_, &device, &touch_));
    }

    void InitializeDisplay() {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = 11; bus.miso_io_num = 13; bus.sclk_io_num = 12;
        bus.quadwp_io_num = -1; bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 320 * 240 * 2;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
        esp_lcd_panel_io_spi_config_t io = {};
        io.cs_gpio_num = GPIO_NUM_10; io.dc_gpio_num = GPIO_NUM_46;
        io.pclk_hz = 20000000; io.trans_queue_depth = 10;
        io.lcd_cmd_bits = 8; io.lcd_param_bits = 8;
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
        // Match the electrical setup already verified on this ES3C28P panel.
        auto command = [panel_io](int cmd, std::initializer_list<uint8_t> bytes) {
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io, cmd, bytes.begin(), bytes.size()));
        };
        command(0xef, {0x03,0x80,0x02}); command(0xcf, {0x00,0xc1,0x30});
        command(0xed, {0x64,0x03,0x12,0x81}); command(0xe8, {0x85,0x00,0x78});
        command(0xcb, {0x39,0x2c,0x00,0x34,0x02}); command(0xf7, {0x20});
        command(0xea, {0x00,0x00}); command(0xc0, {0x23}); command(0xc1, {0x10});
        command(0xc5, {0x3e,0x28}); command(0xc7, {0x86});
        command(0xb1, {0x00,0x13}); command(0xb6, {0x08,0x82,0x27});
        command(0xf2, {0x00}); command(0x26, {0x01});
        command(0xe0, {0x0f,0x31,0x2b,0x0c,0x0e,0x08,0x4e,0xf1,0x37,0x07,0x10,0x03,0x0e,0x09,0x00});
        command(0xe1, {0x00,0x0e,0x14,0x03,0x11,0x07,0x31,0xc1,0x48,0x08,0x0f,0x0c,0x31,0x36,0x0f});
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, false));
        display_ = new YiyiLcdDisplay(panel_io, panel, 320, 240, 0, 0, false, false, true,
                                      [this] { AddDesktopControls(); });
    }

    void InitializeTouch() {
        affine_.valid = true;
        nvs_handle_t shared;
        if (nvs_open("yiyi-desk", NVS_READONLY, &shared) == ESP_OK) {
            uint8_t calibrated = 0, version = 0;
            nvs_get_u8(shared, "affok", &calibrated);
            nvs_get_u8(shared, "affver", &version);
            nvs_get_u8(shared, "touch", &touch_mode_);
            if (calibrated && version == 2) {
                TouchTransform::Affine stored;
                const char* keys[] = {"affax","affbx","affcx","affay","affby","affcy"};
                float* values[] = {&stored.ax,&stored.bx,&stored.cx,&stored.ay,&stored.by,&stored.cy};
                bool valid = true;
                for (int i = 0; i < 6; ++i) {
                    size_t length = sizeof(float);
                    valid &= nvs_get_blob(shared, keys[i], values[i], &length) == ESP_OK && length == sizeof(float);
                }
                if (valid && TouchTransform::finite(stored)) { stored.valid = true; affine_ = stored; }
            }
            nvs_close(shared);
        }
        lvgl_port_lock(0);
        auto indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(indev, this);
        lv_indev_set_read_cb(indev, [](lv_indev_t* input, lv_indev_data_t* data) {
            auto self = static_cast<YiyiBoard*>(lv_indev_get_user_data(input));
            int phase = self->injected_phase_.load();
            if (phase) {
                data->point = {self->injected_x_, self->injected_y_};
                data->state = phase == 2 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
                self->injected_phase_.store(phase - 1);
                return;
            }
            uint8_t reg = 0x02, bytes[5] = {};
            data->state = LV_INDEV_STATE_RELEASED;
            if (i2c_master_transmit_receive(self->touch_, &reg, 1, bytes, sizeof(bytes), 20) != ESP_OK || !(bytes[0] & 0x0f)) return;
            int rx = ((bytes[1]&0x0f)<<8)|bytes[2];
            int ry = ((bytes[3]&0x0f)<<8)|bytes[4];
            int16_t x, y;
            if (!TouchTransform::map(self->affine_, rx, ry, x, y)) return;
            if (self->touch_mode_ & 1) x = 319 - x;
            if (self->touch_mode_ & 2) y = 239 - y;
            data->point = {x,y}; data->state = LV_INDEV_STATE_PRESSED;
        });
        lvgl_port_unlock();
    }

    void InitializeDiagnostics() {
        usb_serial_jtag_driver_config_t config = {};
        config.tx_buffer_size = 4096; config.rx_buffer_size = 256;
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
        usb_serial_jtag_vfs_use_driver();
        xTaskCreate([](void* argument) {
            auto self = static_cast<YiyiBoard*>(argument);
            std::string command;
            while (true) {
                char ch;
                if (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100)) <= 0) continue;
                if (ch == '\r') continue;
                if (ch != '\n') { if (command.size() < 80) command += ch; continue; }
                auto& app = Application::GetInstance();
                if (command == "status") {
                    auto& audio = app.GetAudioService();
                    auto stats = audio.GetDebugStatistics();
                    auto& wifi = WifiManager::GetInstance();
                    printf("{\"implementation\":\"official-2.5.0\",\"uptimeMs\":%u,\"state\":%d,\"wifi\":%s,\"wifiConfigMode\":%s,\"desktopWifi\":true,\"heapFree\":%u,\"psramFree\":%u,\"touchCalibrated\":%s,\"processing\":%s,\"vad\":%s,\"micRms\":%u,\"inputFrames\":%u,\"encodedFrames\":%u,\"decodedFrames\":%u,\"playedFrames\":%u}\n",
                        unsigned(esp_timer_get_time()/1000), int(app.GetDeviceState()), wifi.IsConnected() ? "true" : "false",
                        wifi.IsConfigMode() ? "true" : "false", unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), self->affine_.valid ? "true" : "false",
                        audio.IsAudioProcessorRunning() ? "true" : "false", audio.IsVoiceDetected() ? "true" : "false", unsigned(audio.GetMicrophoneRms()),
                        unsigned(stats.input_count), unsigned(stats.encode_count), unsigned(stats.decode_count), unsigned(stats.playback_count));
                } else if (command == "back") {
                    app.Schedule([] { ExitToDesktop(); });
                } else if (command == "talk" && app.GetDeviceState() == kDeviceStateIdle) {
                    app.ToggleChatState();
                } else if (command.starts_with("tap ")) {
                    int x, y;
                    if (sscanf(command.c_str(), "tap %d %d", &x, &y) == 2 &&
                        x >= 0 && x < 320 && y >= 0 && y < 240 && self->injected_phase_.load() == 0) {
                        self->injected_x_ = x; self->injected_y_ = y;
                        self->injected_phase_.store(2);
                    }
                } else if (command == "screenshot") {
                    std::string jpeg;
                    esp_log_level_set("*", ESP_LOG_NONE);
                    if (self->display_->SnapshotToJpeg(jpeg, 90)) {
                        printf("JPEG %u\n", unsigned(jpeg.size())); fflush(stdout);
                        // Each write must fit the USB driver's 4 KB ring buffer.
                        for (size_t offset = 0; offset < jpeg.size();) {
                            size_t size = std::min(size_t(512), jpeg.size() - offset);
                            int written = usb_serial_jtag_write_bytes(jpeg.data() + offset, size, pdMS_TO_TICKS(5000));
                            if (written <= 0) break;
                            offset += written;
                        }
                        printf("\nEND\n"); fflush(stdout);
                    }
                    esp_log_level_set("*", ESP_LOG_INFO);
                }
                command.clear();
            }
        }, "desktop_diag", 4096, this, 1, nullptr);
    }

public:
    YiyiBoard() {
        InitializeI2c(); InitializeDisplay(); InitializeTouch();
        GetBacklight()->RestoreBrightness(); InitializeDiagnostics();
    }
    AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec codec(i2c_, I2C_NUM_0, 16000, 16000,
            GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_6,
            GPIO_NUM_1, ES8311_CODEC_DEFAULT_ADDR, true, true);
        return &codec;
    }
    Display* GetDisplay() override { return display_; }
    Backlight* GetBacklight() override {
        static PwmBacklight backlight(GPIO_NUM_45, false);
        return &backlight;
    }
};
DECLARE_BOARD(YiyiBoard);
