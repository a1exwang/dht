#include "dht.hpp"
#include <public_ip.hpp>
#include <log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

using boost::asio::ip::udp;

namespace dht {

using namespace std::string_literals;
class DHTImpl {
 public:
  friend class DHT;

  explicit DHTImpl(DHT *dht)
      : dht_(dht),
        io(),
        socket(io,
              udp::endpoint(
                  boost::asio::ip::address_v4::from_string(dht->config_.bind_ip),
                  dht->config_.bind_port)),
        expand_route_timer(io, boost::asio::chrono::seconds(dht->config_.discovery_interval_seconds)),
        report_stat_timer(io, boost::asio::chrono::seconds(dht->config_.report_interval_seconds)),
        refresh_nodes_timer(io, boost::asio::chrono::seconds(dht->config_.refresh_nodes_check_interval_seconds)) { }

  /**
   * DHT Message handlers
   */

  void handle_send(const boost::system::error_code &error, std::size_t bytes_transferred) {
    if (error || bytes_transferred <= 0) {
      LOG(error) << "sendto failed: " << error.message();
    }
  };

  void good_sender(const krpc::NodeID &sender_id) {
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

  void handle_ping_response(const krpc::PingResponse &response) {
    LOG(trace) << "received ping response from '" << response.node_id().to_string() << "'";
    dht_->routing_table.add_node(Entry(response.node_id(), sender_endpoint.address().to_v4().to_uint(), sender_endpoint.port()));
    dht_->routing_table.make_good_now(response.node_id());
    dht_->total_ping_response_received_++;
  }
  void handle_find_node_response(const krpc::FindNodeResponse &response) {
    for (auto &target_node : response.nodes()) {
      dht::Entry entry(target_node);
      this->dht_->routing_table.add_node(entry);
    }
    this->dht_->routing_table.add_node(
        Entry{
          response.sender_id(),
          sender_endpoint.address().to_v4().to_uint(),
          sender_endpoint.port()});
    this->dht_->routing_table.make_good_now(response.sender_id());
  }
  void handle_ping_query(const krpc::PingQuery &query) {
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
  void handle_find_node_query(const krpc::FindNodeQuery &query) {
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
          sender_endpoint.port()},
        info
    );
    good_sender(query.sender_id());
  }
  void handle_get_peers_query(const krpc::GetPeersQuery &query) {
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
  void handle_announce_peer_query(const krpc::AnnouncePeerQuery &query) {
    // TODO
//    LOG(warning) << "AnnouncePeer Query ignored" << std::endl;
    LOG(info) << "Received info_hash from '" << query.sender_id().to_string() << " " << sender_endpoint << "' ih='" << query.info_hash().to_string() << "'";
    dht_->got_info_hash(query.info_hash());
    good_sender(query.sender_id());
  }

  void handle_receive_from(const boost::system::error_code& error, std::size_t bytes_transferred) {
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
          this->dht_->transaction_manager.end(id, [&method_name](const transaction::Transaction &transaction) {
            method_name = transaction.method_name_;
          });
        }
        return method_name;
      });
    } catch (const bencoding::InvalidBencoding &e) {
      LOG(error) << "Invalid bencoding, e: '"<< e.what() << "', ignored";

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
    } else if(auto query = std::dynamic_pointer_cast<krpc::Query>(message); query) {
      if (auto ping = std::dynamic_pointer_cast<krpc::PingQuery>(query); ping) {
        handle_ping_query(*ping);
      } else if (auto find_node_query = std::dynamic_pointer_cast<krpc::FindNodeQuery>(query); find_node_query) {
        handle_find_node_query(*find_node_query);
      } else if (auto get_peers_query = std::dynamic_pointer_cast<krpc::GetPeersQuery>(query); get_peers_query) {
        handle_get_peers_query(*get_peers_query);
      } else if (auto announce_peer_query = std::dynamic_pointer_cast<krpc::AnnouncePeerQuery>(query); announce_peer_query) {
        handle_announce_peer_query(*announce_peer_query);
      } else {
        LOG(error) << "Warning! query type not supported";
        has_error = true;
      }
    } else if(auto dht_error = std::dynamic_pointer_cast<krpc::Error>(message); dht_error) {
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

  void find_self(const udp::endpoint &ep) {
    // bootstrap by finding self
    auto find_node_query = std::make_shared<krpc::FindNodeQuery>(self(), self());
    socket.async_send_to(
        boost::asio::buffer(
            dht_->create_query(*find_node_query)
        ),
        ep,
        boost::bind(
            &DHTImpl::handle_send,
            this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
  }

  void bootstrap() {
    dht_->self_info_.ip(public_ip::my_v4());
    dht_->self_info_.port(dht_->config_.bind_port);

    socket.async_receive_from(
        boost::asio::buffer(receive_buffer),
        sender_endpoint,
        boost::bind(&DHTImpl::handle_receive_from, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));

    for (const auto &item : this->dht_->config_.bootstrap_nodes) {
      std::string node_host{}, node_port{};
      std::tie(node_host, node_port) = item;

      udp::resolver resolver(io);
      udp::endpoint ep;
      try {
        for (auto &item : resolver.resolve(udp::resolver::query(node_host, node_port))) {
          if (item.endpoint().protocol() == udp::v4()) {
            ep = item.endpoint();
            break;
          }
        }
      } catch (std::exception &e) {
        LOG(error) << "failed to resolve '" << node_host << ":" << node_port << ", skipping, reason: " <<  e.what();
        break;
      }

      find_self(ep);
    }
    expand_route_timer.async_wait(
        boost::bind(
            &DHTImpl::handle_expand_route_timer,
            this,
            boost::asio::placeholders::error));

    expand_route_timer.async_wait(
        boost::bind(
            &DHTImpl::handle_report_stat_timer,
            this,
            boost::asio::placeholders::error));
    refresh_nodes_timer.async_wait(
        boost::bind(
            &DHTImpl::handle_refresh_nodes_timer,
            this,
            boost::asio::placeholders::error));
  }

  void loop() {
    try {
      io.run();
      LOG(info) << "Successfully end";
    } catch (std::exception& e) {
      LOG(error) << "Exception: " << e.what();
      std::exit(EXIT_FAILURE);
    }
  }

  /**
   * Helper functions
   */
  void continue_receive() {
    socket.async_receive_from(
        boost::asio::buffer(receive_buffer),
        sender_endpoint,
        boost::bind(&DHTImpl::handle_receive_from, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));

  }
  std::function<void(const boost::system::error_code &, size_t)>
  default_handle_send() {
    return boost::bind(
        &DHTImpl::handle_send,
        this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred);
  }

  [[nodiscard]]
  krpc::NodeID self() const {
    return dht_->self_info_.id();
  }

  void send_find_node_response(
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
  void ping(const krpc::NodeInfo &target) {
    auto ping_query = std::make_shared<krpc::PingQuery>(self());
    udp::endpoint ep{boost::asio::ip::make_address_v4(target.ip()), target.port()};
    socket.async_send_to(
        boost::asio::buffer(dht_->create_query(*ping_query)),
        ep,
        default_handle_send());
    dht_->total_ping_query_sent_++;
  }

  /**
   * Timer Handlers
   */
  void handle_report_stat_timer(const boost::system::error_code& e) {
    if (e) {
      LOG(error) << "report stat timer error: " << e.message();
      return;
    }
    report_stat_timer.expires_at(
        report_stat_timer.expiry() +
            boost::asio::chrono::seconds(dht_->config_.report_interval_seconds));
    report_stat_timer.async_wait(
        boost::bind(
            &DHTImpl::handle_report_stat_timer,
            this,
            boost::asio::placeholders::error));

//    LOG(info) << "routing table "
//              << dht_->routing_table.good_node_count()
//              << " " << dht_->routing_table.known_node_count();
    dht_->routing_table.stat();
    LOG(info) << "self NodeInfo " << dht_->self_info_.to_string();
    LOG(info) << "total ping query sent: " << dht_->total_ping_query_sent_;
    LOG(info) << "total ping query received: " << dht_->total_ping_query_received_;
    LOG(info) << "total ping response received: " << dht_->total_ping_response_received_;
  }
  void handle_expand_route_timer(const boost::system::error_code& e) {
    if (e) {
      LOG(error) << "Timer error: " << e.message();
      return;
    }

    expand_route_timer.expires_at(expand_route_timer.expiry() +
        boost::asio::chrono::seconds(dht_->config_.discovery_interval_seconds));

    expand_route_timer.async_wait(
        boost::bind(
            &DHTImpl::handle_expand_route_timer,
            this,
            boost::asio::placeholders::error));

    if (!dht_->routing_table.is_full()) {
      LOG(debug) << "sending find node query and find_self()...";
      auto targets = dht_->routing_table.select_expand_route_targets();
      Entry node;
      krpc::NodeID target_id;
      for (auto &item : targets) {
        std::tie(node, target_id) = item;

        auto find_node_query = std::make_shared<krpc::FindNodeQuery>(self(), target_id);
        udp::endpoint ep{boost::asio::ip::make_address_v4(node.ip()), node.port()};
        socket.async_send_to(
            boost::asio::buffer(dht_->create_query(*find_node_query)),
            ep,
            default_handle_send());
        find_self(udp::endpoint{boost::asio::ip::address_v4(node.ip()), node.port()});
      }
    }
  }
  void handle_refresh_nodes_timer(const boost::system::error_code& e) {
    if (e) {
      LOG(error) << "refresh nodes timer error: " << e.message();
      return;
    }

    refresh_nodes_timer.expires_at(refresh_nodes_timer.expiry() +
        boost::asio::chrono::seconds(dht_->config_.refresh_nodes_check_interval_seconds));

    refresh_nodes_timer.async_wait(
        boost::bind(
            &DHTImpl::handle_refresh_nodes_timer,
            this,
            boost::asio::placeholders::error));

    dht_->routing_table.gc();

    // try refreshing questionable nodes
    dht_->routing_table.iterate_nodes([this](const Entry &node) {
      if (!node.is_good()) {
        if (!dht_->routing_table.require_response_now(node.id())) {
          LOG(error) << "Node gone when iterating " << node.to_string();
        }
        ping(krpc::NodeInfo{node.id(), node.ip(), node.port()});
      }
    });
  }
 private:
  DHT *dht_;
  boost::asio::io_service io{};

  std::array<char, 65536> receive_buffer{};
  udp::socket socket;
  udp::endpoint sender_endpoint{};

  boost::asio::steady_timer expand_route_timer;
  boost::asio::steady_timer report_stat_timer;
  boost::asio::steady_timer refresh_nodes_timer;
};

/**
 * class dht::DHT
 */

DHT::DHT(const Config &config)
    :config_(config),
     self_info_(parse_node_id(config.self_node_id), 0, 0),
     transaction_manager(),
     routing_table(self_info_.id()),
     info_hash_list_stream_(config.info_hash_save_path, std::fstream::app),
     impl_(std::make_unique<DHTImpl>(this)) {
  if (!info_hash_list_stream_.is_open()) {
    throw std::runtime_error("Failed to open info hash list file '" + config.info_hash_save_path + "'");
  }
}
void DHT::loop() { impl_->loop(); }

std::unique_ptr<DHT> DHT::make(const Config &config) {
  return std::make_unique<DHT>(config);
}

DHT::~DHT() {}

void DHT::bootstrap() { impl_->bootstrap(); }
std::string DHT::create_query(krpc::Query &query) {
  transaction_manager.start([&query, this](transaction::Transaction &transaction) {
    transaction.method_name_ = query.method_name();
    query.set_transaction_id(transaction.id_);
  });
  std::stringstream ss;
  query.encode(ss, bencoding::EncodeMode::Bencoding);
  return ss.str();
}
krpc::NodeID DHT::parse_node_id(const std::string &s) {
  if (s.empty()) {
    return krpc::NodeID::random();
  } else {
    return krpc::NodeID::from_string(s);
  }
}
std::string DHT::create_response(const krpc::Response &query) {
  std::stringstream ss;
  query.encode(ss, bencoding::EncodeMode::Bencoding);
  return ss.str();
}
void DHT::got_info_hash(const krpc::NodeID &info_hash) {
  info_hash_list_stream_ << info_hash.to_string() << std::endl;
  info_hash_list_stream_.flush();
  if (!info_hash_list_stream_) {
    LOG(error) << "Failed to write to info hash list file '" << config_.info_hash_save_path << "'";
  }
}

}
