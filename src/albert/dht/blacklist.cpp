#include <albert/dht/blacklist.hpp>

#include <list>

namespace albert::dht {

bool Blacklist::add(Blacklist::KeyType endpoint) {
  if (items_.size() >= max_size_) {
    return false;
  }
  bool success = false;
  std::tie(std::ignore, success) = items_.insert({endpoint, BlacklistItem(banning_duration_)});
  return success;
}

bool Blacklist::has(const Blacklist::KeyType &item) const {
  auto it = items_.find(item);
  if (it == items_.end()) {
    return false;
  }
  return it->second.expired();
}

size_t Blacklist::gc() {
  std::list<KeyType> to_delete;
  for (auto &[key, item] : items_) {
    if (item.expired()) {
      to_delete.push_back(key);
    }
  }
  for (auto &item : to_delete) {
    items_.erase(item);
  }
  return to_delete.size();
}

size_t Blacklist::BlacklistHash::operator()(const Blacklist::KeyType &item) const {
  uint32_t ip = 0;
  uint16_t port = 0;
  std::tie(ip, port) = item;
  return (ip << 16u) | port;
}

bool BlacklistItem::expired() const {
  return std::chrono::system_clock::now() > expired_at_;
}
}
