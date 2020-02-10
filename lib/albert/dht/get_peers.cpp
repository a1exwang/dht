#include "dht_impl.hpp"
#include "get_peers.hpp"

#include <boost/bind.hpp>

#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/bt/peer_connection.hpp>
#include <albert/u160/u160.hpp>


namespace albert::dht {

void DHTImpl::handle_get_peers_response(
    const krpc::GetPeersResponse &response,
    const krpc::GetPeersQuery &query
) {
  auto transaction_id = response.transaction_id();
  auto info_hash = query.info_hash();
  auto sender_id = response.sender_id();
  if (dht_->get_peers_manager_->has_request(info_hash)) {
    if (dht_->get_peers_manager_->has_node(info_hash, sender_id)) {
      if (response.has_peers()) {
        LOG(debug) << "handle get_peers from " << sender_id.to_string() << " got " << response.peers().size() << " peers";
        uint32_t ip;
        uint16_t port;
        for (auto item : response.peers()) {
          std::tie(ip, port) = item;
          dht_->get_peers_manager_->add_peer(info_hash, ip, port);
        }
      } else {
        auto old_prefix = u160::U160::common_prefix_length(info_hash, sender_id);
        dht_->get_peers_manager_->set_node_traversed(info_hash, sender_id);
        LOG(debug) << "Node traversed prefix " << old_prefix << " '"
                  << sender_id.to_string() << "'";
        for (auto &node : response.nodes()) {
          if (!dht_->get_peers_manager_->has_node_traversed(info_hash, node.id()) && node.valid()) {
            auto new_prefix = u160::U160::common_prefix_length(info_hash, node.id());
            if (new_prefix >= old_prefix) {
              LOG(debug) << "Node to traverse prefix " << new_prefix << " " << node.id().to_string();
              send_get_peers_query(info_hash, node);
            } else {
              LOG(debug) << "Node ignored new prefix length(" << new_prefix << ") shorter than old(" << old_prefix << ")";
            }
          }
        }
      }
    } else {
      LOG(debug) << "GetPeersManager info_hash: '" << info_hash.to_string() << "' "
                 << "unknown node sent us a request. node: " << sender_id.to_string();
    }
  } else {
    LOG(debug) << "GetPeersRequest manager failed, info_hash not found";
  }

  good_sender(response.sender_id(), response.version());
}

void get_peers::GetPeersRequest::delete_node(const u160::U160 &id) {
  nodes_.erase(id);
}
void get_peers::GetPeersRequest::add_node(const u160::U160 &node) {
  nodes_.insert(std::make_pair(node, NodeStatus()));
}
bool get_peers::GetPeersRequest::has_node(const u160::U160 &id) const {
  return nodes_.find(id) != nodes_.end();
}
std::set<std::tuple<uint32_t, uint16_t>> get_peers::GetPeersRequest::peers() const {
  return peers_;
}
bool get_peers::GetPeersRequest::has_node_traversed(const u160::U160 &id) const {
  if (nodes_.find(id) == nodes_.end()) {
    return false;
  } else {
    return nodes_.at(id).traversed;
  }
}
void get_peers::GetPeersRequest::add_peer(uint32_t ip, uint16_t port) {
  auto result = peers_.insert({ip, port});
  // Call the add-peer-callback only when the peer is first added
  if (result.second) {
    for (auto &callback : callbacks_) {
      callback(ip, port);
    }
  }
}
void get_peers::GetPeersRequest::set_node_traversed(const u160::U160 &id) {
  nodes_.at(id).traversed = true;
}
bool get_peers::GetPeersRequest::expired() const {
  return std::chrono::high_resolution_clock::now() > expiration_time_;
}
void get_peers::GetPeersRequest::add_callback(std::function<void(uint32_t, uint16_t)> callback) { this->callbacks_.emplace_back(std::move(callback)); }

bool get_peers::GetPeersManager::has_node(const u160::U160 &id, const u160::U160 &node) const {
  return requests_.at(id).has_node(node);
}
void get_peers::GetPeersManager::set_node_traversed(const u160::U160 &id, const u160::U160 &node) {
  requests_.at(id).set_node_traversed(node);
}
void get_peers::GetPeersManager::create_request(
    const u160::U160 &info_hash) {
  requests_.emplace(
      info_hash,
      GetPeersRequest(
          info_hash,
          std::chrono::high_resolution_clock::now() + expiration_));
}
void get_peers::GetPeersManager::add_peer(const u160::U160 &id, uint32_t ip, uint16_t port) {
  requests_.at(id).add_peer(ip, port);
}
void get_peers::GetPeersManager::add_node(const u160::U160 &id, const u160::U160 &node) {
  requests_.at(id).add_node(node);
}
bool get_peers::GetPeersManager::has_node_traversed(const u160::U160 &id, const u160::U160 &node) const {
  return requests_.at(id).has_node_traversed(node);
}
bool get_peers::GetPeersManager::has_request(const u160::U160 &id) const {
  return requests_.find(id) != requests_.end();
}
void get_peers::GetPeersManager::gc() {
  std::list<u160::U160> to_delete;

  size_t had_peer = 0;
  size_t total_peers = 0;
  size_t total_traversed = 0;
  size_t total_nodes = 0;
  for (auto &item : requests_) {
    if (item.second.expired() > 0) {
      to_delete.push_back(item.first);
    } else {
      int64_t total = 0;
      for (auto &node : item.second.nodes_) {
        if (node.second.traversed) {
          total_traversed++;
        }
        total_nodes++;
      }
      if (item.second.peers().size() > 0) {
        had_peer++;
        total_peers += item.second.peers().size();
      }
    }
  }
  for (auto &item : to_delete) {
    requests_.erase(item);
  }

  LOG(info) << "GetPeersManager: nodes/traversed/peers/valid requests/deleting "
            << total_nodes << "/"
            << total_traversed << "/"
            << total_peers << "/"
            << had_peer << "/"
            << to_delete.size();
}
void get_peers::GetPeersManager::add_callback(
    const u160::U160 &id,
    const std::function<void(uint32_t, uint16_t)> &callback) {
  requests_.at(id).add_callback(callback);
}

void DHTImpl::handle_get_peers_timer(const std::function<void()> &cancel) {
  dht_->get_peers_manager_->gc();
}

void DHTImpl::get_peers(const u160::U160 &info_hash, const std::function<void(uint32_t, uint16_t)> &callback) {

  if (!dht_->get_peers_manager_->has_request(info_hash)) {
    dht_->get_peers_manager_->create_request(info_hash);

//  auto targets = dht_->routing_table.k_nearest_good_nodes(info_hash, 200);
    // use the whole routing table

  } else {
    LOG(debug) << "get_peers() already searched for " << info_hash.to_string();
  }

  dht_->get_peers_manager_->add_callback(info_hash, callback);

  std::list<routing_table::Entry> targets = dht_->main_routing_table_->k_nearest_good_nodes(
      info_hash,
      dht_->main_routing_table_->max_bucket_size());
//  dht_->main_routing_table_->iterate_nodes([&targets](const routing_table::Entry &entry) {
//    targets.push_back(entry);
//  });

  int sent = 0;
  for (auto &entry : targets) {
    auto receiver = entry.node_info();
    if (!dht_->get_peers_manager_->has_node(info_hash, receiver.id())) {
      send_get_peers_query(info_hash, receiver);
      sent++;
    }
  }
  LOG(info) << "dht_get_peers " << sent << " sent";
}

}
