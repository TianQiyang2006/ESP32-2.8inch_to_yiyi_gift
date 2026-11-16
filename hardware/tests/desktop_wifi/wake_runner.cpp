#define private public
#include "afe_wake_word.h"
#undef private
#include <iostream>

void WaitUntil(const std::function<bool()>& ready) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
int main() {
    AfeWakeWord wake;
    std::vector<int16_t> first(512, 1), later(512, 2);
    for (int i = 0; i < 66; ++i) wake.StoreWakeWordData(first.data(), first.size());
    holdEncoder = true; wake.EncodeWakeWordData();
    WaitUntil([] { return encoderStarted.load(); });
    // Resume detection while the network's previous wake batch is encoding.
    for (int i = 0; i < 66; ++i) wake.StoreWakeWordData(later.data(), later.size());
    holdEncoder = false;
    WaitUntil([&] { return eTaskGetState(wake.wake_word_encode_task_) == eDeleted; });
    std::vector<uint8_t> packet;
    int count = 0;
    while (wake.GetWakeWordOpus(packet)) { assert(packet[0] == 1); ++count; }
    assert(count == 66 && wake.wake_word_pcm_.size() == 66);
    holdEncoder = true; encoderStarted = false; wake.EncodeWakeWordData();
    WaitUntil([] { return encoderStarted.load(); });
    auto taskCount = fakeTasks.size();
    std::atomic<bool> retryStarted{false};
    std::thread retry([&] { retryStarted = true; wake.EncodeWakeWordData(); });
    WaitUntil([&] { return retryStarted.load(); });
    vTaskDelay(20);
    assert(fakeTasks.size() == taskCount); // No reuse of a running static task's stack.
    holdEncoder = false; retry.join();
    WaitUntil([&] { return eTaskGetState(wake.wake_word_encode_task_) == eDeleted; });
    assert(maxActiveTasks == 1);
    for (auto& task : fakeTasks) task->thread.join();
    std::cout << "PASS: concurrent wake detection preserves the owned encoding batch and future PCM; repeated encodes cannot reuse an active task stack\n";
}
