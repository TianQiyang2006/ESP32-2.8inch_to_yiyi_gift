#include "wake_word_audio_cache.h"
#include <cassert>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    WakeWordAudioCache cache;
    assert(!cache.Initialize(0));
    assert(cache.Initialize(8) && cache.Initialize(8) && !cache.Initialize(16));
    std::vector<int16_t> first{1,2,3,4,5,6}, next{7,8,9,10};
    cache.Store(first.data(), first.size());
    cache.Store(next.data(), next.size());
    std::vector<int16_t> output(8);
    assert(cache.Read(0, output.data(), output.size()) == 8);
    assert((output == std::vector<int16_t>{3,4,5,6,7,8,9,10}));
    assert(cache.Read(6, output.data(), 8) == 2 && output[0] == 9 && output[1] == 10);
    assert(cache.Read(8, output.data(), 8) == 0);
    std::vector<int16_t> long_input{11,12,13,14,15,16,17,18,19,20};
    cache.Store(long_input.data(), long_input.size());
    assert(cache.Read(0, output.data(), 8) == 8);
    assert((output == std::vector<int16_t>{13,14,15,16,17,18,19,20}));
    cache.Clear();
    assert(cache.Size() == 0 && cache.Read(0, output.data(), 8) == 0);

    WakeWordAudioCache concurrent;
    assert(concurrent.Initialize(512));
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 1; i < 20000; ++i) {
            std::vector<int16_t> batch(512, static_cast<int16_t>(i));
            concurrent.Store(batch.data(), batch.size());
            if (i % 23 == 0) concurrent.Clear();
        }
        done = true;
    });
    size_t reads = 0;
    while (!done || reads < 1000) {
        std::vector<int16_t> batch(512);
        auto count = concurrent.Read(0, batch.data(), batch.size());
        assert(count == 0 || count == 512);
        for (size_t i = 1; i < count; ++i) assert(batch[i] == batch[0]);
        assert(concurrent.Size() <= 512);
        ++reads;
    }
    writer.join();
    std::cout << "PASS: official 2.5.0 wake audio cache wraparound, bounded storage and concurrent read/store/clear\n";
}
