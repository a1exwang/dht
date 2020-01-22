#include "dht_impl.hpp"

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <dht/dht.hpp>
#include <krpc/krpc.hpp>
#include <utils/log.hpp>

namespace dht {

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
  auto nodes = dht_->routing_table.k_nearest_good_nodes(query.target_id(), BucketMaxGoodItems);
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
//    std::vector<krpc::NodeInfo> nodes{dht_->self_info_};
//    std::string token = "hello, world";
//    krpc::GetPeersResponse response(
//        query.transaction_id(),
//        krpc::ClientVersion,
//        self(),
//        token,
//        nodes
//    );
//    socket.async_send_to(
//        boost::asio::buffer(dht_->create_response(response)),
//        sender_endpoint,
//        default_handle_send());
//    dht_->message_counters_[krpc::MessageTypeResponse + ":"s + krpc::MethodNameFindNode]++;

  LOG(warning) << "GetPeers Query ignored";
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

void DHTImpl::handle_receive_from(const boost::system::error_code &error, std::size_t bytes_transferred) {
  if (error || bytes_transferred <= 0) {
    LOG(error) << "receive failed: " << error.message();
    return;
  }

  // parse receive data into a Message
  std::stringstream ss(std::string(receive_buffer.data(), bytes_transferred));
  std::shared_ptr<krpc::Message> message;
  try {
    auto node = bencoding::Node::decode(ss);
    message = krpc::Message::decode(*node, [this](std::string id) -> std::string {
      std::string method_name{};
      if (dht_->transaction_manager.has_transaction(id)) {
        this->dht_->transaction_manager.end(id, [&method_name](const dht::Transaction &transaction) {
          method_name = transaction.method_name_;
        });
      }
      return method_name;
    });
  } catch (const bencoding::InvalidBencoding &e) {
    LOG(error) << "Invalid bencoding, e: '" << e.what() << "', ignored";
  } catch (const krpc::InvalidMessage &e) {
    LOG(error) << "InvalidMessage, e: '" << e.what() << "', ignored";
    continue_receive();
    return;
  }

  bool has_error = false;
  if (auto response = std::dynamic_pointer_cast<krpc::Response>(message); response) {
    if (auto find_node_response = std::dynamic_pointer_cast<krpc::FindNodeResponse>(response); find_node_response) {
      handle_find_node_response(*find_node_response);
    } else if (auto ping_response = std::dynamic_pointer_cast<krpc::PingResponse>(response); ping_response) {
      handle_ping_response(*ping_response);
    } else {
      LOG(error) << "Warning! response type not supported";
      has_error = true;
    }
  } else if (auto query = std::dynamic_pointer_cast<krpc::Query>(message); query) {
    if (auto ping = std::dynamic_pointer_cast<krpc::PingQuery>(query); ping) {
      handle_ping_query(*ping);
    } else if (auto find_node_query = std::dynamic_pointer_cast<krpc::FindNodeQuery>(query); find_node_query) {
      handle_find_node_query(*find_node_query);
    } else if (auto get_peers_query = std::dynamic_pointer_cast<krpc::GetPeersQuery>(query); get_peers_query) {
      handle_get_peers_query(*get_peers_query);
    } else if (auto
          announce_peer_query = std::dynamic_pointer_cast<krpc::AnnouncePeerQuery>(query); announce_peer_query) {
      handle_announce_peer_query(*announce_peer_query);
    } else {
      LOG(error) << "Warning! query type not supported";
      has_error = true;
    }
  } else if (auto dht_error = std::dynamic_pointer_cast<krpc::Error>(message); dht_error) {
    LOG(error) << "DHT Error message from " << sender_endpoint << ", '" << dht_error->message() << "'";
  } else {
    LOG(error) << "Unknown message type";
    has_error = true;
  }

  // A node is also good if it has ever responded to one of our queries and has sent us a query within the last 15 minutes
  if (!has_error) {
    bool found = dht_->routing_table.make_good_now(
        sender_endpoint.address().to_v4().to_uint(),
        sender_endpoint.port()
    );
    if (!found) {
      LOG(debug) << "A stranger has sent us a message, not updating routing table";
    }
  }
  continue_receive();
}


}