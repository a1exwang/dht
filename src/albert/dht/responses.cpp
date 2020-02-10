#include "dht_impl.hpp"

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <albert/dht/dht.hpp>
#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/dht/sample_infohashes/sample_infohashes_manager.hpp>
#include <albert/dht/transaction.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>
#include <albert/io_latency/function_latency.hpp>
#include "get_peers.hpp"

namespace albert::dht {

void DHTImpl::handle_ping_response(const krpc::PingResponse &response) {
  LOG(trace) << "received ping response from '" << response.node_id().to_string() << "'";
  good_sender(response.node_id(), response.version());
  dht_->total_ping_response_received_++;
}

void DHTImpl::handle_find_node_response(const krpc::FindNodeResponse &response, routing_table::RoutingTable *routing_table) {
  for (auto &target_node : response.nodes()) {
    if (target_node.id() == self()) {
      LOG(info) << "got self id by find_node response from " << sender_endpoint << ", " << response.sender_id().to_string();
    } else {
      dht::routing_table::Entry entry(target_node, response.version());
      // not self and not in black list
      if (!(target_node.id() == self()) &&
          !(target_node.ip() == dht_->self_info_.ip() && target_node.port() == dht_->self_info_.port()) &&
          target_node.port() != 0 &&
          !dht_->in_black_list(target_node.ip(), target_node.port())) {
        routing_table->add_node(entry);
      }
    }
  }
  good_sender(response.sender_id(), response.version());
}

void DHTImpl::handle_sample_infohashes_response(
    const krpc::SampleInfohashesResponse &response) {

  // TODO
//  sample_infohashes_manager_->handle(response);

  good_sender(response.sender_id(), response.version());
}

}

