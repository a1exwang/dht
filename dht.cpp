#include "dht.hpp"
#include <log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

using boost::asio::ip::udp;

namespace dht {

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
        report_stat_timer(io, boost::asio::chrono::seconds(dht->config_.report_interval_seconds)) { }

  void loop() {
    try {
      io.run();
      LOG(info) << "Successfully end";
    } catch (std::exception& e) {
      LOG(error) << "Exception: " << e.what();
      std::exit(EXIT_FAILURE);
    }
  }

  void handle_send(const boost::system::error_code &error, std::size_t bytes_transferred) {
    if (error || bytes_transferred <= 0) {
      LOG(error) << "sendto failed: " << error.message();
    }
  };


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
        std::string method_name;
        this->dht_->transaction_manager.end(id, [&method_name](const transaction::Transaction &transaction) {
          method_name = transaction.method_name_;
        });
        return method_name;
      });
    } catch (const krpc::InvalidMessage &e) {
      LOG(error) << "Invalid Message, e: " << e.what() << ", skipped package";
      continue_receive();
      return;
    }

    if (auto response = std::dynamic_pointer_cast<krpc::Response>(message); response) {
      if (auto find_node_response = std::dynamic_pointer_cast<krpc::FindNodeResponse>(response); find_node_response) {
//        find_node_response->print_nodes();
        for (auto &target_node : find_node_response->nodes()) {
          dht::Entry entry(target_node.id(), target_node.ip(), target_node.port());
          this->dht_->routing_table.add_node(entry);
        }
      } else if (auto ping_response = std::dynamic_pointer_cast<krpc::PingResponse>(response); ping_response) {
        LOG(info) << "received ping response";
      } else {
        LOG(error) << "Warning! response type not supported";
      }
    } else if(auto query = std::dynamic_pointer_cast<krpc::Query>(message); query) {
      LOG(info) << "query received, ignored";
    }
    continue_receive();
  }
  void continue_receive() {
    socket.async_receive_from(
        boost::asio::buffer(receive_buffer),
        sender_endpoint,
        boost::bind(&DHTImpl::handle_receive_from, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));

  }

  krpc::NodeID self() const {
    return dht_->self_node_id_;
  }

  void bootstrap() {
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
  }


  void send_find_node_query(const krpc::NodeInfo &target) {
    auto find_node_query = std::make_shared<krpc::FindNodeQuery>(self(), target.id());
    udp::endpoint ep{boost::asio::ip::make_address_v4(target.ip()), target.port()};
    socket.async_send_to(
        boost::asio::buffer(dht_->create_query(*find_node_query)),
        ep,
        boost::bind(&DHTImpl::handle_send, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
  }

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

    LOG(info) << "routing table " << dht_->routing_table.size();
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
      LOG(debug) << "sending find node query...";
      auto targets = dht_->routing_table.select_expand_route_targets();
      for (auto &target : targets) {
        send_find_node_query(
            krpc::NodeInfo{target.id(), target.ip(), target.port()}
        );
      }
    }
  }
 private:
  DHT *dht_;
  boost::asio::io_service io{};

  std::array<char, 65536> receive_buffer{};
  udp::socket socket;
  udp::endpoint sender_endpoint{};

  boost::asio::steady_timer expand_route_timer;
  boost::asio::steady_timer report_stat_timer;
};

DHT::DHT(const Config &config)
    :config_(config),
     self_node_id_(parse_node_id(config.self_node_id)),
     transaction_manager(),
     routing_table(self_node_id_),
     impl_(std::make_unique<DHTImpl>(this)) {
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

}
