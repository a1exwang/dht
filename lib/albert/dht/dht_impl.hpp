#pragma once
#include <array>
#include <functional>
#include <type_traits>

#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <albert/log/log.hpp>
#include <albert/public_ip/public_ip.hpp>
#include <utility>

using boost::asio::ip::udp;

namespace boost::system {
class error_code;
}

namespace albert::krpc {
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

namespace albert::dht {
using namespace std::string_literals;

class DHT;
namespace get_peers {
class GetPeersManager;
}
class Transaction;

class DHTImpl;
class Timer {
 public:
  typedef std::function<void()> Cancel;
  typedef void (DHTImpl::*TimerHandler)(const Cancel &);
  Timer(DHTImpl &that, std::string name, TimerHandler handler, int seconds);
  void fire();
  void fire_immediately();
  void handler_timer(const boost::system::error_code &error);;
 private:
  DHTImpl &that_;
  std::string name_;
  TimerHandler handler_;
  boost::asio::steady_timer timer_;
  int seconds_;
};

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
  friend class Timer;

  /**
   * DHT Message handlers
   */

  void handle_ping_response(const krpc::PingResponse &response);
  void handle_find_node_response(const krpc::FindNodeResponse &response);
  void handle_get_peers_response(
      const krpc::GetPeersResponse &response,
      const krpc::GetPeersQuery &query);
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

  void handle_read_input(
      const boost::system::error_code &error,
      std::size_t bytes_transferred);

  void find_self(const udp::endpoint &ep);
  void ping(const krpc::NodeInfo &target);

  [[nodiscard]]
  krpc::NodeID self() const;

  void send_find_node_response(
      const std::string &transaction_id,
      const krpc::NodeInfo &receiver,
      const std::vector<krpc::NodeInfo> &nodes);
  void send_get_peers_query(
      const krpc::NodeID &info_hash,
      const krpc::NodeInfo &receiver
      );

  void handle_send(const boost::system::error_code &error, std::size_t bytes_transferred);
  void good_sender(const krpc::NodeID &sender_id);

  /**
   * Timer Handlers
   *
   * How to Write a new timer:
   * 1. Add a timer handler member function who has the signature `void handler(const Timer::Cancel&)`;
   * 2. Add `timers_.emplace_back(*this, "timer name", &DHTImpl::handler, interval);` in DHTImpl's constructor.
   */
  void handle_report_stat_timer(const Timer::Cancel &cancel);
  void handle_expand_route_timer(const Timer::Cancel &cancel);
  void handle_refresh_nodes_timer(const Timer::Cancel &cancel);
  void handle_get_peers_timer(const Timer::Cancel &cancel);

  void dht_get_peers(const krpc::NodeID &info_hash);
 private:
  DHT *dht_;

  boost::asio::io_service io{};

  std::array<char, 65536> receive_buffer{};
  udp::socket socket;
  udp::endpoint sender_endpoint{};
  boost::asio::signal_set signals_;

  std::vector<Timer> timers_;

  boost::asio::posix::stream_descriptor input_;
  boost::asio::streambuf input_buffer_;
  std::stringstream input_ss_;
};
}