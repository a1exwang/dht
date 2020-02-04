#pragma once

#include <memory>
#include <set>

#include <boost/asio/steady_timer.hpp>

#include <albert/krpc/krpc.hpp>
#include <albert/u160/u160.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}

namespace albert::dht {
class DHT;
class DHTImpl;
namespace routing_table {
class RoutingTable;
}
}

namespace albert::dht::sample_infohashes {
class SampleInfohashesManager {
 public:
  SampleInfohashesManager(boost::asio::io_service &io, DHT &dht,
                          DHTImpl &impl, std::function<void (const u160::U160 &)> handler);
  void handle(const krpc::SampleInfohashesResponse &response);

 private:

  void handle_timer(const boost::system::error_code &error);
 private:
  boost::asio::io_service &io_;
  DHTImpl &impl_;
  DHT &dht_;

  u160::U160 current_target_;
  dht::routing_table::RoutingTable *routing_table_;
  std::function<void (const u160::U160 &)> handler_;
  std::set<u160::U160> traversed_;

  boost::asio::steady_timer action_timer_;
};
}
