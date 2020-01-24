#include "dht_impl.hpp"
#include "get_peers.hpp"

#include <boost/bind.hpp>

#include <dht/dht.hpp>


namespace dht {

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
//        dht_->get_peers_manager_->done(info_hash);
      } else {
        dht_->get_peers_manager_->set_node_traversed(info_hash, sender_id);
        LOG(info) << "Node traversed '" << sender_id.to_string() << "'";
        for (auto &node : response.nodes()) {
          if (!dht_->get_peers_manager_->has_node_traversed(info_hash, node.id())) {
            send_get_peers_query(info_hash, node);
          }
        }
      }
    } else {
      LOG(error) << "GetPeersManager info_hash: '" << info_hash.to_string() << "' "
                 << "unknown node sent us a request. node: " << sender_id.to_string();
    }
  } else {
    LOG(error) << "GetPeersRequest manager failed, info_hash not found";
  }
  this->dht_->routing_table.add_node(
      Entry{
          response.sender_id(),
          sender_endpoint.address().to_v4().to_uint(),
          sender_endpoint.port()
      });
  this->dht_->routing_table.make_good_now(response.sender_id());
}

void get_peers::GetPeersRequest::delete_node(const krpc::NodeID &id) {
  nodes_.erase(id);
}
void get_peers::GetPeersRequest::add_node(const krpc::NodeID &node) {
  nodes_.insert(std::make_pair(node, NodeStatus()));
}
void get_peers::GetPeersRequest::callback() {
  callback_(target_info_hash_, peers_);
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
}
void get_peers::GetPeersRequest::set_node_traversed(const krpc::NodeID &id) {
  nodes_.at(id).traversed = true;
}
bool get_peers::GetPeersRequest::expired() const {
  return std::chrono::high_resolution_clock::now() > expiration_time_;
}

bool get_peers::GetPeersManager::has_node(const krpc::NodeID &id, const krpc::NodeID &node) const {
  return requests_.at(id).has_node(node);
}
void get_peers::GetPeersManager::set_node_traversed(const krpc::NodeID &id, const krpc::NodeID &node) {
  requests_.at(id).set_node_traversed(node);
}
void get_peers::GetPeersManager::create_request(
    const krpc::NodeID &info_hash,
    std::function<void(krpc::NodeID target, const std::set<std::tuple<uint32_t, uint16_t>> &result )> callback) {
  requests_.emplace(
      info_hash,
      GetPeersRequest(
          info_hash,
          std::chrono::high_resolution_clock::now() + expiration_,
          std::move(callback)));
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
void get_peers::GetPeersManager::cleanup_expired() {
  std::list<krpc::NodeID> to_delete;
  for (auto &item : requests_) {
    if (item.second.expired()) {
      item.second.callback();
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

void DHTImpl::handle_get_peers_timer(const boost::system::error_code &e) {
  if (e) {
    throw std::runtime_error("get_peers timer error: " + e.message());
  }
  get_peers_timer.expires_at(refresh_nodes_timer.expiry() +
      boost::asio::chrono::seconds(dht_->config_.get_peers_refresh_interval_seconds));

  get_peers_timer.async_wait(
      boost::bind(
          &DHTImpl::handle_get_peers_timer,
          this,
          boost::asio::placeholders::error));

  dht_->get_peers_manager_->cleanup_expired();
}

}
