#include "dht_impl.hpp"

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <dht/dht.hpp>
#include <dht/transaction.hpp>
#include <krpc/krpc.hpp>
#include <utils/log.hpp>
#include "get_peers.hpp"

namespace dht {

void DHTImpl::handle_ping_response(const krpc::PingResponse &response) {
  LOG(trace) << "received ping response from '" << response.node_id().to_string() << "'";
  dht_->routing_table.add_node(Entry(response.node_id(),
                                     sender_endpoint.address().to_v4().to_uint(),
                                     sender_endpoint.port()));
  dht_->routing_table.make_good_now(response.node_id());
  dht_->total_ping_response_received_++;
}

void DHTImpl::handle_find_node_response(const krpc::FindNodeResponse &response) {
  for (auto &target_node : response.nodes()) {
    dht::Entry entry(target_node);
    this->dht_->routing_table.add_node(entry);
  }
  this->dht_->routing_table.add_node(
      Entry{
          response.sender_id(),
          sender_endpoint.address().to_v4().to_uint(),
          sender_endpoint.port()
      });
  this->dht_->routing_table.make_good_now(response.sender_id());
}


void DHTImpl::handle_sample_infohashes_response(
    const krpc::SampleInfohashesResponse &response) {

  for (auto &info_hash : response.samples()) {
    dht_->got_info_hash(info_hash);
  }
  this->dht_->routing_table.add_node(
      Entry{
          response.sender_id(),
          sender_endpoint.address().to_v4().to_uint(),
          sender_endpoint.port()
      });
  this->dht_->routing_table.make_good_now(response.sender_id());
}

}

