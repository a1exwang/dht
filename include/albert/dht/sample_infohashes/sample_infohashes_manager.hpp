#pragma once

#include <memory>
#include <set>

#include <boost/asio/steady_timer.hpp>

#include <albert/krpc/krpc.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}

namespace albert::dht {
class DHT;
class DHTImpl;
class RoutingTable;
}

namespace albert::dht::sample_infohashes {
class SampleInfohashesManager {
 public:
  SampleInfohashesManager(boost::asio::io_service &io, DHT &dht,
                          DHTImpl &impl, std::function<void (const krpc::NodeID &)> handler);
  void handle(const krpc::SampleInfohashesResponse &response);

 private:

  void handle_timer(const boost::system::error_code &error);
 private:
  boost::asio::io_service &io_;
  DHTImpl &impl_;
  DHT &dht_;

  krpc::NodeID current_target_;
  dht::RoutingTable *routing_table_;
  std::function<void (const krpc::NodeID &)> handler_;
  std::set<krpc::NodeID> traversed_;

  boost::asio::steady_timer action_timer_;
};
}
