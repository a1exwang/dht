#include "dht_impl.hpp"
#include "get_peers.hpp"

#include <dht/dht.hpp>


namespace dht {

void DHTImpl::handle_get_peers_response(
    const krpc::GetPeersResponse &response,
    const dht::Transaction &transaction
) {
  auto transaction_id = response.transaction_id();
  auto query = std::dynamic_pointer_cast<krpc::GetPeersQuery>(transaction.query_node_);
  if (dht_->get_peers_manager_->has_request(query->info_hash())) {
    if (response.has_peers()) {
      uint32_t ip;
      uint16_t port;
      for (auto item : response.peers()) {
        std::tie(ip, port) = item;
        dht_->get_peers_manager_->add_peer(query->info_hash(), ip, port);
      }
      dht_->get_peers_manager_->done(query->info_hash());
    } else {
      dht_->get_peers_manager_->delete_node(query->info_hash(), response.sender_id());
      for (auto &node : response.nodes()) {
        dht_->get_peers_manager_->add_node(query->info_hash(), node);
      }
    }
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
  for (auto it = nodes_.begin(); it != nodes_.end(); it++) {
    if (it->id() == id) {
      nodes_.erase(it);
      return;
    }
  }
}
void get_peers::GetPeersRequest::add_node(const krpc::NodeInfo &node) {
  nodes_.insert(node);
}

}
