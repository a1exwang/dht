#pragma once
#include <functional>
#include <list>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include <albert/krpc/krpc.hpp>
#include <albert/u160/u160.hpp>

namespace albert::dht::get_peers {


struct NodeStatus {
  bool traversed = false;
};

class GetPeersRequest {
 public:
  GetPeersRequest(
      albert::u160::U160 target,
      std::chrono::high_resolution_clock::time_point expiration_time)
      :target_info_hash_(target),
       expiration_time_(expiration_time) {}

  void add_peer(uint32_t ip, uint16_t port);
  [[nodiscard]]
  std::set<std::tuple<uint32_t, uint16_t>> peers() const;

  void add_callback(std::function<void(uint32_t, uint16_t)> callback);

  bool expired() const;

  void add_node(const u160::U160 &node);
  [[nodiscard]]
  bool has_node(const u160::U160 &id) const;
  void delete_node(const u160::U160 &id);

  [[nodiscard]]
  bool has_node_traversed(const u160::U160 &id) const;
  void set_node_traversed(const u160::U160 &id);

// private:
  std::list<std::function<void (uint32_t, uint16_t)>> callbacks_;
  u160::U160 target_info_hash_;
  std::map<albert::u160::U160, NodeStatus> nodes_;
  std::set<std::tuple<uint32_t, uint16_t>> peers_;
  std::chrono::high_resolution_clock::time_point expiration_time_;
};

class GetPeersManager {
 public:
  explicit GetPeersManager(int64_t expiration_seconds) :expiration_(expiration_seconds) {}
  void add_peer(const u160::U160 &id, uint32_t ip, uint16_t port);
  [[nodiscard]]
  bool has_request(const u160::U160 &id) const;
  void add_callback(const u160::U160 &id, const std::function<void (uint32_t, uint16_t)> &callback);
  void add_node(const u160::U160 &id, const u160::U160 &node);
  [[nodiscard]]
  bool has_node_traversed(const u160::U160 &id, const u160::U160 &node) const;
  [[nodiscard]]
  bool has_node(const u160::U160 &id, const u160::U160 &node) const;
  void set_node_traversed(const u160::U160 &id, const u160::U160 &node);
  void create_request(
      const u160::U160 &info_hash);

  void gc();
 private:
  std::map<u160::U160, GetPeersRequest> requests_;
  std::chrono::seconds expiration_;
};

}