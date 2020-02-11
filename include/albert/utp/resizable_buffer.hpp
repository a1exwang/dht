#pragma once

#include <cstddef>
#include <cstdint>

#include <exception>
#include <list>
#include <memory>
#include <vector>

namespace albert::utp {

class Allocator {
 public:
  class OutOfMemory : std::exception { };
 public:
  typedef std::vector<uint8_t>* Buffer;
  Allocator(size_t buffer_size, size_t count) :buffer_size_(buffer_size), buffers_() {
    for (size_t i = 0; i < count; i++) {
      buffers_.push_back(new std::vector<uint8_t>(buffer_size_));
    }
  }
  ~Allocator() {
    for (auto buffer : buffers_) {
      delete buffer;
    }
  }
  Buffer allocate() {
    if (buffers_.empty()) {
      throw OutOfMemory();
    }
    auto ret = buffers_.front();
    buffers_.pop_front();
    return ret;
  }
  void deallocate(Buffer buffer) {
    buffers_.push_back(buffer);
  }
  bool empty() const { return buffers_.empty(); }
  size_t buffer_size() const { return buffer_size_; }
 private:
  size_t buffer_size_;
  std::list<Buffer> buffers_;
};

// This is a buffer class that behaves like std::vector<uint8_t>
// But it can pop_front with O(1) time
class ResizableBuffer {
 public:
  ResizableBuffer(
      Allocator::Buffer data_owner,
      gsl::span<const uint8_t> data) :data_owner_(data_owner), data_(data), start_(0) { }

  const uint8_t *data() const {
    return data_.data() + start_;
  }
  size_t size() const {
    return data_.size() - start_;
  }
  void skip_front(size_t n) {
    start_ += n;
  }
  Allocator::Buffer buffer() { return data_owner_; }
 private:
  Allocator::Buffer data_owner_;
  gsl::span<const uint8_t> data_;
  size_t start_;
};

}
