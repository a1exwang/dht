#pragma once
#include <list>
#include <vector>
#include <functional>
#include <tuple>
#include <set>

#include <krpc/krpc.hpp>

namespace dht::get_peers {

class GetPeersRequest {
 public:
  GetPeersRequest(
      krpc::NodeID target,
      std::function<void (
          krpc::NodeID target,
          const std::list<std::tuple<uint32_t, uint16_t>> &result
      )> callback)
      :callback_(callback),
       target_info_hash_(target)
       {
  }

  [[nodiscard]]
  std::set<std::tuple<uint32_t, uint16_t>> peers() const {
    return peers_;
  }
  void add_peer(uint32_t ip, uint16_t port) {
    peers_.insert({ip, port});
  }

  void add_node(const krpc::NodeInfo &node);
  void delete_node(const krpc::NodeID &id);
  void callback();

 private:
  std::function<void (
      krpc::NodeID target,
      const std::list<std::tuple<uint32_t, uint16_t>> &result
      )> callback_;
  krpc::NodeID target_info_hash_;
  std::set<krpc::NodeInfo> nodes_;
  std::set<std::tuple<uint32_t, uint16_t>> peers_;
};

class GetPeersManager {
 public:
  GetPeersManager() {

  }
  void add_peer(const krpc::NodeID &id, uint32_t ip, uint16_t port) {
    requests_.at(id).add_peer(ip, port);
  }
  bool has_request(const krpc::NodeID &id) const {
    return requests_.find(id) != requests_.end();
  }
  void add_node(const krpc::NodeID &id, const krpc::NodeInfo &node) {
    requests_.at(id).add_node(node);
  }
  void delete_node(const krpc::NodeID &id, const krpc::NodeID &node) {
    requests_.at(id).delete_node(node);
  }
  void done(const krpc::NodeID &id) {
    requests_.at(id).callback();
    requests_.erase(id);
  }
  void create_request(
      const krpc::NodeID &id,
      krpc::NodeID target,
      std::function<void (
          krpc::NodeID target,
          const std::list<std::tuple<uint32_t, uint16_t>> &result
      )> callback) {
    requests_.emplace(id, GetPeersRequest(target, std::move(callback)));
  }
 private:
  std::map<krpc::NodeID, GetPeersRequest> requests_;
};

}