#pragma once

#include <memory>

#include <albert/krpc/krpc.hpp>

namespace albert::dht {
class DHT;
class DHTImpl;
class RoutingTable;
}

namespace albert::dht::sample_infohashes {
class SampleInfohashesManager {
 public:
  SampleInfohashesManager(DHT &dht, DHTImpl &impl, std::function<void (const krpc::NodeID &)> handler);
  void handle(const krpc::SampleInfohashesResponse &response);
 private:
  DHTImpl &impl_;
  DHT &dht_;
  dht::RoutingTable *routing_table_;
  std::function<void (const krpc::NodeID &)> handler_;
};
}
