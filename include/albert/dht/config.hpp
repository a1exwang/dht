#pragma once

#include <cstddef>
#include <cstdint>

#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include <albert/config/config.hpp>

namespace boost::program_options {
class variables_map;
}

namespace albert::dht {

struct Config :public albert::config::Config {
  Config();

  void serialize(std::ostream &os) const override;
  void after_parse(boost::program_options::variables_map &vm) override;

  std::string public_ip = "0.0.0.0";
  std::string bind_ip = "0.0.0.0";
  uint16_t bind_port = 16667;

  std::string self_node_id;
  std::vector<std::pair<std::string, std::string>> bootstrap_nodes;

  std::string info_hash_save_path = "info_hash.txt";
  std::string routing_table_save_path = "route.txt";

  int discovery_interval_seconds = 5;
  int report_interval_seconds = 5;
  int refresh_nodes_check_interval_seconds = 5;
  int get_peers_refresh_interval_seconds = 2;
  int get_peers_request_expiration_seconds = 30;
  int transaction_expiration_seconds = 60;

  bool throttler_enabled = false;
  int throttler_max_rps = 1000;
  double throttler_leak_probability = 0.1;
  int throttler_max_queue_size = 1000;
  size_t throttler_max_latency_ns = 1000ul*1000ul*1000ul;

  size_t max_routing_table_bucket_size = 8;
  size_t max_routing_table_known_nodes = 16384;
  bool delete_good_nodes = true;

  bool fake_id = false;
  size_t fake_id_prefix_length = 128;
  bool fat_routing_table = false;

  bool debug = false;
  std::string resolve_torrent_info_hash;
};


}
