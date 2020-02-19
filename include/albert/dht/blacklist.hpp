#pragma once

#include <chrono>
#include <unordered_set>
#include <unordered_map>

namespace albert::dht {

class BlacklistItem {
 public:
  BlacklistItem(std::chrono::microseconds expiration)
      : expired_at_(std::chrono::system_clock::now() + expiration) { }
  bool expired() const;
 private:
  std::chrono::system_clock::time_point expired_at_;
};


class Blacklist {
 public:
  typedef std::tuple<uint32_t, uint16_t> KeyType;

  struct BlacklistHash {
    size_t operator()(const KeyType &item) const;
  };

  Blacklist(size_t max_size, std::chrono::microseconds duration) :max_size_(max_size), banning_duration_(duration) { }

  bool add(KeyType endpoint);

  [[nodiscard]]
  bool has(const KeyType &item) const;

  [[nodiscard]]
  size_t memory_size() const {
    return sizeof(*this) + items_.size() * (sizeof(KeyType) + sizeof(BlacklistItem));
  }
  [[nodiscard]]
  size_t size() const { return items_.size(); }

  size_t gc();
 private:
  size_t max_size_;
  std::chrono::microseconds banning_duration_;
  std::unordered_map<KeyType, BlacklistItem, BlacklistHash> items_;
};

}