#include <albert/dht/sample_infohashes/sample_infohashes_manager.hpp>

#include <albert/log/log.hpp>
#include <albert/dht/routing_table.hpp>
#include <albert/dht/dht.hpp>
#include "../dht_impl.hpp"

namespace albert::dht::sample_infohashes {

SampleInfohashesManager::SampleInfohashesManager(DHT &dht, DHTImpl &impl, std::function<void (const krpc::NodeID &)> handler)
    :dht_(dht), impl_(impl), handler_(handler) {

  auto id = krpc::NodeID::random();
  auto rt = std::make_unique<dht::RoutingTable>(id, "sample_infohashes(" + id.to_string() + ")", "");
  routing_table_ = rt.get();
  dht_.add_routing_table(std::move(rt));
  impl_.bootstrap_routing_table(*routing_table_);
}


void SampleInfohashesManager::handle(const albert::krpc::SampleInfohashesResponse &response) {
  // TODO
  for (auto sample : response.samples()) {
    LOG(info) << "sample infohashes handle " << sample.to_string();
  }
}

}
