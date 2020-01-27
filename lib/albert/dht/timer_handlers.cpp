#include "dht_impl.hpp"


#include <boost/asio.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/bind.hpp>

#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>

namespace albert::dht {

void DHTImpl::handle_report_stat_timer(const Timer::Cancel &cancel) {
  bool simple = true;
  if (simple) {
    LOG(info) << "routing table "
              << dht_->routing_table->max_prefix_length() << " "
              << dht_->routing_table->good_node_count() << " "
              << dht_->routing_table->known_node_count();
  } else {
    dht_->routing_table->stat();
    LOG(info) << "self NodeInfo " << dht_->self_info_.to_string();
    LOG(info) << "total ping query sent: " << dht_->total_ping_query_sent_;
    LOG(info) << "total ping query received: " << dht_->total_ping_query_received_;
    LOG(info) << "total ping response received: " << dht_->total_ping_response_received_;
  }
}
void DHTImpl::handle_expand_route_timer(const Timer::Cancel &cancel) {
  if (!dht_->routing_table->is_full()) {
    LOG(debug) << "sending find node query and find_self()...";
    auto targets = dht_->routing_table->select_expand_route_targets();
    Entry node;
    krpc::NodeID target_id;
    for (auto &item : targets) {
      std::tie(node, target_id) = item;

      auto find_node_query = std::make_shared<krpc::FindNodeQuery>(self(), target_id);
      udp::endpoint ep{boost::asio::ip::make_address_v4(node.ip()), node.port()};
      socket.async_send_to(
          boost::asio::buffer(dht_->create_query(find_node_query)),
          ep,
          default_handle_send());
      find_self(udp::endpoint{boost::asio::ip::address_v4(node.ip()), node.port()});
    }
  }
}
void DHTImpl::handle_refresh_nodes_timer(const Timer::Cancel &cancel) {
  dht_->routing_table->gc();

  // try refreshing questionable nodes
  dht_->routing_table->iterate_nodes([this](const Entry &node) {
    if (!node.is_good()) {
      if (!dht_->routing_table->require_response_now(node.id())) {
        LOG(error) << "Node gone when iterating " << node.to_string();
      }
      ping(krpc::NodeInfo{node.id(), node.ip(), node.port()});


      // sample_infohashes
//      auto sample_infohashes_query = std::make_shared<krpc::SampleInfohashesQuery>(self(), self());
//      udp::endpoint ep{boost::asio::ip::make_address_v4(node.ip()), node.port()};
//      socket.async_send_to(
//          boost::asio::buffer(dht_->create_query(sample_infohashes_query)),
//          ep,
//          default_handle_send());
    }
  });
}
}