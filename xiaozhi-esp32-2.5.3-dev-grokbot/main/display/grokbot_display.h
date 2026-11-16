#pragma once

#include "grokbot_player.h"
#include "lcd_display.h"

#include <atomic>
#include <cstdint>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <miniz.h>

class GrokbotDisplay : public SpiLcdDisplay {
public:
    GrokbotDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy, gpio_num_t te_pin = GPIO_NUM_NC, int normal_fps = 30,
                   int buffer_lines = 80);
    ~GrokbotDisplay() override;

    void SetupUI() override;
    bool SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetPowerSaveMode(bool on) override;

private:
    static void RounderEventCb(lv_event_t* event);
    static void AnimationTaskEntry(void* arg);
    static void TeIsr(void* arg);
    void AnimationTask();
    void InitializeTe();
    bool WaitForTeFrame(int fps);

    bool EnsurePlayer();
    bool DecodeFrame(const GrokBotPlayer::FrameInfo& frame, bool expand);
    bool RebuildCurrentFrame();
    void ExpandPackedFrame();
    bool PresentFrame();
    const char* EffectiveAnimationName();
    int64_t NowUs() const;

    GrokBotPlayer player_;
    const uint8_t* atlas_data_ = nullptr;
    size_t atlas_size_ = 0;
    bool player_ready_ = false;
    int64_t next_atlas_check_us_ = 0;

    uint8_t* packed_ = nullptr;
    uint8_t* delta_ = nullptr;
    tinfl_decompressor* decompressor_ = nullptr;
    uint16_t* image_buffers_[2] = {};
    lv_image_dsc_t image_descs_[2] = {};
    uint32_t pair_lut_[256] = {};
    int frame_width_ = 0;
    int frame_height_ = 0;
    size_t packed_size_ = 0;
    size_t image_size_ = 0;
    int write_buffer_ = 0;

    bool reverse_ = false;
    bool rebuild_pending_ = false;
    bool has_selected_ = false;
    gpio_num_t te_pin_ = GPIO_NUM_NC;
    SemaphoreHandle_t te_semaphore_ = nullptr;
    bool te_isr_installed_ = false;
    std::atomic<uint32_t> te_timeout_count_{0};
    std::atomic<uint32_t> te_edge_count_{0};
    std::atomic<uint32_t> te_wait_count_{0};
    std::atomic<int64_t> te_wait_us_total_{0};
    uint32_t last_te_present_count_ = 0;

    std::atomic<const char*> requested_status_{"idle"};
    std::atomic<const char*> requested_emotion_{nullptr};
    std::atomic<int64_t> notification_until_us_{0};
    std::atomic<bool> assets_update_in_progress_{false};
    std::atomic<bool> atlas_refresh_required_{false};
    std::atomic<int> target_fps_{30};
    int normal_fps_ = 30;
    std::atomic<bool> running_{false};
    int64_t decode_us_total_ = 0;
    int64_t present_us_total_ = 0;
    TaskHandle_t task_handle_ = nullptr;
};
