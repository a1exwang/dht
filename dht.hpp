#pragma once

#include <routing_table.hpp>
#include <krpc.hpp>
#include <transaction.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dht {

struct Config {
  std::string bind_ip;
  uint16_t bind_port = 0;

  std::string self_node_id;
  std::vector<std::pair<std::string, std::string>> bootstrap_nodes;

  int discovery_interval_seconds = 5;
  int report_interval_seconds = 3;
  int refresh_nodes_check_interval_seconds = 5;
};

class DHTImpl;

class DHT {
 public:
  static std::unique_ptr<DHT> make(const Config &config);
  explicit DHT(const Config &config);
  ~DHT();

  void loop();
  void bootstrap();

  std::string create_query(krpc::Query &query);
  std::string create_response(const krpc::Response &query);
  double get_current_time() const {
    return std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - bootstrap_time_).count();
  }

 private:

  static krpc::NodeID parse_node_id(const std::string &s);
  Config config_;

  // NOTE(aocheng) This must be place before routing_table
  krpc::NodeInfo self_info_;

  transaction::TransactionManager transaction_manager;
  dht::RoutingTable routing_table;

  /**
   * Stats
   */
  std::chrono::high_resolution_clock::time_point bootstrap_time_;
  size_t total_ping_query_received_{};
  size_t total_ping_query_sent_{};
  size_t total_ping_response_received_{};

  std::map<std::string, size_t> message_counters_;

  // This must be placed last
  // Hide network IO implementation details
  friend class DHTImpl;
  std::unique_ptr<DHTImpl> impl_;
};

}