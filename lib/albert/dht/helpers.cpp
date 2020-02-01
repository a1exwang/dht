#include "dht_impl.hpp"

#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>

#include <albert/dht/dht.hpp>
#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/dht/sample_infohashes/sample_infohashes_manager.hpp>
#include <albert/krpc/krpc.hpp>
#include "get_peers.hpp"


namespace albert::dht {

void DHTImpl::continue_receive() {
  socket.async_receive_from(
      boost::asio::buffer(receive_buffer),
      sender_endpoint,
      boost::bind(&DHTImpl::handle_receive_from, this,
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred));

}
std::function<void(const boost::system::error_code &, size_t)>
DHTImpl::default_handle_send() {
  return boost::bind(
      &DHTImpl::handle_send,
      this,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred);
}

[[nodiscard]]
krpc::NodeID DHTImpl::self() const {
  return dht_->self_info_.id();
}

void DHTImpl::send_find_node_response(
    const std::string &transaction_id,
    const krpc::NodeInfo &receiver,
    const std::vector<krpc::NodeInfo> &nodes) {

  auto response = std::make_shared<krpc::FindNodeResponse>(
      transaction_id,
      self(),
      nodes
  );
  udp::endpoint ep{boost::asio::ip::make_address_v4(receiver.ip()), receiver.port()};
  socket.async_send_to(
      boost::asio::buffer(dht_->create_response(*response)),
      ep,
      default_handle_send());
  dht_->message_counters_[krpc::MessageTypeResponse + ":"s + krpc::MethodNameFindNode]++;
}
void DHTImpl::ping(const krpc::NodeInfo &target) {
  auto ping_query = std::make_shared<krpc::PingQuery>(self());
  udp::endpoint ep{boost::asio::ip::make_address_v4(target.ip()), target.port()};
  socket.async_send_to(
      boost::asio::buffer(dht_->create_query(ping_query, nullptr)),
      ep,
      default_handle_send());
  dht_->total_ping_query_sent_++;
}

void DHTImpl::find_self(routing_table::RoutingTable &rt, const udp::endpoint &ep) {
  // bootstrap by finding self
  auto id = rt.self();
  auto find_node_query = std::make_shared<krpc::FindNodeQuery>(id, id);
  socket.async_send_to(
      boost::asio::buffer(
          dht_->create_query(find_node_query, &rt)
      ),
      ep,
      boost::bind(
          &DHTImpl::handle_send,
          this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
}

void DHTImpl::handle_send(const boost::system::error_code &error, std::size_t bytes_transferred) {
  if (error || bytes_transferred <= 0) {
    LOG(error) << "sendto failed: " << error.message();
  }
}
void DHTImpl::good_sender(const krpc::NodeID &sender_id) {
  for (auto &rt : dht_->routing_tables_) {
    bool added = rt->add_node(
        routing_table::Entry(
            sender_id,
            sender_endpoint.address().to_v4().to_uint(),
            sender_endpoint.port()));
    if (added) {
      rt->make_good_now(sender_id);
    } else {
      LOG(error) << "Routing table(" + rt->name() + " ) full. Did not add new good sender";
    }
  }
}
void DHTImpl::send_get_peers_query(const krpc::NodeID &info_hash, const krpc::NodeInfo &receiver) {
  dht_->get_peers_manager_->add_node(info_hash, receiver.id());
  auto query = std::make_shared<krpc::GetPeersQuery>(
      self(),
      info_hash
  );
  udp::endpoint ep{boost::asio::ip::make_address_v4(receiver.ip()), receiver.port()};
  socket.async_send_to(
      boost::asio::buffer(dht_->create_query(query, dht_->main_routing_table_)),
      ep,
      default_handle_send());
}
void DHTImpl::bootstrap_routing_table(routing_table::RoutingTable &routing_table) {
  // send bootstrap message to bootstrap nodes
  for (const auto &item : this->dht_->config_.bootstrap_nodes) {
    std::string node_host{}, node_port{};
    std::tie(node_host, node_port) = item;

    udp::resolver resolver(io);
    udp::endpoint ep;
    try {
      for (auto &host : resolver.resolve(udp::resolver::query(node_host, node_port))) {
        if (host.endpoint().protocol() == udp::v4()) {
          ep = host.endpoint();
          break;
        }
      }
    } catch (std::exception &e) {
      LOG(error) << "DHTImpl::bootstrap(), failed to resolve '" << node_host << ":" << node_port << ", skipping, reason: " << e.what();
      break;
    }

    find_self(routing_table, ep);
  }

}
void DHTImpl::send_sample_infohashes_query(const krpc::NodeID &target, const krpc::NodeInfo &receiver) {
  auto query = std::make_shared<krpc::SampleInfohashesQuery>(
      self(),
      target
  );
  udp::endpoint ep{boost::asio::ip::make_address_v4(receiver.ip()), receiver.port()};
  socket.async_send_to(
      boost::asio::buffer(dht_->create_query(query, nullptr)),
      ep,
      default_handle_send());
}
void DHTImpl::set_announce_peer_handler(std::function<void(const krpc::NodeID &info_hash)> handler) {
  this->announce_peer_handler_ = std::move(handler);
}
krpc::NodeID DHTImpl::maybe_fake_self(const krpc::NodeID &target) const {
  krpc::NodeID self_id;
  if (dht_->config_.fake_id) {
    self_id = self().fake(target, dht_->config_.fake_id_prefix_length);
  } else {
    self_id = self();
  }
  return self_id;
}
void DHTImpl::bad_sender() {
  uint32_t ip = sender_endpoint.address().to_v4().to_uint();
  uint16_t port = sender_endpoint.port();
  dht_->add_to_black_list(ip, port);
  for (auto &rt : dht_->routing_tables_) {
    rt->make_bad(ip, port);
  }
}

}
