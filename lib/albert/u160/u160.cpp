#include <albert/u160/u160.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <iomanip>
#include <random>
#include <sstream>
#include <string>

#include <openssl/sha.h>

namespace albert::u160 {

U160 U160::random() {
  std::random_device device;
  std::mt19937 rng{std::random_device()()};
  U160 ret{};
  std::generate(ret.data_.begin(), ret.data_.end(), rng);
  return ret;
}
std::string U160::to_string() const {
  std::stringstream ss;
  for (auto c : data_) {
    ss << std::hex << std::setfill('0') << std::setw(2) << (uint32_t)(uint8_t)c;
  }
  return ss.str();
}
U160 U160::pow2m1(size_t r) {
  assert(r > 0 && r <= U160Bits);
  // NodeIDLength - ceil(r/8)
  size_t index = U160Length - ((r-1) / 8 + 1);
  size_t bit = (r % 8 == 0) ? 8 : (r % 8);
  U160 ret{};
  ret.data_[index] = (1u << bit) - 1;
  for (size_t i = index+1; i < U160Length; i++) {
    ret.data_[i] = 0xffu;
  }
  return ret;
}
U160 U160::pow2(size_t r) {
  assert(r >= 0 && r < U160Bits);
  size_t index = U160Length - 1 - r / 8;
  size_t bit = r % 8;
  U160 ret{};
  ret.data_[index] = 1u << bit;
  return ret;
}
void U160::encode(std::ostream &os) const {
  os.write((const char*)data_.data(), data_.size());
}
bool U160::operator<=(const U160 &rhs) const {
  return data_ <= rhs.data_;
}
bool U160::operator==(const U160 &rhs) const {
  return data_ == rhs.data_;
}
bool U160::operator!=(const U160 &rhs) const {
  return data_ != rhs.data_;
}
bool U160::operator<(const U160 &rhs) const {
  // data_ is big endian so we can use lexicographical_compare
  return data_ < rhs.data_;
}
U160 U160::operator&(const U160 &rhs) const {
  U160 ret{};
  for (int i = 0; i < data_.size(); i++) {
    ret.data_[i] = data_[i] & rhs.data_[i];
  }
  return ret;
}
U160 U160::operator|(const U160 &rhs) const {
  U160 ret{};
  for (int i = 0; i < data_.size(); i++) {
    ret.data_[i] = data_[i] | rhs.data_[i];
  }
  return ret;
}
U160 U160::operator^(const U160 &rhs) const {
  U160 ret{};
  for (int i = 0; i < data_.size(); i++) {
    ret.data_[i] = data_[i] ^ rhs.data_[i];
  }
  return ret;
}
uint8_t U160::bit(size_t r) const {
  assert(r >= 0 && r < U160Bits);
  // NodeIDLength - ceil(r/8)
  size_t index = U160Length - 1 - r / 8;
  size_t bit = r % 8;
  return (data_[index] >> bit) & 1u;
}

U160 U160::decode(std::istream &is) {
  if (is) {
    U160 ret{};
    is.read((char*)ret.data_.data(), U160Length);
    if (is.good() && is.gcount() == U160Length) {
      return ret;
    } else {
      throw InvalidFormat("Cannot read NodeID from stream, bad stream when reading");
    }
  } else {
    throw InvalidFormat("Cannot read NodeID from stream, bad stream");
  }
}
U160 U160::from_string(std::string s) {
  U160 ret{};
  if (s.size() != U160Length) {
    throw InvalidFormat("NodeID is not NodeIDLength long");
  }
  std::copy(s.begin(), s.end(), ret.data_.begin());
  return ret;
}
U160 U160::from_hex(const std::string &s) {
  if (s.size() < U160Length * 2) {
    throw InvalidFormat("NodeID hex not long enough, expected " + std::to_string(U160Length*2) + ", got " + std::to_string(s.size()));
  }
  U160 ret;
  for (int i = 0; i < U160Length; i++) {
    std::string part(s.data() + i * 2, 2);
    size_t end_index = 0;
    uint8_t b = std::stoi(part, &end_index, 16);
    if (end_index == 0) {
      throw InvalidFormat("Missing hex number at index " + std::to_string(i));
    }
    ret.data_[i] = b;
  }
  return ret;
}
U160 U160::random_from_prefix(const U160 &prefix, size_t prefix_length) {
  // TODO: not tested
  U160 ret = random();
  if (prefix_length == 0)
    return ret;

  // prefix_length >= 1
  auto bytes = (prefix_length-1) / 8;
  // 0 <= bytes < NodeIDLength
  auto bits = 8 - prefix_length % 8;
  std::copy(prefix.data_.begin(), std::next(prefix.data_.begin(), bytes), ret.data_.begin());
  auto mask_low = ((1u << bits) - 1u);
  auto mask_high = mask_low;
  ret.data_[bytes] = (ret.data_[bytes] & mask_low) | (prefix.data_[bytes] & mask_high);
  return ret;
}
size_t U160::common_prefix_length(const U160 &lhs, const U160 &rhs) {
  for (size_t i = 0; i < U160Bits; i++) {
    size_t b = U160Bits - i - 1;
    if (lhs.bit(b) != rhs.bit(b)) {
      return i;
    }
  }
  return U160Bits;
}
U160 U160::hash(const uint8_t *data, size_t size) {
  U160 ret;
  SHA1(data, size, ret.data_.data());
  return ret;
}

U160 U160::fake(const U160 &target, size_t prefix_length) const {
  U160 ret{};
  for (size_t i = 0; i < U160Bits; i++) {
    if (i < prefix_length) {
      ret.bit(i, bit(i));
    } else {
      ret.bit(i, target.bit(i));
    }
  }
  return ret;
}

void U160::bit(size_t r, size_t value) {
  assert(r >= 0 && r < U160Bits);
  // NodeIDLength - ceil(r/8)
  size_t index = U160Length - 1 - r / 8;
  size_t bit = r % 8;
  if (value == 0) {
    data_[index] &= ~(1u << bit);
  } else {
    data_[index] |= 1u << bit;
  }
}


}