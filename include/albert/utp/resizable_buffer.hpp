#pragma once

#include <cstddef>
#include <cstdint>

#include <exception>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <gsl/span>

namespace albert::utp {

class Allocator {
 public:
  class OutOfMemory : std::exception { };
 public:
  typedef std::shared_ptr<gsl::span<uint8_t>> Buffer;
  Allocator(const std::string &name, size_t buffer_size, size_t count);
  Buffer allocate();
  bool empty() const { return available_buffers_.empty(); }
  size_t buffer_size() const { return buffer_size_; }
 private:
  std::string name_;
  size_t buffer_size_;
  std::list<gsl::span<uint8_t>> available_buffers_;
  std::unique_ptr<uint8_t, std::function<void(uint8_t*)>> real_buffer_;
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
