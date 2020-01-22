#include "dht_impl.hpp"

#include <string>

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <dht/dht.hpp>
#include <utils/log.hpp>
#include <utils/public_ip.hpp>

using boost::asio::ip::udp;

namespace dht {

using namespace std::string_literals;

DHTImpl::DHTImpl(DHT *dht)
    : dht_(dht),
      io(),
      socket(io,
             udp::endpoint(
                 boost::asio::ip::address_v4::from_string(dht->config_.bind_ip),
                 dht->config_.bind_port)),
      expand_route_timer(io, boost::asio::chrono::seconds(dht->config_.discovery_interval_seconds)),
      report_stat_timer(io, boost::asio::chrono::seconds(dht->config_.report_interval_seconds)),
      refresh_nodes_timer(io, boost::asio::chrono::seconds(dht->config_.refresh_nodes_check_interval_seconds)) {}


void DHTImpl::bootstrap() {
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
      LOG(error) << "failed to resolve '" << node_host << ":" << node_port << ", skipping, reason: " << e.what();
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

void DHTImpl::loop() {
  try {
    io.run();
    LOG(info) << "Successfully end";
  } catch (std::exception &e) {
    LOG(error) << "Exception: " << e.what();
    std::exit(EXIT_FAILURE);
  }
}


std::unique_ptr<DHT> DHT::make(const Config &config) {
  return std::make_unique<DHT>(config);
}
void DHT::loop() { impl_->loop(); }
void DHT::bootstrap() { impl_->bootstrap(); }
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
DHT::~DHT() {}

}

