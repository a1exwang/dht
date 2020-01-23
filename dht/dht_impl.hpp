#pragma once
#include <array>

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>

#include <utils/log.hpp>
#include <utils/public_ip.hpp>

using boost::asio::ip::udp;

namespace boost::system {
class error_code;
}

namespace krpc {
class PingResponse;
class FindNodeResponse;
class GetPeersResponse;
class SampleInfohashesResponse;

class PingQuery;
class FindNodeQuery;
class GetPeersQuery;
class AnnouncePeerQuery;
class NodeInfo;
class NodeID;
}

namespace dht {
using namespace std::string_literals;

class DHT;
namespace get_peers {
class GetPeersManager;
}
class Transaction;

class DHTImpl {
 public:
  /**
   * Interface functions
   */

  explicit DHTImpl(DHT *dht);
  void bootstrap();
  void loop();

 private:
  friend class DHT;

  /**
   * DHT Message handlers
   */

  void handle_ping_response(const krpc::PingResponse &response);
  void handle_find_node_response(const krpc::FindNodeResponse &response);
  void handle_get_peers_response(
      const krpc::GetPeersResponse &response,
      const dht::Transaction &transaction);
  void handle_sample_infohashes_response(const krpc::SampleInfohashesResponse &response);

  void handle_ping_query(const krpc::PingQuery &query);
  void handle_find_node_query(const krpc::FindNodeQuery &query);
  void handle_get_peers_query(const krpc::GetPeersQuery &query);
  void handle_announce_peer_query(const krpc::AnnouncePeerQuery &query);

  void handle_receive_from(const boost::system::error_code &error, std::size_t bytes_transferred);

  /**
   * Helper functions
   */
  void continue_receive();
  std::function<void(const boost::system::error_code &, size_t)>
  default_handle_send();

  void find_self(const udp::endpoint &ep);
  void ping(const krpc::NodeInfo &target);

  [[nodiscard]]
  krpc::NodeID self() const;

  void send_find_node_response(
      const std::string &transaction_id,
      const krpc::NodeInfo &receiver,
      const std::vector<krpc::NodeInfo> &nodes);

  void handle_send(const boost::system::error_code &error, std::size_t bytes_transferred);
  void good_sender(const krpc::NodeID &sender_id);

  /**
   * Timer Handlers
   */
  void handle_report_stat_timer(const boost::system::error_code &e);
  void handle_expand_route_timer(const boost::system::error_code &e);
  void handle_refresh_nodes_timer(const boost::system::error_code &e);
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
}