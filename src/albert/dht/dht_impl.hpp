#pragma once
#include <array>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
#include <utility>

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/signal_set.hpp>

#include <albert/dht/dht.hpp>
#include <albert/flow_control/rps_throttler.hpp>
#include <albert/log/log.hpp>
#include <albert/public_ip/public_ip.hpp>

using boost::asio::ip::udp;

namespace boost::system {
class error_code;
}

namespace albert::u160 {
class U160;
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
}

namespace albert::dht {

class DHT;
class Transaction;
class DHTImpl;

namespace get_peers {
class GetPeersManager;
}
namespace sample_infohashes {
class SampleInfohashesManager;
}

using namespace std::string_literals;
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

  explicit DHTImpl(DHT *dht, boost::asio::io_service &io);
  void bootstrap();
  void get_peers(const u160::U160 &info_hash, const std::function<void(uint32_t, uint16_t)> &callback);
  void sample_infohashes(std::function<void(const u160::U160 &info_hash)> handler);

  /* For SampleInfohashesManager */
  void bootstrap_routing_table(routing_table::RoutingTable &routing_table);
  void send_sample_infohashes_query(
      const u160::U160 &target,
      const krpc::NodeInfo &receiver
  );

  void set_announce_peer_handler(std::function<void (const u160::U160 &info_hash)> handler);

 private:
  friend class DHT;
  friend class Timer;

  /**
   * DHT Message handlers
   */

  void handle_ping_response(const krpc::PingResponse &response);
  void handle_find_node_response(const krpc::FindNodeResponse &response, routing_table::RoutingTable *routing_table);
  void handle_get_peers_response(
      const krpc::GetPeersResponse &response,
      const krpc::GetPeersQuery &query);
  void handle_sample_infohashes_response(const krpc::SampleInfohashesResponse &response);

  void handle_ping_query(const krpc::PingQuery &query);
  void handle_find_node_query(const krpc::FindNodeQuery &query);
  void handle_get_peers_query(const krpc::GetPeersQuery &query);
  void handle_announce_peer_query(const krpc::AnnouncePeerQuery &query);

  void handle_receive_from(const boost::system::error_code &error, std::size_t bytes_transferred);

  bool try_to_handle_unknown_message(std::shared_ptr<bencoding::Node> node);

  /**
   * Helper functions
   */
  void continue_receive();
  std::function<void(const boost::system::error_code &, size_t)>
  default_handle_send(const std::string &description);

  void find_self(routing_table::RoutingTable &rt, const udp::endpoint &ep);
  void find_node(routing_table::RoutingTable &rt, const udp::endpoint &ep, u160::U160 target);
  void ping(const krpc::NodeInfo &target);

  [[nodiscard]]
  u160::U160 self() const;

  [[nodiscard]]
  u160::U160 maybe_fake_self(const u160::U160 &target) const;

  void send_find_node_response(
      const std::string &transaction_id,
      const krpc::NodeInfo &receiver,
      const std::vector<krpc::NodeInfo> &nodes);
  void send_get_peers_query(
      const u160::U160 &info_hash,
      const krpc::NodeInfo &receiver
      );

  void handle_send(const std::string &description, const boost::system::error_code &error, std::size_t bytes_transferred);
  void good_sender(const u160::U160 &sender_id, const std::string &version);

  void bad_node(const u160::U160 &id);
  // return value: If the sender is added to black list, it returns true; otherwise, it returns false.
  bool bad_sender();

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

 private:
  DHT *dht_;
  std::unique_ptr<sample_infohashes::SampleInfohashesManager> sample_infohashes_manager_;

  boost::asio::io_service &io;

  std::array<char, 65536> receive_buffer{};
  udp::socket socket;
  udp::endpoint sender_endpoint{};
  boost::asio::signal_set signals_;

  std::vector<Timer> timers_;

  std::function<void (const u160::U160 &info_hash)> announce_peer_handler_;
  flow_control::RPSThrottler throttler_;
};
}
