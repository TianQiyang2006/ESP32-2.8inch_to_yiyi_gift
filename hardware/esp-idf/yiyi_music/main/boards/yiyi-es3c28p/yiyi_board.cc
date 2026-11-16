#include "wifi_board.h"
#include "audio/codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "device_state.h"
#include "backlight.h"
#include "desktop_bridge.h"
#include "yiyi_touch_transform.h"
#include "esp32_music.h"
#include "music_companion.h"
#include "config.h"
#include "yiyi_battery.h"

#include <esp_lcd_ili9341.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lvgl_port.h>
#include <driver/spi_master.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <freertos/task.h>
#include <nvs.h>
#include <wifi_station.h>
#include <settings.h>
#include <font_emoji.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define TAG "YiyiBoard"

static bool StartsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
LV_FONT_DECLARE(yiyi_exit_font_20);

class YiyiBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_ = nullptr;
    i2c_master_dev_handle_t touch_ = nullptr;
    SpiLcdDisplay* display_ = nullptr;
    TouchTransform::Affine affine_;
    uint8_t touch_mode_ = 0;
    lv_obj_t* exit_button_ = nullptr;
    YiyiBattery battery_;
    lv_obj_t* battery_notice_=nullptr;
    lv_obj_t* music_bar_ = nullptr;
    lv_obj_t* music_title_ = nullptr;
    lv_obj_t* music_pause_label_ = nullptr;
    lv_obj_t* playlist_entry_ = nullptr;
    lv_obj_t* playlist_popup_ = nullptr;
    lv_obj_t* playlist_list_ = nullptr;
    lv_obj_t* playlist_status_ = nullptr;
    std::atomic<bool> playlist_fetching_{false};
    std::atomic<bool> remote_action_pending_{false};
    std::string music_ui_title_;
    bool music_ui_playing_ = false;
    bool music_ui_paused_ = false;
    std::atomic<int> injected_phase_{0};
    int injected_x_ = 0;
    int injected_y_ = 0;

    bool UsesDesktopWifi() const override { return true; }

    void ClearBatteryNotice() {
        if(!lvgl_port_lock(1000))return;
        if(battery_notice_){lv_obj_delete(battery_notice_);battery_notice_=nullptr;}
        lvgl_port_unlock();
    }

    void ShowBatteryNotice(BatteryPolicy::Notice notice) {
        if(!lvgl_port_lock(1000))return;
        if(battery_notice_)lv_obj_delete(battery_notice_);
        battery_notice_=lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(battery_notice_);lv_obj_set_size(battery_notice_,320,240);
        lv_obj_set_style_bg_color(battery_notice_,lv_color_black(),0);
        lv_obj_set_style_bg_opa(battery_notice_,LV_OPA_50,0);
        lv_obj_remove_flag(battery_notice_,LV_OBJ_FLAG_SCROLLABLE);
        auto *card=lv_obj_create(battery_notice_);lv_obj_set_size(card,280,150);lv_obj_center(card);
        lv_obj_remove_flag(card,LV_OBJ_FLAG_SCROLLABLE);
        auto *label=lv_label_create(card);lv_obj_set_width(label,248);
        lv_obj_set_style_text_font(label,&font_puhui_20_4,0);
        const char *message=notice==BatteryPolicy::Notice::HighVoltage?"电压偏高\n请断开充电并检查":notice==BatteryPolicy::Notice::SensorFault?"电压读取异常\n请检查电池连接":"电量不足\n请使用外置模块充电";
        lv_label_set_text(label,message);
        auto *close=lv_button_create(card);lv_obj_set_size(close,100,36);lv_obj_align(close,LV_ALIGN_BOTTOM_MID,0,0);
        auto *text=lv_label_create(close);lv_obj_set_style_text_font(text,&font_puhui_20_4,0);
        lv_label_set_text(text,"确定");lv_obj_center(text);
        lv_obj_add_event_cb(close,[](lv_event_t *event){
            auto self=static_cast<YiyiBoard*>(lv_event_get_user_data(event));
            if(self->battery_notice_){lv_obj_delete(self->battery_notice_);self->battery_notice_=nullptr;}
        },LV_EVENT_CLICKED,this);
        lvgl_port_unlock();
    }

    void AddDesktopControls() {
        if (!lvgl_port_lock(1000)) {
            ESP_LOGW(TAG, "Unable to lock LVGL to add desktop controls");
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
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.ToggleChatState();
            }
        }, LV_EVENT_CLICKED, nullptr);
        lvgl_port_unlock();
    }

    void AddMusicControls() {
        if (!lvgl_port_lock(1000)) return;
        auto screen = lv_layer_top();

        music_bar_ = lv_obj_create(screen);
        lv_obj_add_flag(music_bar_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(music_bar_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(music_bar_, DISPLAY_WIDTH - 8, 76);
        lv_obj_align(music_bar_, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_pad_all(music_bar_, 4, 0);
        lv_obj_set_style_pad_row(music_bar_, 2, 0);
        lv_obj_set_style_radius(music_bar_, 6, 0);
        lv_obj_set_style_bg_color(music_bar_, lv_color_hex(0xf5f6f4), 0);
        lv_obj_set_style_bg_opa(music_bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(music_bar_, lv_color_hex(0x9aa7a2), 0);
        lv_obj_set_style_border_width(music_bar_, 1, 0);
        lv_obj_set_style_text_font(music_bar_, &font_puhui_20_4, 0);
        lv_obj_set_style_text_color(music_bar_, lv_color_hex(0x263e36), 0);
        lv_obj_set_flex_flow(music_bar_, LV_FLEX_FLOW_COLUMN);

        music_title_ = lv_label_create(music_bar_);
        lv_obj_set_width(music_title_, DISPLAY_WIDTH - 24);
        lv_obj_set_height(music_title_, 23);
        lv_obj_set_style_text_align(music_title_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(music_title_, LV_LABEL_LONG_DOT);
        lv_label_set_text(music_title_, "");

        auto row = lv_obj_create(music_bar_);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, DISPLAY_WIDTH - 24, 40);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 5, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto make_button = [row](int width, const char* text, lv_color_t color) {
            auto button = lv_button_create(row);
            lv_obj_set_size(button, width, 38);
            lv_obj_set_style_pad_all(button, 0, 0);
            lv_obj_set_style_radius(button, 4, 0);
            lv_obj_set_style_text_color(button, lv_color_hex(0x263e36), 0);
            lv_obj_set_style_bg_color(button, color, 0);
            lv_obj_set_style_bg_color(button, lv_color_hex(0x1f6b5c), LV_STATE_PRESSED);
            auto label = lv_label_create(button);
            lv_obj_center(label);
            lv_label_set_text(label, text);
            return button;
        };

        auto previous = make_button(60, LV_SYMBOL_PREV, lv_color_hex(0xe3e8e5));
        lv_obj_set_style_text_font(previous, &font_awesome_20_4, 0);
        lv_obj_add_event_cb(previous, [](lv_event_t* event) {
            static_cast<YiyiBoard*>(lv_event_get_user_data(event))->QueueRemoteAction("previous");
        }, LV_EVENT_CLICKED, this);

        auto pause = make_button(60, LV_SYMBOL_PAUSE, lv_color_hex(0x247565));
        lv_obj_set_style_text_font(pause, &font_awesome_20_4, 0);
        lv_obj_set_style_text_color(pause, lv_color_white(), 0);
        music_pause_label_ = lv_obj_get_child(pause, 0);
        lv_obj_add_event_cb(pause, [](lv_event_t* event) {
            auto self = static_cast<YiyiBoard*>(lv_event_get_user_data(event));
            auto music = Board::GetInstance().GetMusic();
            if (!music || !music->IsPlaying()) return;
            bool paused = music->IsPaused();
            if (paused ? music->ResumeStreaming() : music->PauseStreaming()) {
                lv_label_set_text(self->music_pause_label_, paused ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
            }
        }, LV_EVENT_CLICKED, this);

        auto next = make_button(60, LV_SYMBOL_NEXT, lv_color_hex(0xe3e8e5));
        lv_obj_set_style_text_font(next, &font_awesome_20_4, 0);
        lv_obj_add_event_cb(next, [](lv_event_t* event) {
            static_cast<YiyiBoard*>(lv_event_get_user_data(event))->QueueRemoteAction("next");
        }, LV_EVENT_CLICKED, this);

        auto playlist = make_button(92, "歌单", lv_color_hex(0x2d6f67));
        lv_obj_set_style_text_color(playlist, lv_color_white(), 0);
        lv_obj_add_event_cb(playlist, [](lv_event_t* event) {
            static_cast<YiyiBoard*>(lv_event_get_user_data(event))->ShowPlaylistChooser();
        }, LV_EVENT_CLICKED, this);

        playlist_entry_ = lv_button_create(screen);
        lv_obj_set_size(playlist_entry_, 120, 38);
        lv_obj_align(playlist_entry_, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_pad_all(playlist_entry_, 0, 0);
        lv_obj_set_style_radius(playlist_entry_, 6, 0);
        lv_obj_set_style_bg_color(playlist_entry_, lv_color_hex(0x2d6f67), 0);
        lv_obj_set_style_text_color(playlist_entry_, lv_color_white(), 0);
        lv_obj_set_style_text_font(playlist_entry_, &font_puhui_20_4, 0);
        auto entry_label = lv_label_create(playlist_entry_);
        lv_label_set_text(entry_label, "播放歌单");
        lv_obj_center(entry_label);
        lv_obj_add_event_cb(playlist_entry_, [](lv_event_t* event) {
            static_cast<YiyiBoard*>(lv_event_get_user_data(event))->ShowPlaylistChooser();
        }, LV_EVENT_CLICKED, this);

        playlist_popup_ = lv_obj_create(screen);
        lv_obj_add_flag(playlist_popup_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(playlist_popup_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(playlist_popup_, DISPLAY_WIDTH - 20, DISPLAY_HEIGHT - 32);
        lv_obj_center(playlist_popup_);
        lv_obj_set_style_pad_all(playlist_popup_, 8, 0);
        lv_obj_set_style_radius(playlist_popup_, 6, 0);
        lv_obj_set_style_bg_color(playlist_popup_, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(playlist_popup_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(playlist_popup_, lv_color_hex(0x65736f), 0);
        lv_obj_set_style_border_width(playlist_popup_, 1, 0);
        lv_obj_set_style_text_font(playlist_popup_, &font_puhui_20_4, 0);
        lv_obj_set_style_text_color(playlist_popup_, lv_color_hex(0x263e36), 0);

        auto popup_header = lv_obj_create(playlist_popup_);
        lv_obj_remove_flag(popup_header, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(popup_header, DISPLAY_WIDTH - 40, 34);
        lv_obj_set_style_pad_all(popup_header, 0, 0);
        lv_obj_set_style_bg_opa(popup_header, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(popup_header, 0, 0);
        lv_obj_set_flex_flow(popup_header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(popup_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(popup_header, LV_ALIGN_TOP_MID, 0, 0);
        auto heading = lv_label_create(popup_header);
        lv_label_set_text(heading, "播放歌单");
        auto close = lv_button_create(popup_header);
        lv_obj_set_size(close, 36, 30);
        lv_obj_set_style_pad_all(close, 0, 0);
        lv_obj_set_style_radius(close, 4, 0);
        auto close_label = lv_label_create(close);
        lv_label_set_text(close_label, "X");
        lv_obj_center(close_label);
        lv_obj_add_event_cb(close, [](lv_event_t* event) {
            auto self = static_cast<YiyiBoard*>(lv_event_get_user_data(event));
            lv_obj_add_flag(self->playlist_popup_, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI(TAG, "Playlist chooser closed");
        }, LV_EVENT_CLICKED, this);

        playlist_status_ = lv_label_create(playlist_popup_);
        lv_obj_set_width(playlist_status_, DISPLAY_WIDTH - 44);
        lv_obj_align(playlist_status_, LV_ALIGN_TOP_LEFT, 0, 38);
        lv_label_set_text(playlist_status_, "");

        playlist_list_ = lv_obj_create(playlist_popup_);
        lv_obj_set_size(playlist_list_, DISPLAY_WIDTH - 40, DISPLAY_HEIGHT - 112);
        lv_obj_align(playlist_list_, LV_ALIGN_TOP_LEFT, 0, 62);
        lv_obj_set_scroll_dir(playlist_list_, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(playlist_list_, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_pad_all(playlist_list_, 0, 0);
        lv_obj_set_style_pad_row(playlist_list_, 4, 0);
        lv_obj_set_style_bg_opa(playlist_list_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(playlist_list_, 0, 0);
        lv_obj_set_flex_flow(playlist_list_, LV_FLEX_FLOW_COLUMN);
        lvgl_port_unlock();
    }

    void ShowPlaylistChooser() {
        if (!playlist_popup_) return;
        lv_obj_clear_flag(playlist_popup_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(playlist_popup_);
        ESP_LOGI(TAG, "Playlist chooser opened");
        if (playlist_fetching_.exchange(true)) return;
        lv_label_set_text(playlist_status_, "正在获取歌单...");
        lv_obj_clean(playlist_list_);
        auto result = xTaskCreate([](void* argument) {
          {
            auto self = static_cast<YiyiBoard*>(argument);
            auto response = MusicCompanion::GetInstance().ListPlaylists();
            std::vector<std::pair<std::string, int>> playlists;
            cJSON* root = cJSON_Parse(response.c_str());
            auto items = root ? cJSON_GetObjectItem(root, "playlists") : nullptr;
            if (cJSON_IsArray(items)) {
                cJSON* item = nullptr;
                cJSON_ArrayForEach(item, items) {
                    auto name = cJSON_GetObjectItem(item, "name");
                    auto count = cJSON_GetObjectItem(item, "count");
                    if (cJSON_IsString(name) && cJSON_IsNumber(count)) {
                        playlists.emplace_back(name->valuestring, count->valueint);
                    }
                }
            }
            bool valid = cJSON_IsArray(items) && cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));
            cJSON_Delete(root);

            if (lvgl_port_lock(1000)) {
                lv_obj_clean(self->playlist_list_);
                if (!valid) {
                    lv_label_set_text(self->playlist_status_, "暂时无法获取歌单");
                } else if (playlists.empty()) {
                    lv_label_set_text(self->playlist_status_, "还没有已保存的歌单");
                } else {
                    lv_label_set_text(self->playlist_status_, "点选歌单，顺序播放");
                    for (const auto& entry : playlists) {
                        auto row = lv_button_create(self->playlist_list_);
                        lv_obj_set_size(row, lv_pct(100), 40);
                        lv_obj_set_style_pad_all(row, 6, 0);
                        lv_obj_set_style_radius(row, 4, 0);
                        lv_obj_set_style_bg_color(row, lv_color_hex(0xe8eeeb), 0);
                        lv_obj_set_style_text_color(row, lv_color_hex(0x263e36), 0);
                        auto label = lv_label_create(row);
                        lv_label_set_text_fmt(label, "%s  %d首", entry.first.c_str(), entry.second);
                        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
                        auto name = new std::string(entry.first);
                        lv_obj_set_width(label, lv_pct(100));
                        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
                        lv_obj_set_user_data(row, name);
                        lv_obj_add_event_cb(row, [](lv_event_t* event) {
                            auto row = static_cast<lv_obj_t*>(lv_event_get_target(event));
                            auto code = lv_event_get_code(event);
                            auto name = static_cast<std::string*>(lv_obj_get_user_data(row));
                            if (code == LV_EVENT_DELETE) {
                                delete name;
                            } else if (code == LV_EVENT_CLICKED && name) {
                                static_cast<YiyiBoard*>(lv_event_get_user_data(event))->QueuePlaylist(*name);
                            }
                        }, LV_EVENT_ALL, self);
                    }
                }
                lvgl_port_unlock();
            }
            ESP_LOGI(TAG, "Playlist chooser loaded: ok=%d count=%u", valid, unsigned(playlists.size()));
            self->playlist_fetching_ = false;
          }
            vTaskDelete(nullptr);
        }, "playlist_list", 6144, this, 2, nullptr);
        if (result != pdPASS) {
            playlist_fetching_ = false;
            lv_label_set_text(playlist_status_, "内存不足，请稍后重试");
        }
    }

    void QueuePlaylist(const std::string& name) {
        if (remote_action_pending_.exchange(true)) return;
        lv_label_set_text(playlist_status_, "正在加入播放队列...");
        struct RequestContext { YiyiBoard* board; std::string name; };
        auto context = new RequestContext{this, name};
        auto result = xTaskCreate([](void* argument) {
          {
            std::unique_ptr<RequestContext> context(static_cast<RequestContext*>(argument));
            auto response = MusicCompanion::GetInstance().PlayPlaylist(context->name);
            cJSON* root = cJSON_Parse(response.c_str());
            bool ok = root && cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));
            auto message = root ? cJSON_GetObjectItem(root, "message") : nullptr;
            std::string error = cJSON_IsString(message) ? message->valuestring : "暂时无法开始播放";
            cJSON_Delete(root);
            auto self = context->board;
            if (lvgl_port_lock(500)) {
                if (ok) {
                    lv_obj_add_flag(self->playlist_popup_, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_label_set_text(self->playlist_status_, error.c_str());
                }
                lvgl_port_unlock();
            }
            self->remote_action_pending_ = false;
            ESP_LOGI(TAG, "Playlist play submitted: ok=%d", ok);
          }
            vTaskDelete(nullptr);
        }, "playlist_play", 6144, context, 2, nullptr);
        if (result != pdPASS) {
            delete context;
            remote_action_pending_ = false;
            lv_label_set_text(playlist_status_, "内存不足，请稍后重试");
        }
    }

    void QueueRemoteAction(const std::string& action) {
        if (remote_action_pending_.exchange(true)) return;
        struct RequestContext { YiyiBoard* board; std::string action; };
        auto context = new RequestContext{this, action};
        auto result = xTaskCreate([](void* argument) {
          {
            std::unique_ptr<RequestContext> context(static_cast<RequestContext*>(argument));
            auto response = MusicCompanion::GetInstance().PlayerControl(context->action);
            cJSON* root = cJSON_Parse(response.c_str());
            bool ok = root && cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));
            auto message = root ? cJSON_GetObjectItem(root, "message") : nullptr;
            std::string notice = ok ? (context->action == "next" ? "正在切到下一首" : "正在切到上一首") :
                (cJSON_IsString(message) ? message->valuestring : "暂时无法切歌，请重试");
            cJSON_Delete(root);
            auto self = context->board;
            Application::GetInstance().Schedule([self, notice]() {
                self->display_->ShowNotification(notice.c_str(), 1800);
            });
            self->remote_action_pending_ = false;
            ESP_LOGI(TAG, "Touch player control submitted: action=%s ok=%d", context->action.c_str(), ok);
          }
            vTaskDelete(nullptr);
        }, "music_control", 6144, context, 2, nullptr);
        if (result != pdPASS) {
            delete context;
            remote_action_pending_ = false;
        }
    }

    void UpdateMusicControls() {
        if (!music_bar_ || !playlist_popup_) return;
        auto player = static_cast<Esp32Music*>(GetMusic());
        bool playing = player && player->IsPlaying();
        bool paused = player && player->IsPaused();
        std::string title;
        if (playing) {
            cJSON* status = player->PlaybackStatus();
            auto field = cJSON_GetObjectItem(status, "title");
            if (cJSON_IsString(field)) title = field->valuestring;
            cJSON_Delete(status);
        }
        if (playing == music_ui_playing_ && paused == music_ui_paused_ && title == music_ui_title_) return;
        if (!lvgl_port_lock(100)) return;
        music_ui_playing_ = playing;
        music_ui_paused_ = paused;
        if (!title.empty()) music_ui_title_ = title;
        if (playing) {
            lv_obj_add_flag(playlist_entry_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(music_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(music_title_, music_ui_title_.c_str());
            lv_label_set_text(music_pause_label_, paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
        } else {
            music_ui_title_.clear();
            lv_obj_add_flag(music_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(playlist_entry_, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
    }

    static void MusicUiTask(void* argument) {
        auto self = static_cast<YiyiBoard*>(argument);
        vTaskDelay(pdMS_TO_TICKS(5000));
        for (;;) {
            self->UpdateMusicControls();
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    void InitializeI2c() {
        i2c_master_bus_config_t config = {};
        config.i2c_port = I2C_NUM_0;
        config.sda_io_num = AUDIO_CODEC_I2C_SDA_PIN;
        config.scl_io_num = AUDIO_CODEC_I2C_SCL_PIN;
        config.clk_source = I2C_CLK_SRC_DEFAULT;
        config.glitch_ignore_cnt = 7;
        config.flags.enable_internal_pullup = true;
        ESP_ERROR_CHECK(i2c_new_master_bus(&config, &i2c_));

        gpio_config_t reset = {};
        reset.pin_bit_mask = 1ULL << TOUCH_RST_PIN;
        reset.mode = GPIO_MODE_OUTPUT;
        ESP_ERROR_CHECK(gpio_config(&reset));
        gpio_set_level(TOUCH_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(15));
        gpio_set_level(TOUCH_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(150));

        i2c_device_config_t device = {};
        device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device.device_address = TOUCH_I2C_ADDR;
        device.scl_speed_hz = 100000;
        ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_, &device, &touch_));
    }

    void InitializeDisplay() {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = DISPLAY_MOSI_PIN;
        bus.miso_io_num = DISPLAY_MISO_PIN;
        bus.sclk_io_num = DISPLAY_CLK_PIN;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2;
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
        // ES3C28P 面板已在本项目桌面固件上验证的初始化序列
        auto command = [panel_io](int cmd, std::initializer_list<uint8_t> bytes) {
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io, cmd, bytes.begin(), bytes.size()));
        };
        command(0xef, {0x03, 0x80, 0x02}); command(0xcf, {0x00, 0xc1, 0x30});
        command(0xed, {0x64, 0x03, 0x12, 0x81}); command(0xe8, {0x85, 0x00, 0x78});
        command(0xcb, {0x39, 0x2c, 0x00, 0x34, 0x02}); command(0xf7, {0x20});
        command(0xea, {0x00, 0x00}); command(0xc0, {0x23}); command(0xc1, {0x10});
        command(0xc5, {0x3e, 0x28}); command(0xc7, {0x86});
        command(0xb1, {0x00, 0x13}); command(0xb6, {0x08, 0x82, 0x27});
        command(0xf2, {0x00}); command(0x26, {0x01});
        command(0xe0, {0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00});
        command(0xe1, {0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f});
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
            {
                .text_font = &font_puhui_20_4,
                .icon_font = &font_awesome_20_4,
                .emoji_font = font_emoji_64_init(),
            });
        AddDesktopControls();
        AddMusicControls();
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
                const char* keys[] = {"affax", "affbx", "affcx", "affay", "affby", "affcy"};
                float* values[] = {&stored.ax, &stored.bx, &stored.cx, &stored.ay, &stored.by, &stored.cy};
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
            int rx = ((bytes[1] & 0x0f) << 8) | bytes[2];
            int ry = ((bytes[3] & 0x0f) << 8) | bytes[4];
            int16_t x, y;
            if (!TouchTransform::map(self->affine_, rx, ry, x, y)) return;
            if (self->touch_mode_ & 1) x = DISPLAY_WIDTH - 1 - x;
            if (self->touch_mode_ & 2) y = DISPLAY_HEIGHT - 1 - y;
            data->point = {x, y};
            data->state = LV_INDEV_STATE_PRESSED;
        });
        lvgl_port_unlock();
    }

    void MusicTask(const std::string& song) {
        auto music = Board::GetInstance().GetMusic();
        if (!music) {
            printf("MUSIC_RESULT success=0 reason=no-music-object\n");
            fflush(stdout);
            return;
        }
        printf("MUSIC_START song=%s\n", song.c_str());
        fflush(stdout);
        bool ok = music->Download(song, "");
        std::string details = music->GetDownloadResult();
        // 只回传前 300 字节，避免串口输出过大
        if (details.size() > 300) details.resize(300);
        printf("MUSIC_RESULT success=%d details=%s\n", ok ? 1 : 0, details.c_str());
        fflush(stdout);
    }

    static void MusicTaskEntry(void* arg) {
        auto* pair = static_cast<std::pair<YiyiBoard*, std::string>*>(arg);
        pair->first->MusicTask(pair->second);
        delete pair;
        vTaskDelete(nullptr);
    }

    void HandleCommand(const std::string& command) {
        auto& app = Application::GetInstance();
        if (command == "status") {
            auto& audio = app.GetAudioService();
            auto& wifi = WifiStation::GetInstance();
            auto music = Board::GetInstance().GetMusic();
            Settings music_settings("music", false);
            std::string music_url = music_settings.GetString("server_url", "http://110.42.59.54:2233");
            std::string ip = wifi.GetIpAddress();
            bool connected = wifi.IsConnected();
            int rssi = connected ? int(wifi.GetRssi()) : 0;
            unsigned uptime_ms = unsigned(esp_timer_get_time() / 1000);
            // sdkconfig uses newlib nano formatting: %llu is unsupported and would corrupt printf args.
            printf("{\"implementation\":\"music-2.5.0\",\"uptimeMs\":%u,\"state\":%d,\"wifi\":%s,\"wifiConfigMode\":%s,\"desktopWifi\":true,\"ip\":\"%s\",\"rssi\":%d,\"heapFree\":%u,\"psramFree\":%u,\"touchCalibrated\":%s,\"processing\":%s,\"vad\":%s,\"wakeWordRunning\":%s,\"music\":%s,\"musicUrl\":\"%s\"}\n",
                uptime_ms, int(app.GetDeviceState()),
                connected ? "true" : "false",
                app.GetDeviceState() == kDeviceStateWifiConfiguring ? "true" : "false",
                ip.c_str(), rssi,
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                affine_.valid ? "true" : "false",
                audio.IsAudioProcessorRunning() ? "true" : "false",
                audio.IsVoiceDetected() ? "true" : "false",
                audio.IsWakeWordRunning() ? "true" : "false",
                music ? "true" : "false", music_url.c_str());
        } else if (command == "power") {
            const auto b=battery_.Get();
            char volts[24]="null",percent[12]="null";
            if(b.valid){snprintf(volts,sizeof(volts),"%d.%03d",b.millivolts/1000,b.millivolts%1000);snprintf(percent,sizeof(percent),"%d",b.percent);}
            printf("{\"installed\":true,\"voltageValid\":%s,\"volts\":%s,\"percent\":%s,\"chargingKnown\":false,\"charging\":null,\"ratedCapacityMah\":%d,\"protection\":{\"state\":\"%s\",\"mode\":\"notify_only\",\"automaticSleep\":false}}\n",
                b.valid?"true":"false",volts,percent,BatteryPolicy::RatedCapacityMah,b.protection.name());
        } else if (StartsWith(command, "volume ")) {
            int volume = -1;
            if (sscanf(command.c_str(), "volume %d", &volume) == 1 && volume >= 0 && volume <= 100) {
                app.Schedule([volume]() { Board::GetInstance().GetAudioCodec()->SetOutputVolume(volume); });
            }
        } else if (command == "musicui") {
            if (music_bar_ && lvgl_port_lock(1000)) {
                lv_obj_update_layout(lv_layer_top());
                auto root = cJSON_CreateObject();
                cJSON_AddBoolToObject(root, "controlsVisible", !lv_obj_has_flag(music_bar_, LV_OBJ_FLAG_HIDDEN));
                cJSON_AddBoolToObject(root, "playlistEntryVisible", !lv_obj_has_flag(playlist_entry_, LV_OBJ_FLAG_HIDDEN));
                cJSON_AddBoolToObject(root, "playlistVisible", !lv_obj_has_flag(playlist_popup_, LV_OBJ_FLAG_HIDDEN));
                cJSON_AddStringToObject(root, "title", lv_label_get_text(music_title_));
                cJSON_AddStringToObject(root, "playlistStatus", lv_label_get_text(playlist_status_));
                auto buttons = cJSON_AddArrayToObject(root, "buttons");
                auto add_button = [buttons](lv_obj_t* button, const char* name) {
                    lv_area_t bounds;
                    lv_obj_get_coords(button, &bounds);
                    auto item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "name", name);
                    cJSON_AddNumberToObject(item, "x", (bounds.x1 + bounds.x2) / 2);
                    cJSON_AddNumberToObject(item, "y", (bounds.y1 + bounds.y2) / 2);
                    cJSON_AddItemToArray(buttons, item);
                };
                add_button(playlist_entry_, "playlistEntry");
                auto row = lv_obj_get_child(music_bar_, 1);
                const char* names[] = {"previous", "pause", "next", "playlist"};
                for (int i = 0; i < 4; ++i) add_button(lv_obj_get_child(row, i), names[i]);
                auto header = lv_obj_get_child(playlist_popup_, 0);
                add_button(lv_obj_get_child(header, 1), "closePlaylist");
                auto playlists = cJSON_AddArrayToObject(root, "playlists");
                for (unsigned i = 0; i < lv_obj_get_child_count(playlist_list_); ++i) {
                    auto item = lv_obj_get_child(playlist_list_, i);
                    add_button(item, lv_label_get_text(lv_obj_get_child(item, 0)));
                    cJSON_AddItemToArray(playlists, cJSON_CreateString(lv_label_get_text(lv_obj_get_child(item, 0))));
                }
                char* encoded = cJSON_PrintUnformatted(root);
                lvgl_port_unlock();
                printf("MUSIC_UI %s\n", encoded ? encoded : "{}");
                cJSON_free(encoded);
                cJSON_Delete(root);
            }
        } else if (command == "back") {
            ExitToDesktop();
        } else if (command == "talk") {
            if (app.GetDeviceState() == kDeviceStateIdle) app.ToggleChatState();
        } else if (StartsWith(command, "tap ")) {
            int x = 0, y = 0;
            if (sscanf(command.c_str(), "tap %d %d", &x, &y) == 2 &&
                x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT &&
                injected_phase_.load() == 0) {
                injected_x_ = x; injected_y_ = y;
                injected_phase_.store(2);
            }
        } else if (StartsWith(command, "music ")) {
            auto* pair = new std::pair<YiyiBoard*, std::string>(this, command.substr(6));
            xTaskCreate(MusicTaskEntry, "music_cmd", 8192, pair, 5, nullptr);
        } else if (command == "musicstop") {
            auto music = Board::GetInstance().GetMusic();
            if (music) {
                static_cast<Esp32Music*>(music)->SetSleepTimer(0);
                music->StopStreaming();
            }
            printf("MUSIC_STOPPED\n");
        } else if (command == "musicpause" || command == "musicresume" || command == "musicstatus" || StartsWith(command, "musicsleep ")) {
            auto player = static_cast<Esp32Music*>(Board::GetInstance().GetMusic());
            if (player) {
                if (command == "musicpause") { player->PauseStreaming(); }
                if (command == "musicresume") { player->ResumeStreaming(); }
                if (StartsWith(command, "musicsleep ")) { player->SetSleepTimer(std::max(0, std::min(86400, atoi(command.substr(11).c_str())))); }
                auto state = player->PlaybackStatus();
                char* encoded = cJSON_PrintUnformatted(state);
                printf("MUSIC_STATUS %s\n", encoded ? encoded : "{}");
                cJSON_free(encoded);
                cJSON_Delete(state);
            }
        } else if (StartsWith(command, "musicmode ")) {
            auto music = Board::GetInstance().GetMusic();
            auto* esp32_music = static_cast<Esp32Music*>(music);
            auto mode = command.substr(10);
            if (esp32_music && (mode == "lyrics" || mode == "歌曲")) {
                esp32_music->SetDisplayMode(Esp32Music::DISPLAY_MODE_LYRICS);
                printf("MUSIC_MODE lyrics\n");
            } else if (esp32_music && (mode == "spectrum" || mode == "频谱")) {
                esp32_music->SetDisplayMode(Esp32Music::DISPLAY_MODE_SPECTRUM);
                printf("MUSIC_MODE spectrum\n");
            } else {
                printf("MUSIC_MODE invalid\n");
            }
        } else if (StartsWith(command, "musicurl ")) {
            auto url = command.substr(9);
            Settings settings("music", true);
            if (url == "default") {
                settings.EraseKey("server_url");
                printf("MUSIC_URL reset to default\n");
            } else {
                settings.SetString("server_url", url);
                printf("MUSIC_URL saved=%s\n", url.c_str());
            }
        } else if (command == "screenshot") {
            printf("SCREENSHOT_UNSUPPORTED\n");
        } else if (command == "help") {
            printf("status, back, talk, tap <x> <y>, music <song>, musicstop, musicmode lyrics|spectrum, musicurl <url>|default, screenshot, help\n");
        }
        fflush(stdout);
    }

    void InitializeDiagnostics() {
        usb_serial_jtag_driver_config_t config = {};
        config.tx_buffer_size = 4096;
        config.rx_buffer_size = 256;
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
        usb_serial_jtag_vfs_use_driver();
        xTaskCreate([](void* argument) {
            auto self = static_cast<YiyiBoard*>(argument);
            std::string command;
            while (true) {
                char ch;
                if (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100)) <= 0) continue;
                if (ch == '\r') continue;
                if (ch != '\n') {
                    if (command.size() < 160) command += ch;
                    continue;
                }
                self->HandleCommand(command);
                command.clear();
            }
        }, "desktop_diag", 8192, this, 1, nullptr);
    }

public:
    YiyiBoard() {
        // The desktop copies its own volume into the shared audio namespace.
        // Use the requested music-app startup volume before the codec starts.
        {
            Settings audio("audio", true);
            audio.SetInt("output_volume", 40);
        }
        battery_.Begin();
        InitializeI2c();
        InitializeDisplay();
        InitializeTouch();
        InitializeDiagnostics();
        GetBacklight()->RestoreBrightness();
        xTaskCreate(MusicUiTask, "music_ui", 4096, this, 2, nullptr);
        xTaskCreate([](void* argument) {
            auto self=static_cast<YiyiBoard*>(argument);
            // Board construction and audio initialization must complete first.
            vTaskDelay(pdMS_TO_TICKS(5000));
            BatteryPolicy::NoticeGate gate;
            for(;;) {
                self->battery_.Poll();const auto b=self->battery_.Get();
                const auto notice=gate.update(b.protection);
                if(notice!=BatteryPolicy::Notice::None) {
                    Application::GetInstance().Schedule([self,notice]{self->ShowBatteryNotice(notice);});
                } else if(!b.protection.low&&!b.protection.sensorFault&&!b.protection.high) {
                    Application::GetInstance().Schedule([self]{self->ClearBatteryNotice();});
                }
                vTaskDelay(pdMS_TO_TICKS(BatteryPolicy::SampleMs));
            }
        },"battery_care",4096,this,2,nullptr);
    }

    bool GetBatteryLevel(int& level,bool& charging,bool& discharging) override {
        const auto b=battery_.Get();
        level=b.percent;charging=false;discharging=false; // Actual charging/discharging are unknown.
        return b.valid;
    }

    AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec codec(i2c_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, true, true);
        return &codec;
    }

    Display* GetDisplay() override { return display_; }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(YiyiBoard);
