#pragma once
#include <Arduino.h>
#include <Stream.h>

// HTTPClient writes decoded bodies here, including chunked transfer responses.
// Short writes stop HTTPClient before a remote body can grow without a bound.
class BoundedStringStream final : public Stream {
public:
    explicit BoundedStringStream(size_t limit) : limit_(limit) {}
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t *bytes, size_t count) override {
        if (failed_) return 0;
        const size_t room = limit_ - body_.length();
        const size_t accepted = count < room ? count : room;
        if (accepted && !body_.concat(reinterpret_cast<const char *>(bytes), accepted)) {
            failed_ = true;
            return 0;
        }
        if (accepted != count) { exceeded_ = true; failed_ = true; }
        return accepted;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    const String &body() const { return body_; }
    bool exceeded() const { return exceeded_; }
    bool failed() const { return failed_; }
private:
    String body_;
    const size_t limit_;
    bool exceeded_ = false, failed_ = false;
};
