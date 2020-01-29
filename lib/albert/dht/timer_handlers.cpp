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
  if (dht_->config_.debug) {
    LOG(info) << "Main routing table debug mode enabled";
    dht_->main_routing_table_->stat();
    LOG(info) << "self NodeInfo " << dht_->self_info_.to_string();
    LOG(info) << "total ping query sent: " << dht_->total_ping_query_sent_;
    LOG(info) << "total ping query received: " << dht_->total_ping_query_received_;
    LOG(info) << "total ping response received: " << dht_->total_ping_response_received_;
    for (auto &rt : dht_->routing_tables_) {
      LOG(info) << "Routing table '" + rt->name() + "' debug mode enabled";
      rt->stat();
      LOG(info) << "self NodeInfo " << dht_->self_info_.to_string();
      LOG(info) << "total ping query sent: " << dht_->total_ping_query_sent_;
      LOG(info) << "total ping query received: " << dht_->total_ping_query_received_;
      LOG(info) << "total ping response received: " << dht_->total_ping_response_received_;
    }
  } else {
    LOG(info) << "main routing table "
              << dht_->main_routing_table_->max_prefix_length() << " "
              << dht_->main_routing_table_->good_node_count() << " "
              << dht_->main_routing_table_->known_node_count();
    for (auto &rt : dht_->routing_tables_) {
      if (rt->name() != dht_->main_routing_table_->name()) {
        LOG(info) << "Routing table '" << rt->name() << "' "
                  << rt->max_prefix_length() << " "
                  << rt->good_node_count() << " "
                  << rt->known_node_count();
      }
    }
  }
}
void DHTImpl::handle_expand_route_timer(const Timer::Cancel &cancel) {
  for (auto &rt : dht_->routing_tables_) {
    if (!rt->is_full()) {
      LOG(debug) << "sending find node query and find_self()...";
      auto targets = rt->select_expand_route_targets();
      Entry node;
      krpc::NodeID target_id;
      for (auto &item : targets) {
        std::tie(node, target_id) = item;

        auto find_node_query = std::make_shared<krpc::FindNodeQuery>(self(), target_id);
        udp::endpoint ep{boost::asio::ip::make_address_v4(node.ip()), node.port()};
        socket.async_send_to(
            boost::asio::buffer(dht_->create_query(find_node_query, rt.get())),
            ep,
            default_handle_send());
        find_self(*rt, udp::endpoint{boost::asio::ip::address_v4(node.ip()), node.port()});
      }
    }

  }
}
void DHTImpl::handle_refresh_nodes_timer(const Timer::Cancel &cancel) {
  for (auto &rt : dht_->routing_tables_) {
    rt->gc();

    // try refreshing questionable nodes
    rt->iterate_nodes([this, &rt](const Entry &node) {
      if (!node.is_good()) {
        if (!rt->require_response_now(node.id())) {
          LOG(error) << "Node gone when iterating " << node.to_string();
        }
        ping(krpc::NodeInfo{node.id(), node.ip(), node.port()});
      }
    });
  }

}
}