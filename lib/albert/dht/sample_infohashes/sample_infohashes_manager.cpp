#include <albert/dht/sample_infohashes/sample_infohashes_manager.hpp>

#include <functional>
#include <utility>

#include <boost/asio/io_service.hpp>

#include <albert/log/log.hpp>
#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/dht/dht.hpp>
#include "../dht_impl.hpp"

namespace albert::dht::sample_infohashes {

SampleInfohashesManager::SampleInfohashesManager(
    boost::asio::io_service &io,
    DHT &dht,
    DHTImpl &impl,
    std::function<void (const krpc::NodeID &)> handler)
    :io_(io),
     dht_(dht),
     impl_(impl),
     current_target_(krpc::NodeID::random()),
     handler_(handler),
     action_timer_(io, boost::asio::chrono::seconds(0)) {

  auto rt = std::make_unique<dht::routing_table::RoutingTable>(
      current_target_, "sample_infohashes(" + current_target_.to_string() + ")", "",
      dht::routing_table::BucketMaxItems,
      16384,
      true, false,
      nullptr);
  routing_table_ = rt.get();
  dht_.add_routing_table(std::move(rt));
  impl_.bootstrap_routing_table(*routing_table_);

  action_timer_.async_wait(boost::bind(&SampleInfohashesManager::handle_timer, this, _1));
}

void SampleInfohashesManager::handle_timer(const boost::system::error_code &error) {
  if (error) {
    throw std::runtime_error("timer error: " + error.message());
  }
  routing_table_->iterate_nodes([this](const dht::routing_table::Entry &entry) {
    if (traversed_.find(entry.id()) == traversed_.end()) {
      impl_.send_sample_infohashes_query(current_target_, entry.node_info());
    }
  });

  action_timer_.expires_at(boost::asio::chrono::steady_clock::now() + boost::asio::chrono::seconds(5));
  action_timer_.async_wait(boost::bind(&SampleInfohashesManager::handle_timer, this, _1));
}


void SampleInfohashesManager::handle(const albert::krpc::SampleInfohashesResponse &response) {
  // TODO
  for (auto sample : response.samples()) {
    LOG(info) << "sample infohashes handle " << sample.to_string();
  }
}

}
