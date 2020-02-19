#include "dht_impl.hpp"


#include <boost/asio.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/bind.hpp>

#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/log/log.hpp>
#include <albert/utils/utils.hpp>

namespace albert::dht {

void DHTImpl::handle_report_stat_timer(const Timer::Cancel &cancel) {
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
//    std::stringstream black_list_s;
//    for (auto &item : dht_->black_list_) {
//      uint32_t ip = 0;
//      uint16_t port = 0;
//      std::tie(ip, port) = item;
//      black_list_s << boost::asio::ip::address_v4(ip) << ":" << port << " ";
//    }
    LOG(info) << "black list " << dht_->blacklist_.size() << " in total";
  } else {
    LOG(info) << "main routing table "
              << dht_->main_routing_table_->max_prefix_length() << " "
              << dht_->main_routing_table_->good_node_count() << " "
              << dht_->main_routing_table_->known_node_count() << " "
              << dht_->main_routing_table_->bucket_count() << " "
              << "banned " << dht_->blacklist_.size() << " "
              << "mem " << utils::pretty_size(dht_->main_routing_table_->memory_size()) << " "
              << "tx: (n,mem) " << dht_->transaction_manager.size() << "," << utils::pretty_size(dht_->transaction_manager.memory_size())
          ;
    for (auto &rt : dht_->routing_tables_) {
      if (rt->name() != dht_->main_routing_table_->name()) {
        LOG(info) << "Routing table '" << rt->name() << "' "
                  << rt->max_prefix_length() << " "
                  << rt->good_node_count() << " "
                  << rt->known_node_count() << " "
                  << rt->bucket_count();
      }
    }
  }
  LOG(info) << "DHT RPSThrottler: " << throttler_.stat();
}
void DHTImpl::handle_expand_route_timer(const Timer::Cancel &cancel) {
  for (auto &rt : dht_->routing_tables_) {
    if (!rt->is_full()) {
      LOG(debug) << "sending find node query and find_self()...";
      auto targets = rt->select_expand_route_targets();
      for (auto &item : targets) {
        auto &node = std::get<0>(item);
        auto &target_id = std::get<1>(item);

        udp::endpoint ep{boost::asio::ip::make_address_v4(node.ip()), node.port()};
        find_node(*rt, ep, target_id);
        find_self(*rt, ep);
      }
    }
  }
}

void DHTImpl::handle_refresh_nodes_timer(const Timer::Cancel &cancel) {
  dht_->blacklist_.gc();

  for (auto &rt : dht_->routing_tables_) {
    rt->gc();

    // try refreshing questionable nodes
    size_t n = 0;
    rt->iterate_nodes([this, &n](const routing_table::Entry &node) {
      if (!node.is_good() && !node.is_bad()) {
        // FIXME: const_cast
        if (const_cast<routing_table::Entry&>(node).require_response_now()) {
          ping(krpc::NodeInfo{node.id(), node.ip(), node.port()});
          n++;
        }
      }
    });
  }

  if (dht_->main_routing_table_->known_node_count() == 0) {
    bootstrap_routing_table(*dht_->main_routing_table_);
  }

  dht_->transaction_manager.gc();
}

}