#include "dht_impl.hpp"
#include "get_peers.hpp"

#include <boost/bind.hpp>

#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/bt/peer_connection.hpp>


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
        LOG(info) << "handle get_peers from " << sender_id.to_string() << " got " << response.peers().size() << " peers";
        uint32_t ip;
        uint16_t port;
        for (auto item : response.peers()) {
          std::tie(ip, port) = item;
          dht_->get_peers_manager_->add_peer(info_hash, ip, port);
        }
      } else {
        auto old_prefix = krpc::NodeID::common_prefix_length(info_hash, sender_id);
        dht_->get_peers_manager_->set_node_traversed(info_hash, sender_id);
        LOG(info) << "Node traversed prefix " << old_prefix << " '"
                  << sender_id.to_string() << "'";
        for (auto &node : response.nodes()) {
          if (!dht_->get_peers_manager_->has_node_traversed(info_hash, node.id())) {
            auto new_prefix = krpc::NodeID::common_prefix_length(info_hash, node.id());
            if (new_prefix >= old_prefix) {
              LOG(info) << "Node to traverse prefix " << new_prefix << " " << node.id().to_string();
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
  dht_->routing_table->add_node(
      Entry{
          response.sender_id(),
          sender_endpoint.address().to_v4().to_uint(),
          sender_endpoint.port()
      });
  dht_->routing_table->make_good_now(response.sender_id());
}

void get_peers::GetPeersRequest::delete_node(const krpc::NodeID &id) {
  nodes_.erase(id);
}
void get_peers::GetPeersRequest::add_node(const krpc::NodeID &node) {
  nodes_.insert(std::make_pair(node, NodeStatus()));
}
bool get_peers::GetPeersRequest::has_node(const krpc::NodeID &id) const {
  return nodes_.find(id) != nodes_.end();
}
std::set<std::tuple<uint32_t, uint16_t>> get_peers::GetPeersRequest::peers() const {
  return peers_;
}
bool get_peers::GetPeersRequest::has_node_traversed(const krpc::NodeID &id) const {
  if (nodes_.find(id) == nodes_.end()) {
    return false;
  } else {
    return nodes_.at(id).traversed;
  }
}
void get_peers::GetPeersRequest::add_peer(uint32_t ip, uint16_t port) {
  peers_.insert({ip, port});
  for (auto &callback : callbacks_) {
    callback(ip, port);
  }
}
void get_peers::GetPeersRequest::set_node_traversed(const krpc::NodeID &id) {
  nodes_.at(id).traversed = true;
}
bool get_peers::GetPeersRequest::expired() const {
  return std::chrono::high_resolution_clock::now() > expiration_time_;
}
void get_peers::GetPeersRequest::add_callback(std::function<void(uint32_t, uint16_t)> callback) { this->callbacks_.emplace_back(std::move(callback)); }

bool get_peers::GetPeersManager::has_node(const krpc::NodeID &id, const krpc::NodeID &node) const {
  return requests_.at(id).has_node(node);
}
void get_peers::GetPeersManager::set_node_traversed(const krpc::NodeID &id, const krpc::NodeID &node) {
  requests_.at(id).set_node_traversed(node);
}
void get_peers::GetPeersManager::create_request(
    const krpc::NodeID &info_hash) {
  requests_.emplace(
      info_hash,
      GetPeersRequest(
          info_hash,
          std::chrono::high_resolution_clock::now() + expiration_));
}
void get_peers::GetPeersManager::add_peer(const krpc::NodeID &id, uint32_t ip, uint16_t port) {
  requests_.at(id).add_peer(ip, port);
}
void get_peers::GetPeersManager::add_node(const krpc::NodeID &id, const krpc::NodeID &node) {
  requests_.at(id).add_node(node);
}
bool get_peers::GetPeersManager::has_node_traversed(const krpc::NodeID &id, const krpc::NodeID &node) const {
  return requests_.at(id).has_node_traversed(node);
}
bool get_peers::GetPeersManager::has_request(const krpc::NodeID &id) const {
  return requests_.find(id) != requests_.end();
}
void get_peers::GetPeersManager::gc() {
  std::list<krpc::NodeID> to_delete;
  for (auto &item : requests_) {
    if (item.second.peers().size() > 0) {
      to_delete.push_back(item.first);
    } else {
      int64_t traversed = 0;
      int64_t total = 0;
      for (auto &node : item.second.nodes_) {
        if (node.second.traversed) {
          traversed++;
        }
        total++;
      }
      LOG(info) << item.first.to_string() << " node count,"
                << " total=" << total << ","
                << " traversed=" << traversed << ","
                << " peers=" << item.second.peers().size();
    }
  }
  for (auto &item : to_delete) {
    requests_.erase(item);
  }
}
void get_peers::GetPeersManager::add_callback(
    const krpc::NodeID &id,
    const std::function<void(uint32_t, uint16_t)> &callback) {
  requests_.at(id).add_callback(callback);
}

void DHTImpl::handle_get_peers_timer(const std::function<void()> &cancel) {
  dht_->get_peers_manager_->gc();
}

void DHTImpl::get_peers(const krpc::NodeID &info_hash, const std::function<void(uint32_t, uint16_t)> &callback) {

  if (!dht_->get_peers_manager_->has_request(info_hash)) {
    dht_->get_peers_manager_->create_request(info_hash);

//  auto targets = dht_->routing_table.k_nearest_good_nodes(info_hash, 200);
    // use the whole routing table

  } else {
    LOG(debug) << "get_peers() already searched for " << info_hash.to_string();
  }

  dht_->get_peers_manager_->add_callback(info_hash, callback);

  std::list<Entry> targets;
  dht_->routing_table->iterate_nodes([&targets](const Entry &entry) {
    targets.push_back(entry);
  });

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
