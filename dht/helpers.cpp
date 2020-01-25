#include "dht_impl.hpp"

#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>

#include <dht/dht.hpp>
#include <krpc/krpc.hpp>
#include "get_peers.hpp"


namespace dht {

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
      boost::asio::buffer(dht_->create_query(ping_query)),
      ep,
      default_handle_send());
  dht_->total_ping_query_sent_++;
}

void DHTImpl::find_self(const udp::endpoint &ep) {
  // bootstrap by finding self
  auto find_node_query = std::make_shared<krpc::FindNodeQuery>(self(), self());
  socket.async_send_to(
      boost::asio::buffer(
          dht_->create_query(find_node_query)
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
  bool added = dht_->routing_table.add_node(
      Entry(
          sender_id,
          sender_endpoint.address().to_v4().to_uint(),
          sender_endpoint.port()));
  if (added) {
    dht_->routing_table.make_good_now(sender_id);
  } else {
    LOG(error) << "Routing table full. Did not add new good sender";
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
      boost::asio::buffer(dht_->create_query(query)),
      ep,
      default_handle_send());
}
void DHTImpl::handle_read_input(
    const boost::system::error_code &error,
    std::size_t bytes_transferred) {

  if (!error) {
    this->dht_get_peers(krpc::NodeID::from_hex("C8DB9C5B37C71D0F3B28788B94B8EFA5D2D92731"));

    std::vector<char> data(input_buffer_.size());
    input_buffer_.sgetn(data.data(), data.size());
    input_buffer_.consume(input_buffer_.size());
    input_ss_.write(data.data(), data.size());

    std::string command_line = input_ss_.str();
    std::vector<std::string> cmd;
    boost::split(cmd, command_line, boost::is_any_of("\t "));

    if (cmd.size() == 2) {
      auto function_name = cmd[0];
      if (function_name == "get-peers") {
        auto info_hash = cmd[1];
        this->dht_get_peers(krpc::NodeID::from_hex(info_hash));
      } else {
        LOG(error) << "Unknown function name " << function_name;
      }
    } else {
      LOG(error) << "Invalid command size " << cmd.size();
    }

  } else if (error == boost::asio::error::not_found) {
    std::vector<char> data(input_buffer_.size());
    input_buffer_.sgetn(data.data(), data.size());
    input_buffer_.consume(input_buffer_.size());
    input_ss_.write(data.data(), data.size());
    /* ignore if new line not reached */
  } else {
    LOG(error) << "Failed to read from stdin " << error.message();
  }

  boost::asio::async_read_until(
      input_,
      input_buffer_,
      '\n',
      boost::bind(
          &DHTImpl::handle_read_input,
          this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred)
  );

}

}
