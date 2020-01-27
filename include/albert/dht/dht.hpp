#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <albert/krpc/krpc.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/routing_table.hpp>
#include <albert/dht/transaction.hpp>
#include <albert/bt/peer_connection.hpp>


namespace albert::dht {
namespace get_peers {
  class GetPeersManager;
};

class DHTImpl;
class Config;

class DHT {
 public:
  static std::unique_ptr<DHT> make(Config config);
  explicit DHT(Config config);
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
  std::unique_ptr<dht::RoutingTable> routing_table;
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