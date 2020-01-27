#include "dht_impl.hpp"

#include <string>

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/public_ip/public_ip.hpp>
#include <albert/utils/utils.hpp>

#include "get_peers.hpp"

using boost::asio::ip::udp;

namespace albert::dht {

using namespace std::string_literals;

DHTImpl::DHTImpl(DHT *dht)
    : dht_(dht),
      io(),
      socket(io,
             udp::endpoint(
                 boost::asio::ip::address_v4::from_string(dht->config_.bind_ip),
                 dht->config_.bind_port)),
      signals_(io, SIGINT),
      input_(io, ::dup(STDIN_FILENO)) {

  timers_.emplace_back(*this, "expand-route", &DHTImpl::handle_expand_route_timer,
                       dht_->config_.discovery_interval_seconds);
  timers_.emplace_back(*this, "report-stat", &DHTImpl::handle_report_stat_timer,
                       dht_->config_.report_interval_seconds);
  timers_.emplace_back(*this, "refresh-nodes",&DHTImpl::handle_refresh_nodes_timer,
                       dht_->config_.refresh_nodes_check_interval_seconds);
  timers_.emplace_back(*this, "get-peers",&DHTImpl::handle_get_peers_timer,
                       dht_->config_.get_peers_refresh_interval_seconds);

}


void DHTImpl::handle_receive_from(const boost::system::error_code &error, std::size_t bytes_transferred) {
  if (error || bytes_transferred <= 0) {
    LOG(error) << "receive failed: " << error.message();
    return;
  }

  // parse receive data into a Message
  std::stringstream ss(std::string(receive_buffer.data(), bytes_transferred));
  std::shared_ptr<krpc::Message> message;
  std::shared_ptr<krpc::Query> query_node;
  std::shared_ptr<bencoding::Node> node;
  try {
    node = bencoding::Node::decode(ss);
  } catch (const bencoding::InvalidBencoding &e) {
    LOG(error) << "Invalid bencoding, e: '" << e.what() << "', ignored " << std::endl
               << albert::dht::utils::hexdump(receive_buffer.data(), bytes_transferred, true);
    continue_receive();
    return;
  }
  try {
    message = krpc::Message::decode(*node, [this, &query_node, &node](std::string id) -> std::string {
      std::string method_name{};
      if (dht_->transaction_manager.has_transaction(id)) {
        this->dht_->transaction_manager.end(id, [&method_name, &query_node](const dht::Transaction &transaction) {
          method_name = transaction.method_name_;
          query_node = transaction.query_node_;
        });
      } else {
        std::stringstream ss;
        node->encode(ss, bencoding::EncodeMode::JSON);
        LOG(debug) << "Invalid message, transaction not found, transaction_id: '"
                   << dht::utils::hexdump(id.data(), id.size(), false) << "', bencoding: " << ss.str();
      }
      return method_name;
    });
  } catch (const krpc::InvalidMessage &e) {
    std::stringstream ss;
    node->encode(ss, bencoding::EncodeMode::JSON);
    LOG(debug) << "InvalidMessage, e: '" << e.what() << "', ignored, bencoding '" << ss.str() << "'";
    continue_receive();
    return;
  }

  bool has_error = false;
  if (auto response = std::dynamic_pointer_cast<krpc::Response>(message); response) {
    if (auto find_node_response = std::dynamic_pointer_cast<krpc::FindNodeResponse>(response); find_node_response) {
      handle_find_node_response(*find_node_response);
    } else if (auto ping_response = std::dynamic_pointer_cast<krpc::PingResponse>(response); ping_response) {
      handle_ping_response(*ping_response);
    } else if (auto get_peers_response = std::dynamic_pointer_cast<krpc::GetPeersResponse>(response); get_peers_response) {
      if (auto q = std::dynamic_pointer_cast<krpc::GetPeersQuery>(query_node); q) {
        handle_get_peers_response(*get_peers_response, *q);
      } else {
        LOG(error) << "Invalid get_peers response, Query type not get_peers";
      }
    } else if (auto sample_infohashes_res = std::dynamic_pointer_cast<krpc::SampleInfohashesResponse>(response); sample_infohashes_res) {
      handle_sample_infohashes_response(*sample_infohashes_res);
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
    } else if (auto announce_peer_query = std::dynamic_pointer_cast<krpc::AnnouncePeerQuery>(query); announce_peer_query) {
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
    bool found = dht_->routing_table->make_good_now(
        sender_endpoint.address().to_v4().to_uint(),
        sender_endpoint.port()
    );
    if (!found) {
      LOG(debug) << "A stranger has sent us a message, not updating routing table";
    }
  }
  continue_receive();
}


void DHTImpl::bootstrap() {

  // Start an asynchronous wait for one of the signals to occur.
  signals_.async_wait([this](const boost::system::error_code& error, int signal_number) {
    if (error) {
      LOG(error) << "Failed to async wait signals '" << error.message() << "'";
      return;
    }
    LOG(info) << "Exiting due to signal " << signal_number;
    io.stop();
  });

  // register receive_from handler
  socket.async_receive_from(
      boost::asio::buffer(receive_buffer),
      sender_endpoint,
      boost::bind(&DHTImpl::handle_receive_from, this,
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred));

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

    find_self(ep);
  }

  // set stdin read handler
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

  // start all timers
  for (auto &timer : timers_) {
    timer.fire_immediately();
  }
}

void DHTImpl::loop() {
  try {
    io.run();
    LOG(info) << "Successfully end";
  } catch (std::exception &e) {
    LOG(error) << "Exception: " << e.what();
    std::exit(EXIT_FAILURE);
  }
}


std::unique_ptr<DHT> DHT::make(Config config) {
  return std::make_unique<DHT>(std::move(config));
}
void DHT::loop() { impl_->loop(); }
void DHT::bootstrap() { impl_->bootstrap(); }
DHT::DHT(Config config)
    :config_(std::move(config)),
     self_info_(parse_node_id(config_.self_node_id), albert::public_ip::my_v4(), config_.bind_port),
     transaction_manager(),
     get_peers_manager_(std::make_unique<dht::get_peers::GetPeersManager>(config_.get_peers_request_expiration_seconds)),
     routing_table(nullptr),
     info_hash_list_stream_(config_.info_hash_save_path, std::fstream::app),
     impl_(std::make_unique<DHTImpl>(this)) {
  std::ifstream ifs(config_.routing_table_save_path);
  if (ifs) {
    LOG(info) << "Loading routing table from '" << config_.routing_table_save_path << "'";
    try {
      routing_table = dht::RoutingTable::deserialize(ifs, config_.routing_table_save_path);
    } catch (const std::exception &e) {
      LOG(info) << "Fail to load routing table, '" << e.what() << "', Creating empty routing table";
      routing_table = std::make_unique<dht::RoutingTable>(
          parse_node_id(config_.self_node_id),
          config_.routing_table_save_path);
    }
  } else {
    LOG(info) << "Creating empty routing table";
    routing_table = std::make_unique<dht::RoutingTable>(
        parse_node_id(config_.self_node_id),
        config_.routing_table_save_path);
  }
  if (!info_hash_list_stream_.is_open()) {
    throw std::runtime_error("Failed to open info hash list file '" + config_.info_hash_save_path + "'");
  }
}
DHT::~DHT() {}

void Timer::handler_timer(const boost::system::error_code &error) {
  if (error) {
    LOG(error) << "DHTImpl::Timer(" + name_ + ") error: " << error.message();
    return;
  }
  bool canceled = false;
  auto cancel = [&]() { canceled = true; };
  (that_.*handler_)(cancel);
  if (!canceled) {
    fire();
  }
}
void Timer::fire() {
  timer_.expires_at(boost::asio::chrono::steady_clock::now() + boost::asio::chrono::seconds(seconds_));
  timer_.async_wait(
      boost::bind(
          &Timer::handler_timer,
          this,
          boost::asio::placeholders::error));
}
void Timer::fire_immediately() {
  timer_.expires_at(boost::asio::chrono::steady_clock::now());
  timer_.async_wait(
      boost::bind(
          &Timer::handler_timer,
          this,
          boost::asio::placeholders::error));
}
Timer::Timer(
    DHTImpl &that,
    std::string name,
    TimerHandler handler,
    int seconds)
    :that_(that),
    name_(std::move(name)),
    handler_(handler),
    timer_(that_.io, boost::asio::chrono::seconds(seconds)),
    seconds_(seconds) {
}
}

