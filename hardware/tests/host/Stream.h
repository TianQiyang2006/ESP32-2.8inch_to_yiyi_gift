#pragma once
#include <cstddef>
#include <cstdint>
class Stream {
public:
 virtual ~Stream() = default;
 virtual size_t write(uint8_t) = 0;
 virtual size_t write(const uint8_t *data,size_t length) {
  size_t count=0;while(count<length&&write(data[count]))++count;return count;
 }
 virtual int available() = 0;
 virtual int read() = 0;
 virtual int peek() = 0;
 virtual void flush() = 0;
};
