#pragma once

#include <array>
#include <memory>
#include <gsl/span>
#include <vector>

#include <albert/utils/utils.hpp>
#include <albert/log/log.hpp>

namespace albert::ring_buffer {

constexpr size_t BufSize = 65536 * 2;


class RingBuffer {
 public:
  RingBuffer() = default;

  bool has_data(size_t size) const;
  void pop_data(void *output, size_t size);

  gsl::span<uint8_t> use_data(size_t size);

  void appended(size_t size);

  gsl::span<uint8_t> use_for_append(size_t append_size);

  void skip_data(size_t size);

  size_t data_size() const { return data_size_; }
  std::string stat();

  size_t remaining_size() const;

 private:
  void switch_buffer();

  size_t main_buf_remaining_size() const;

  size_t side_buf_remaining_size() const;

  size_t data_start() const { return data_start_; }
  size_t data_end() const { return data_start() + data_size(); }

  std::array<uint8_t, BufSize> &main_buf() {
    return main_buf_id_ == 0 ? buf0_ : buf1_;
  }

  [[nodiscard]]
  const std::array<uint8_t, BufSize> &main_buf() const {
    return main_buf_id_ == 0 ? buf0_ : buf1_;
  }

  [[nodiscard]] const std::array<uint8_t, BufSize> &side_buf() const {
    return main_buf_id_ == 0 ? buf1_ : buf0_;
  }
  std::array<uint8_t, BufSize> &side_buf() {
    return main_buf_id_ == 0 ? buf1_ : buf0_;
  }

  std::array<uint8_t, BufSize> &cross_buf() { return bufx_; }
  std::array<uint8_t, BufSize> &cross_buf_w() { return bufx_w_; }

 private:
  std::array<uint8_t, BufSize> buf0_{};
  std::array<uint8_t, BufSize> buf1_{};
  std::array<uint8_t, BufSize> bufx_{};
  std::array<uint8_t, BufSize> bufx_w_{};
  size_t main_buf_id_ = 0;
  bool cross_buf_w_has_data_ = false;

  size_t data_start_ = 0;
  size_t data_size_ = 0;
};


}