#include "dht_impl.hpp"

#include <string>

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/dht/sample_infohashes/sample_infohashes_manager.hpp>
#include <albert/log/log.hpp>
#include <albert/public_ip/public_ip.hpp>
#include <albert/utils/utils.hpp>
#include <albert/u160/u160.hpp>
#include <albert/io_latency/function_latency.hpp>

#include "get_peers.hpp"

using boost::asio::ip::udp;

namespace albert::dht {

using namespace std::string_literals;

DHTImpl::DHTImpl(DHT *dht, boost::asio::io_service &io)
    : dht_(dht),
      sample_infohashes_manager_(nullptr),
      io(io),
      socket(io,
             udp::endpoint(
                 boost::asio::ip::address_v4::from_string(dht->config_.bind_ip),
                 dht->config_.bind_port)),
      signals_(io, SIGINT)
       {

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

  if (dht_->in_black_list(sender_endpoint.address().to_v4().to_uint(), sender_endpoint.port())) {
    continue_receive();
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
    LOG(debug) << "Invalid bencoding, e: '" << e.what() << "', ignored " << std::endl
               << albert::dht::utils::hexdump(receive_buffer.data(), bytes_transferred, true);
    if (bad_sender()) {
      LOG(debug) << "banned " << sender_endpoint << " due to invalid bencoding";
    }
    continue_receive();
    return;
  }
  routing_table::RoutingTable *routing_table = nullptr;
  std::string query_method_name{};
  try {
    message = krpc::Message::decode(*node, [this, &query_method_name, &query_node, &node, &routing_table](std::string id) -> std::string {
      if (dht_->transaction_manager.has_transaction(id)) {
        this->dht_->transaction_manager.end(id, [&query_method_name, &query_node, &routing_table](const dht::Transaction &transaction) {
          query_method_name = transaction.method_name_;
          query_node = transaction.query_node_;
          routing_table = transaction.routing_table_;
        });
      } else {
        std::stringstream ss;
        node->encode(ss, bencoding::EncodeMode::JSON);
        LOG(debug) << "Invalid message, transaction not found, transaction_id: '"
                   << dht::utils::hexdump(id.data(), id.size(), false) << "', bencoding: " << ss.str();
      }
      return query_method_name;
    });
  } catch (const krpc::InvalidMessage &e) {
    std::stringstream ss;
    node->encode(ss, bencoding::EncodeMode::JSON);
    LOG(debug) << "InvalidMessage, e: '" << e.what() << "', ignored, bencoding '" << ss.str() << "'";
    if (bad_sender()) {
      LOG(debug) << "banned " << sender_endpoint << " due to invalid message";
    }
    continue_receive();
    return;
  }

  if (auto response = std::dynamic_pointer_cast<krpc::Response>(message); response) {
    if (auto find_node_response = std::dynamic_pointer_cast<krpc::FindNodeResponse>(response); find_node_response) {
      handle_find_node_response(*find_node_response, routing_table);
    } else if (auto ping_response = std::dynamic_pointer_cast<krpc::PingResponse>(response); ping_response) {
      handle_ping_response(*ping_response);
    } else if (auto get_peers_response = std::dynamic_pointer_cast<krpc::GetPeersResponse>(response); get_peers_response) {
      if (auto q = std::dynamic_pointer_cast<krpc::GetPeersQuery>(query_node); q) {
        handle_get_peers_response(*get_peers_response, *q);
      } else {
        LOG(error) << "Invalid get_peers response, Query type not get_peers";
        if (bad_sender()) {
          LOG(info) << "banned " << sender_endpoint << " due to invalid get_peers response";
        }
      }
    } else if (auto sample_infohashes_res = std::dynamic_pointer_cast<krpc::SampleInfohashesResponse>(response); sample_infohashes_res) {
      handle_sample_infohashes_response(*sample_infohashes_res);
    } else {
      LOG(error) << "Warning! response type not supported";
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
    }
  } else if (auto dht_error = std::dynamic_pointer_cast<krpc::Error>(message); dht_error) {
    LOG(error) << "DHT Error message from " << sender_endpoint << ", '" << dht_error->message() << "' method: " << query_method_name;
    if (bad_sender()) {
      LOG(info) << "banned " << sender_endpoint << " due to error message";
    }
  } else {
    LOG(error) << "Unknown message type";
    if (bad_sender()) {
      LOG(info) << "banned " << sender_endpoint << " due to unknown message type";
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

  // start all timers
  for (auto &timer : timers_) {
    timer.fire_immediately();
  }

  // Bootstrapping routing table by finding self
  bootstrap_routing_table(*dht_->main_routing_table_);
}

void DHTImpl::sample_infohashes(std::function<void(const u160::U160 &info_hash)> handler) {
  if (sample_infohashes_manager_) {
    throw std::invalid_argument("Cannot sample_infohashes already in progress");
  } else {
    sample_infohashes_manager_ = std::make_unique<sample_infohashes::SampleInfohashesManager>(io, *dht_, *this, std::move(handler));
  }
}

/**
 * class albert::dht::DHT
 */

DHT::DHT(Config config)
    : config_(std::move(config)),
      self_info_(u160::U160::from_hex(config_.self_node_id), albert::public_ip::my_v4(), config_.bind_port),
      transaction_manager(std::chrono::seconds(config.transaction_expiration_seconds)),
      get_peers_manager_(std::make_unique<dht::get_peers::GetPeersManager>(config_.get_peers_request_expiration_seconds)),
      main_routing_table_(nullptr),
      info_hash_list_stream_(config_.info_hash_save_path, std::fstream::app) {

  std::ifstream ifs(config_.routing_table_save_path);
  std::unique_ptr<routing_table::RoutingTable> rt;
  if (ifs) {
    LOG(info) << "Loading routing table from '" << config_.routing_table_save_path << "'";
    try {
      rt = routing_table::RoutingTable::deserialize(
          ifs, "main",
          config_.routing_table_save_path,
          config_.max_routing_table_bucket_size,
          config_.max_routing_table_known_nodes,
          config_.delete_good_nodes,
          config_.fat_routing_table,
          boost::bind(&DHT::add_to_black_list, this, _1, _2));
      LOG(info) << "Routing table size " << rt->known_node_count();
    } catch (const std::exception &e) {
      LOG(info) << "Failed to load routing table, '" << e.what() << "', Creating empty routing table";
    }
  } else {
    LOG(info) << "Creating empty routing table";
  }
  if (!rt) {
    rt = std::make_unique<routing_table::RoutingTable>(
        u160::U160::from_hex(config_.self_node_id),
        "main",
        config_.routing_table_save_path,
        config_.max_routing_table_bucket_size,
        config_.max_routing_table_known_nodes,
        config_.delete_good_nodes,
        config_.fat_routing_table,
        boost::bind(&DHT::add_to_black_list, this, _1, _2));
  }
  main_routing_table_ = rt.get();
  routing_tables_.push_back(std::move(rt));

  if (!info_hash_list_stream_.is_open()) {
    throw std::runtime_error("Failed to open info hash list file '" + config_.info_hash_save_path + "'");
  }
}

DHT::~DHT() {}

DHTInterface::DHTInterface(Config config, boost::asio::io_service &io_service)
    :dht_(std::make_unique<DHT>(std::move(config))), impl_() {
  impl_ = std::make_unique<DHTImpl>(dht_.get(), io_service);
}

DHTInterface::~DHTInterface() {}

void DHTInterface::start() {
  impl_->bootstrap();
}
void DHTInterface::get_peers(const u160::U160 &info_hash, const std::function<void(uint32_t, uint16_t)> &callback) {
  impl_->get_peers(info_hash, callback);
}
void DHTInterface::sample_infohashes(const std::function<void (const u160::U160 &)> handler) {
  impl_->sample_infohashes(std::move(handler));
}
void DHTInterface::set_announce_peer_handler(std::function<void(const u160::U160 &info_hash)> handler) {
  impl_->set_announce_peer_handler(std::move(handler));
}

void Timer::handler_timer(const boost::system::error_code &error) {
  if (error) {
    throw std::runtime_error("DHTImpl::Timer(" + name_ + ") error: " + error.message());
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

