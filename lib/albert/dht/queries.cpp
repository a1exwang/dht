#include "dht_impl.hpp"

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <albert/dht/dht.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>

namespace albert::dht {

void DHTImpl::handle_ping_query(const krpc::PingQuery &query) {
  krpc::PingResponse res(query.transaction_id(), self());

  socket.async_send_to(
      boost::asio::buffer(
          dht_->create_response(res)
      ),
      sender_endpoint,
      default_handle_send());
  dht_->total_ping_query_received_++;

  good_sender(query.sender_id());
}

void DHTImpl::handle_find_node_query(const krpc::FindNodeQuery &query) {
  auto nodes = dht_->routing_table->k_nearest_good_nodes(query.target_id(), BucketMaxGoodItems);
  std::vector<krpc::NodeInfo> info;
  for (auto &node : nodes) {
    info.push_back(node.node_info());
  }
  send_find_node_response(
      query.transaction_id(),
      krpc::NodeInfo{
          query.sender_id(),
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
//  std::vector<krpc::NodeInfo> nodes{dht_->self_info_};
  std::vector<krpc::NodeInfo> nodes;
  std::string token = "hello, world";
  krpc::GetPeersResponse response(
      query.transaction_id(),
      krpc::ClientVersion,
      self(),
      token,
      nodes
  );
  socket.async_send_to(
      boost::asio::buffer(dht_->create_response(response)),
      sender_endpoint,
      default_handle_send());
  dht_->message_counters_[krpc::MessageTypeResponse + ":"s + krpc::MethodNameFindNode]++;

//  LOG(warning) << "GetPeers Query ignored";
  good_sender(query.sender_id());
}
void DHTImpl::handle_announce_peer_query(const krpc::AnnouncePeerQuery &query) {
  // TODO
//    LOG(warning) << "AnnouncePeer Query ignored" << std::endl;
  LOG(info) << "Received info_hash from '" << query.sender_id().to_string() << " " << sender_endpoint << "' ih='"
            << query.info_hash().to_string() << "'";
  dht_->got_info_hash(query.info_hash());
  good_sender(query.sender_id());
}

}