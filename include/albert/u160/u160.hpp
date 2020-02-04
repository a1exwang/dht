#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <string>
#include <iostream>


namespace albert::u160 {

class InvalidFormat :public std::runtime_error {
 public:
  using runtime_error::runtime_error;
};


constexpr size_t U160Length = 20;
constexpr size_t U160Bits = U160Length * 8;
class U160 {
 public:
  U160() {
    data_.fill(0);
  }

  static U160 pow2(size_t r);
  static U160 pow2m1(size_t r);
  static U160 from_string(std::string s);
  static U160 from_hex(const std::string &s);
  void encode(std::ostream &os) const;
  static U160 decode(std::istream &is);
  static U160 random();
  static U160 random_from_prefix(const U160 &prefix, size_t prefix_length);
  static size_t common_prefix_length(const U160 &lhs, const U160 &rhs);
  static U160 hash(const uint8_t *data, size_t size);

  std::string to_string() const;

  bool operator<(const U160 &rhs) const;
  bool operator==(const U160 &rhs) const;
  bool operator!=(const U160 &rhs) const;
  bool operator<=(const U160 &rhs) const;
  U160 operator&(const U160 &rhs) const;
  U160 operator|(const U160 &rhs) const;
  U160 operator^(const U160 &rhs) const;
  uint8_t bit(size_t r) const;
  U160 distance(const U160 &rhs) const { return *this ^ rhs; }

  U160 fake(const U160 &target, size_t prefix_length = 128) const;
 private:
  void bit(size_t r, size_t value);

 private:
  // network byte order
  std::array<uint8_t, U160Length> data_{};
};
}