#pragma once

#include <cstddef>
#include <cstdint>

#include <memory>
#include <string>
#include <vector>
#include <iostream>


namespace albert::dht {

struct Config {
  static Config from_command_line(int argc, char **argv);
  void serialize(std::ostream &os) const;

  std::string bind_ip = "0.0.0.0";
  uint16_t bind_port = 16667;

  std::string self_node_id;
  std::vector<std::pair<std::string, std::string>> bootstrap_nodes;

  std::string info_hash_save_path = "info_hash.txt";
  std::string routing_table_save_path = "route.txt";

  int discovery_interval_seconds = 5;
  int report_interval_seconds = 10;
  int refresh_nodes_check_interval_seconds = 5;
  int get_peers_refresh_interval_seconds = 2;
  int get_peers_request_expiration_seconds = 30;

  bool debug = false;
};


}
