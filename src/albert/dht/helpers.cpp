#include "dht_impl.hpp"

#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio/placeholders.hpp>

#include <albert/dht/dht.hpp>
#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/dht/sample_infohashes/sample_infohashes_manager.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/u160/u160.hpp>

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
DHTImpl::default_handle_send(const std::string &description) {
  return boost::bind(
      &DHTImpl::handle_send,
      this,
      description,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred);
}

[[nodiscard]]
u160::U160 DHTImpl::self() const {
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
      default_handle_send("find_node to " + receiver.to_string()));
  dht_->message_counters_[krpc::MessageTypeResponse + ":"s + krpc::MethodNameFindNode]++;
}
void DHTImpl::ping(const krpc::NodeInfo &target) {
  auto ping_query = std::make_shared<krpc::PingQuery>(self());
  udp::endpoint ep{boost::asio::ip::make_address_v4(target.ip()), target.port()};

  std::stringstream ep_ss;
  ep_ss << ep;
  socket.async_send_to(
      boost::asio::buffer(dht_->create_query(ping_query, nullptr)),
      ep,
      default_handle_send("ping " + ep_ss.str()));
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
      default_handle_send(std::string("find_self to " + ep.address().to_string() + ":" + std::to_string(ep.port())))
  );
}
void DHTImpl::find_node(routing_table::RoutingTable &rt, const udp::endpoint &ep, u160::U160 target) {
  // bootstrap by finding self
  auto id = rt.self();
  auto find_node_query = std::make_shared<krpc::FindNodeQuery>(id, target);
  socket.async_send_to(
      boost::asio::buffer(
          dht_->create_query(find_node_query, &rt)
      ),
      ep,
      default_handle_send(std::string("find_node to " + ep.address().to_string() + ":" + std::to_string(ep.port())))
  );
}

void DHTImpl::handle_send(const std::string &description, const boost::system::error_code &error, std::size_t bytes_transferred) {
  if (error || bytes_transferred <= 0) {
    LOG(error) << "DHTImpl: async_send_to '" << description << "' failed: " << error.message();
  }
}

void DHTImpl::good_sender(const u160::U160 &sender_id, const std::string &version) {
  for (auto &rt : dht_->routing_tables_) {
    bool added = rt->add_node(
        routing_table::Entry(
            sender_id,
            sender_endpoint.address().to_v4().to_uint(),
            sender_endpoint.port(),
            version));
    if (added) {
      LOG(debug) << "DHTImpl: good sender " << sender_id.to_string();
    }
    rt->make_good_now(sender_id);
  }
}
void DHTImpl::try_to_send_get_peers_query(const u160::U160 &info_hash, const krpc::NodeInfo &receiver) {
  if (dht_->get_peers_manager_->has_request(info_hash)) {
    dht_->get_peers_manager_->add_node(info_hash, receiver);
    auto query = std::make_shared<krpc::GetPeersQuery>(
        self(),
        info_hash
    );
    udp::endpoint ep{boost::asio::ip::make_address_v4(receiver.ip()), receiver.port()};
    socket.async_send_to(
        boost::asio::buffer(dht_->create_query(query, dht_->main_routing_table_)),
        ep,
        default_handle_send("get_peers " + info_hash.to_string() + ", to " + receiver.to_string()));
  }
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
void DHTImpl::send_sample_infohashes_query(const u160::U160 &target, const krpc::NodeInfo &receiver) {
  auto query = std::make_shared<krpc::SampleInfohashesQuery>(
      self(),
      target
  );
  udp::endpoint ep{boost::asio::ip::make_address_v4(receiver.ip()), receiver.port()};
  socket.async_send_to(
      boost::asio::buffer(dht_->create_query(query, nullptr)),
      ep,
      default_handle_send("sample_infohashes"));
}
void DHTImpl::set_announce_peer_handler(std::function<void(const u160::U160 &info_hash)> handler) {
  this->announce_peer_handler_ = std::move(handler);
}
u160::U160 DHTImpl::maybe_fake_self(const u160::U160 &target) const {
  u160::U160 self_id;
  if (dht_->config_.fake_id) {
    self_id = self().fake(target, dht_->config_.fake_id_prefix_length);
  } else {
    self_id = self();
  }
  return self_id;
}
bool DHTImpl::bad_sender() {
  uint32_t ip = sender_endpoint.address().to_v4().to_uint();
  uint16_t port = sender_endpoint.port();
  return dht_->add_to_black_list(ip, port);
}
bool DHTImpl::try_to_handle_unknown_message(std::shared_ptr<bencoding::Node> node) {
  // libtorrent/utorrent extension:
  // According to https://www.libtorrent.org/dht_extensions.html,
  // any message which is not recognized but has either an info_hash or target argument is interpreted as find node
  if (auto dict_node = std::dynamic_pointer_cast<bencoding::DictNode>(node); !dict_node) {
    return false;
  } else {
    auto &dict = dict_node->dict();
    std::string target;

    if (dict.find("info_hash") != dict.end()) {
      if (auto s = std::dynamic_pointer_cast<bencoding::StringNode>(dict.at("info_hash")); s) {
        target = *s;
      }
    } else if (dict.find("target") != dict.end()) {
      if (auto s = std::dynamic_pointer_cast<bencoding::StringNode>(dict.at("target")); s) {
        target = *s;
      }
    }

    std::string transaction_id = "unknown tx";
    if (dict.find("t") != dict.end()) {
      if (auto s = std::dynamic_pointer_cast<bencoding::StringNode>(dict.at("t")); s) {
        transaction_id = *s;
      }
    }

    u160::U160 sender_id;
    if (dict.find("id") != dict.end()) {
      if (auto s = std::dynamic_pointer_cast<bencoding::StringNode>(dict.at("id")); s) {
        transaction_id = *s;
      }
    }

    u160::U160 target_id;
    try {
      target_id = u160::U160::from_hex(target);
    } catch (const u160::InvalidFormat &) {
      return false;
    }

    auto nodes = dht_->main_routing_table_->k_nearest_good_nodes(target_id, routing_table::BucketMaxGoodItems);
    std::vector<krpc::NodeInfo> info{};
    for (auto &n : nodes) {
      info.push_back(n.node_info());
    }

    send_find_node_response(
        transaction_id,
        krpc::NodeInfo(
            maybe_fake_self(sender_id),
            sender_endpoint.address().to_v4().to_uint(),
            sender_endpoint.port()
        ),
        info
    );
  }
  return true;
}

}
