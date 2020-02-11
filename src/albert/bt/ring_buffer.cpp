#include <sstream>
#include <cassert>

#include <albert/bt/ring_buffer.hpp>
#include <albert/log/log.hpp>

template <class ElementType>
using span = gsl::span<ElementType>;

namespace albert::ring_buffer {

bool RingBuffer::has_data(size_t size) const {
  return data_size() >= size;
}
void RingBuffer::pop_data(void *output, size_t size) {
  auto p = use_data(size);
  std::copy(p.begin(), p.end(), (char*)output);
  skip_data(size);
}

span<uint8_t> RingBuffer::use_data(size_t size) {
  assert(size <= data_size());
  if (data_start() + size < main_buf().size()) {
    return span<uint8_t>(main_buf().data() + data_start(), size);
  } else {
    auto data_size_in_side_buf = data_start() + size - main_buf().size();
    std::copy(std::next(main_buf().begin(), data_start()), main_buf().end(), cross_buf().begin());
    std::copy(side_buf().begin(), std::next(side_buf().begin(), data_size_in_side_buf), std::next(cross_buf().begin(), main_buf().size() - data_start()));
    return span<uint8_t>(cross_buf().data(), size);
  }
}
void RingBuffer::appended(size_t size) {
  if (cross_buf_w_has_data_) {
    if (main_buf_remaining_size() > size) {
//        LOG(info) << "RingBuffer::appended() copy to main buf " << size << ", main remaing size: " << main_buf_remaining_size() << ", " << stat();
      std::copy(cross_buf_w().begin(), std::next(cross_buf_w().begin(), size), std::next(main_buf().begin(), data_start() + data_size()));
    } else {
//        LOG(info) << "RingBuffer::appended() copy to both bufs " << size << ", main remaing size: " << main_buf_remaining_size() << ", " << stat();
      size_t size0 = main_buf_remaining_size();
      std::copy(cross_buf_w().begin(), std::next(cross_buf_w().begin(), size0), std::next(main_buf().begin(), data_start() + data_size()));
      std::copy(std::next(cross_buf_w().begin(), size0), std::next(cross_buf_w().begin(), size), side_buf().begin());
    }
    cross_buf_w_has_data_ = false;
    data_size_ += size;
  } else {
//      LOG(info) << "RingBuffer::appended() no copy " << size << " " << stat();
    data_size_ += size;
  }
}
span<uint8_t> RingBuffer::use_for_append(size_t append_size) {
  if (main_buf_remaining_size() == 0) {
    if (append_size <= side_buf_remaining_size()) {
      cross_buf_w_has_data_ = false;
//        LOG(info) << "use_for_append to side buffer " << append_size << " " << stat();
      return span<uint8_t>(side_buf().data()+(data_start()+data_size()-main_buf().size()), append_size);
    } else {
      throw std::overflow_error("Overflow, appended size " + std::to_string(append_size) + " > "
                                    + "remaining size " + std::to_string(side_buf_remaining_size()));
    }
  } else if (main_buf_remaining_size() < append_size) {
//      LOG(info) << "use_for_append to cross buffer " << append_size << " " << stat();
    cross_buf_w_has_data_ = true;
    return span<uint8_t>(cross_buf_w().data(), append_size);
  } else {
//      LOG(info) << "use_for_append to main buffer " << append_size << " " << stat();
    cross_buf_w_has_data_ = false;
    return span<uint8_t>(main_buf().data() + data_start() + data_size(), append_size);
  }
}
void RingBuffer::skip_data(size_t size) {
  assert(size <= data_size());
  data_start_ += size;
  data_size_ -= size;

  if (data_start() >= main_buf().size()) {
    switch_buffer();
  }
//    LOG(info) << "skipped data " << size << " " << stat();
}
std::string RingBuffer::stat() {
  std::stringstream ss;
  ss << "main buf: " << main_buf_id_ << ", data: "
     << data_start_ << " " << data_size_ << ", crossbuf has data: " << cross_buf_w_has_data_;
  return ss.str();
}
size_t RingBuffer::remaining_size() const { return main_buf().size() + side_buf().size() - data_end(); }
size_t RingBuffer::main_buf_remaining_size() const {
  if (data_end() >= main_buf().size()) {
    return 0;
  } else {
    return main_buf().size() - (data_end());
  }
}
size_t RingBuffer::side_buf_remaining_size() const {
  if (data_end() < main_buf().size()) {
    return side_buf().size();
  } else {
    return side_buf().size() - (data_end() - main_buf().size());
  }
}
void RingBuffer::switch_buffer() {
  data_start_ -= main_buf().size();
  main_buf_id_ = main_buf_id_ == 0 ? 1 : 0;
//    LOG(info) << "RingBuffer: switch buffer " << stat();
}

}
