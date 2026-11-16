#pragma once
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_FAIL -1
#define ESP_WN_PREFIX "wn"
#define AFE_TYPE_SR 0
#define AFE_MODE_HIGH_PERF 0
#define AEC_MODE_SR_HIGH_PERF 0
#define AFE_MEMORY_ALLOC_MORE_PSRAM 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_INTERNAL 1
#define WAKENET_DETECTED 1
#define OPUS_FRAME_DURATION_MS 60
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
using StackType_t = int;
struct StaticTask_t {};
enum eTaskState { eRunning, eDeleted };
struct FakeTask { std::thread thread; std::atomic<eTaskState> state{eRunning}; };
using TaskHandle_t = FakeTask*;
inline std::vector<std::unique_ptr<FakeTask>> fakeTasks;
inline std::atomic<int> activeTasks{0}, maxActiveTasks{0};
inline int xTaskCreate(void (*)(void*), const char*, int, void*, int, TaskHandle_t*) { return 1; }
inline TaskHandle_t xTaskCreateStatic(void (*fn)(void*), const char*, size_t, void* arg, int, StackType_t*, StaticTask_t*) {
    auto task = std::make_unique<FakeTask>(); auto ptr = task.get();
    fakeTasks.push_back(std::move(task));
    ptr->thread = std::thread([=] {
        int count = ++activeTasks; maxActiveTasks.store(std::max(maxActiveTasks.load(), count));
        fn(arg); --activeTasks; ptr->state = eDeleted;
    });
    return ptr;
}
inline eTaskState eTaskGetState(TaskHandle_t task) { return task->state; }
inline void vTaskDelay(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void vTaskDelete(void*) {}
using EventGroupHandle_t = int;
inline int xEventGroupCreate() { return 0; }
inline void vEventGroupDelete(int) {}
inline void xEventGroupSetBits(int, int) {}
inline void xEventGroupClearBits(int, int) {}
inline int xEventGroupWaitBits(int, int, int, int, int) { return 0; }
inline void* heap_caps_malloc(size_t size, int) { return malloc(size); }
inline void heap_caps_free(void* pointer) { free(pointer); }
inline long long esp_timer_get_time() { return 0; }
class WakeWord { public: virtual ~WakeWord() = default; };
class AudioCodec { public: bool input_reference() { return false; } int input_channels() { return 1; } };
struct srmodel_list_t { int num; char** model_name; };
struct esp_afe_sr_data_t {};
struct afe_fetch_result_t { int ret_value, data_size, wakeup_state, wakenet_model_index; int16_t* data; };
struct afe_config_t { int aec_init, aec_mode, afe_perferred_core, afe_perferred_priority, memory_alloc_mode; };
struct esp_afe_sr_iface_t {
    void (*destroy)(esp_afe_sr_data_t*);
    esp_afe_sr_data_t* (*create_from_config)(afe_config_t*);
    void (*reset_buffer)(esp_afe_sr_data_t*);
    void (*feed)(esp_afe_sr_data_t*, const int16_t*);
    int (*get_feed_chunksize)(esp_afe_sr_data_t*);
    int (*get_fetch_chunksize)(esp_afe_sr_data_t*);
    afe_fetch_result_t* (*fetch_with_delay)(esp_afe_sr_data_t*, int);
};
inline srmodel_list_t* esp_srmodel_init(const char*) { return nullptr; }
inline void esp_srmodel_deinit(srmodel_list_t*) {}
inline char* esp_srmodel_get_wake_words(srmodel_list_t*, char*) { return nullptr; }
inline afe_config_t* afe_config_init(const char*, srmodel_list_t*, int, int) { return nullptr; }
inline const esp_afe_sr_iface_t* esp_afe_handle_from_config(afe_config_t*) { return nullptr; }
inline std::atomic<bool> holdEncoder{false}, encoderStarted{false};
class OpusEncoderWrapper {
public:
    OpusEncoderWrapper(int, int, int) {}
    void SetComplexity(int) {}
    void Encode(std::vector<int16_t>&& pcm, std::function<void(std::vector<uint8_t>&&)> callback) {
        encoderStarted = true;
        while (holdEncoder) std::this_thread::yield();
        assert(pcm.size() == 512);
        callback(std::vector<uint8_t>{uint8_t(pcm.front())});
    }
};
