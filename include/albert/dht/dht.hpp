#pragma once
#include <cstddef>
#include <cstdint>

#include <chrono>
#include <fstream>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <albert/krpc/krpc.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/transaction.hpp>


namespace boost::asio {
class io_context;
typedef io_context io_service;
}


namespace albert::dht {
namespace routing_table {
class RoutingTable;
}
namespace get_peers {
  class GetPeersManager;
}
namespace sample_infohashes {
class SampleInfohashesManager;
}

class DHTImpl;
class Config;

class DHT {
 public:
  static std::unique_ptr<DHT> make(Config config);
  explicit DHT(Config config);
  ~DHT();

  // routing_table: The routing table the query belongs to. If routing_table is nullptr, it belongs all routing tables.
  std::string create_query(std::shared_ptr<krpc::Query> query, routing_table::RoutingTable *routing_table);
  std::string create_response(const krpc::Response &query);
  double get_current_time() const {
    return std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - bootstrap_time_).count();
  }

  void add_routing_table(std::unique_ptr<routing_table::RoutingTable> routing_table);

  bool in_black_list(uint32_t ip, uint16_t port) const;
  void add_to_black_list(uint32_t ip, uint16_t port);
 private:
  static krpc::NodeID parse_node_id(const std::string &s);
  Config config_;

  // NOTE(aocheng) This must be place before routing_table
  krpc::NodeInfo self_info_;

  dht::TransactionManager transaction_manager;
  std::list<std::unique_ptr<dht::routing_table::RoutingTable>> routing_tables_;
  dht::routing_table::RoutingTable *main_routing_table_;

  /**
   * Function managers
   */
  std::unique_ptr<get_peers::GetPeersManager> get_peers_manager_;

  /**
   * Stats
   */
  std::chrono::high_resolution_clock::time_point bootstrap_time_;
  size_t total_ping_query_received_{};
  size_t total_ping_query_sent_{};
  size_t total_ping_response_received_{};

  std::map<std::string, size_t> message_counters_;
  std::ofstream info_hash_list_stream_;

  std::set<std::tuple<uint32_t, uint16_t>> black_list_;

  // This must be placed last
  // Hide network IO implementation details
  friend class DHTImpl;
//  std::unique_ptr<DHTImpl> impl_;
};

class DHTInterface {
 public:
  DHTInterface(Config config, boost::asio::io_service &io_service);
  ~DHTInterface();
  void start();
  void get_peers(const krpc::NodeID &info_hash, const std::function<void(uint32_t, uint16_t)> &callback);
  void sample_infohashes(const std::function<void(const krpc::NodeID &info_hash)> handler);
  void set_announce_peer_handler(std::function<void (const krpc::NodeID &info_hash)> handler);
 private:
  std::unique_ptr<DHT> dht_;
  std::unique_ptr<DHTImpl> impl_;
};

}