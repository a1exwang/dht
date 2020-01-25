#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <krpc/krpc.hpp>
#include <dht/routing_table.hpp>
#include <dht/transaction.hpp>
#include <bt/peer_connection.hpp>


namespace dht {
namespace get_peers {
  class GetPeersManager;
};

struct Config {
  std::string bind_ip;
  uint16_t bind_port = 0;

  std::string self_node_id;
  std::vector<std::pair<std::string, std::string>> bootstrap_nodes;

  std::string info_hash_save_path = "info_hash.txt";

  int discovery_interval_seconds = 5;
  int report_interval_seconds = 10;
  int refresh_nodes_check_interval_seconds = 5;
  int get_peers_refresh_interval_seconds = 2;
  int get_peers_request_expiration_seconds = 30;
};

class DHTImpl;

class DHT {
 public:
  static std::unique_ptr<DHT> make(const Config &config);
  explicit DHT(const Config &config);
  ~DHT();

  void loop();
  void bootstrap();

  std::string create_query(std::shared_ptr<krpc::Query> query);
  std::string create_response(const krpc::Response &query);
  double get_current_time() const {
    return std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - bootstrap_time_).count();
  }

  void got_info_hash(const krpc::NodeID &info_hash);

 private:
  static krpc::NodeID parse_node_id(const std::string &s);
  Config config_;

  // NOTE(aocheng) This must be place before routing_table
  krpc::NodeInfo self_info_;

  dht::TransactionManager transaction_manager;
  dht::RoutingTable routing_table;
  std::unique_ptr<get_peers::GetPeersManager> get_peers_manager_;

  std::list<bt::peer::PeerConnection> peer_connections_;

  /**
   * Stats
   */
  std::chrono::high_resolution_clock::time_point bootstrap_time_;
  size_t total_ping_query_received_{};
  size_t total_ping_query_sent_{};
  size_t total_ping_response_received_{};

  std::map<std::string, size_t> message_counters_;
  std::ofstream info_hash_list_stream_;

  // This must be placed last
  // Hide network IO implementation details
  friend class DHTImpl;
  std::unique_ptr<DHTImpl> impl_;
};

}