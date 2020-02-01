#include "dht_impl.hpp"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <albert/dht/dht.hpp>
#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>
#include <albert/utils/utils.hpp>

namespace albert::dht {

void DHTImpl::handle_ping_query(const krpc::PingQuery &query) {
  krpc::PingResponse res(query.transaction_id(), maybe_fake_self(query.sender_id()));

  socket.async_send_to(
      boost::asio::buffer(
          dht_->create_response(res)
      ),
      sender_endpoint,
      default_handle_send("ping query to " + query.sender_id().to_string()));
  dht_->total_ping_query_received_++;

  good_sender(query.sender_id());
}

void DHTImpl::handle_find_node_query(const krpc::FindNodeQuery &query) {
  auto nodes = dht_->main_routing_table_->k_nearest_good_nodes(query.target_id(), routing_table::BucketMaxGoodItems);
  std::vector<krpc::NodeInfo> info{};
  for (auto &node : nodes) {
    info.push_back(node.node_info());
  }

  send_find_node_response(
      query.transaction_id(),
      krpc::NodeInfo{
          maybe_fake_self(query.sender_id()),
          sender_endpoint.address().to_v4().to_uint(),
          sender_endpoint.port()
      },
      info
  );
  good_sender(query.sender_id());
}

void DHTImpl::handle_get_peers_query(const krpc::GetPeersQuery &query) {
  // TODO: implement complete get peers

  /**
   * Currently we only return self as closer nodes
   */
  std::vector<krpc::NodeInfo> nodes{};
  if (dht_->config_.fake_id) {
    auto self_id = maybe_fake_self(query.sender_id());
    nodes.push_back(krpc::NodeInfo(self_id, dht_->self_info_.ip(), dht_->self_info_.port()));
  }

  std::string token(6, 0);
  std::random_device rng{};
  std::uniform_int_distribution<char> dist;
  std::generate(token.begin(), token.end(), [&]() { return dist(rng); });
  krpc::GetPeersResponse response(
      query.transaction_id(),
      krpc::ClientVersion,
      maybe_fake_self(query.sender_id()),
      token,
      nodes
  );
  socket.async_send_to(
      boost::asio::buffer(dht_->create_response(response)),
      sender_endpoint,
      default_handle_send("get_peers query " + query.sender_id().to_string()));
  dht_->message_counters_[krpc::MessageTypeResponse + ":"s + krpc::MethodNameFindNode]++;

  LOG(debug) << "get_peers query received from " << sender_endpoint
            << " token: '" << albert::dht::utils::hexdump(token.data(), token.size(), false) << "'";
  good_sender(query.sender_id());
}
void DHTImpl::handle_announce_peer_query(const krpc::AnnouncePeerQuery &query) {
  if (announce_peer_handler_) {
    announce_peer_handler_(query.info_hash());
  }
  krpc::AnnouncePeerResponse response(
      query.transaction_id(),
      maybe_fake_self(query.sender_id())
  );
  socket.async_send_to(
      boost::asio::buffer(dht_->create_response(response)),
      sender_endpoint,
      default_handle_send("announce_peer query " + query.sender_id().to_string()));
  dht_->message_counters_[krpc::MessageTypeResponse + ":"s + krpc::MethodNameFindNode]++;

  good_sender(query.sender_id());
}

}