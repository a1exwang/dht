#include <albert/utp/resizable_buffer.hpp>

#include <functional>
#include <albert/log/log.hpp>

namespace albert::utp {

Allocator::Allocator(const std::string &name, size_t buffer_size, size_t count) :name_(name), buffer_size_(buffer_size) {

  real_buffer_ = std::unique_ptr<uint8_t, std::function<void(uint8_t*)>>(new uint8_t[buffer_size_ * count], [](uint8_t *ptr) { delete[] ptr; });
  for (size_t i = 0; i < count; i++) {
    available_buffers_.emplace_back(real_buffer_.get() + buffer_size_ * i, buffer_size_);
  }
}

Allocator::Buffer Allocator::allocate() {
  if (empty()) {
    throw OutOfMemory();
  }
  auto ret = available_buffers_.front();
  available_buffers_.pop_front();

  // this is slow
//  LOG(debug) << "utp::Allocator: allocate " << name_ << " " << available_buffers_.size();

  return std::shared_ptr<gsl::span<uint8_t>>(
      new gsl::span<uint8_t>(ret),
      [this](gsl::span<uint8_t> *ptr) {
//        LOG(debug) << "utp::Allocator: deallocate " << name_ << " " << available_buffers_.size();
        available_buffers_.push_back(*ptr);
        delete ptr;
      });
}

}