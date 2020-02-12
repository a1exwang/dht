#include <albert/dht/config.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>

#include <boost/program_options.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <albert/log/log.hpp>
#include <albert/config/config.hpp>

namespace po = boost::program_options;

namespace albert::dht {

void Config::serialize(std::ostream &os) const {
  os << "# DHT config" << std::endl;
  os << "public_ip = " << public_ip << std::endl;
  os << "bind_ip = " << bind_ip << std::endl;
  os << "bind_port = " << bind_port << std::endl;
  os << "self_node_id = " << self_node_id << std::endl;
  os << "bootstrap_nodes = ";
  bool first = true;
  for (const auto &item : bootstrap_nodes) {
    if (first) {
      first = false;
    } else {
      os << ",";
    }
    os << item.first << ":" << item.second;
  }
  os << std::endl;

  os << "info_hash_save_path = " << info_hash_save_path << std::endl;
  os << "routing_table_save_path = " << routing_table_save_path << std::endl;

  os << "discovery_interval_seconds = " << discovery_interval_seconds << std::endl;
  os << "report_interval_seconds = " << report_interval_seconds << std::endl;
  os << "refresh_nodes_check_interval_seconds = " << refresh_nodes_check_interval_seconds << std::endl;
  os << "get_peers_refresh_interval_seconds = " << get_peers_refresh_interval_seconds << std::endl;
  os << "get_peers_request_expiration_seconds = " << get_peers_request_expiration_seconds << std::endl;
  os << "throttler_enabled " << throttler_enabled << std::endl;
  os << "throttler_max_rps " << throttler_max_rps << std::endl;
  os << "throttler_max_queue_size " << throttler_max_queue_size << std::endl;
  os << "throttler_max_latency_ns " << throttler_max_latency_ns << std::endl;
  os << "debug = " << debug << std::endl;
  os << "resolve_torrent_info_hash = " << resolve_torrent_info_hash << std::endl;
  os << "max_routing_table_bucket_size = " << max_routing_table_bucket_size << std::endl;
  os << "max_routing_table_known_nodes = " << max_routing_table_known_nodes << std::endl;
  os << "delete_good_nodes = " << delete_good_nodes << std::endl;
  os << "fake_id = " << fake_id << std::endl;
  os << "fake_id_prefix_length = " << fake_id_prefix_length << std::endl;
  os << "fat_routing_table = " << fat_routing_table << std::endl;
  os << "transaction_expiration_seconds = " << transaction_expiration_seconds << std::endl;
  os << "# end of config." << std::endl;
}

Config::Config() {
  po::options_description desc("General Options");
  desc.add_options()
      ("debug", po::value(&debug), "Enable debug mode")
      ("public-ip", po::value(&public_ip), "My public IP address. Set this to skip public IP resolution")
      ("bind-ip", po::value(&bind_ip)->default_value("0.0.0.0"), "DHT Client bind IP address")
      ("bind-port", po::value(&bind_port)->default_value(16667), "DHT Client bind port")
      ("id", po::value(&self_node_id)->default_value(""), "DHT Client Node ID, empty string stands for random value")
      ("bootstrap-nodes", po::value<std::string>()->default_value(
          "router.utorrent.com:6881,"
          "router.bittorrent.com:6881,"
          "dht.transmissionbt.com:6881"), "DHT Bootstrap node list")
      ;

  po::options_description hidden;
  hidden.add_options()
      ("info-hash-save-path", po::value(&info_hash_save_path)->default_value("info_hash.txt"), "Received infohashes save path")
      ("routing-table-save-path", po::value(&routing_table_save_path)->default_value("route.txt"), "Received infohashes save path")
      ("discovery-interval-seconds", po::value(&discovery_interval_seconds), "DHT discovery interval in seconds")
      ("report-interval-seconds", po::value(&report_interval_seconds), "DHT routing table report interval in seconds")
      ("refresh-nodes-check-interval", po::value(&refresh_nodes_check_interval_seconds), "")
      ("get-peers-refresh-interval", po::value(&get_peers_refresh_interval_seconds), "")
      ("get-peers-request-expiration", po::value(&get_peers_request_expiration_seconds), "")
      ("throttler-enabled", po::value(&throttler_enabled))
      ("throttler-max-rps", po::value(&throttler_max_rps))
      ("throttler-max-queue-size", po::value(&throttler_max_queue_size))
      ("throttler-max-latency-ns", po::value(&throttler_max_latency_ns))
      ("resolve-torrent-info-hash", po::value(&resolve_torrent_info_hash), "")
      ("max-routing-table-bucket-size", po::value(&max_routing_table_bucket_size), "")
      ("max-routing-table-known-nodes", po::value(&max_routing_table_known_nodes), "")
      ("delete-good-nodes", po::value(&delete_good_nodes), "")
      ("fake-id", po::value(&fake_id), "")
      ("fake-id-prefix-length", po::value(&fake_id_prefix_length), "")
      ("fat-routing-table", po::value(&fat_routing_table), "")
      ("transaction-expiration-seconds", po::value(&transaction_expiration_seconds), "")
      ;

  all_options_ = std::make_unique<po::options_description>();
  all_options_->add(desc);
  all_options_->add(hidden);
}

void Config::after_parse(boost::program_options::variables_map &vm) {
  if(vm.count("bootstrap-nodes") > 0) {
    auto bootstrap_nodes_string = vm["bootstrap-nodes"].as<std::string>();
    std::vector<std::string> strs;
    boost::algorithm::split(strs, bootstrap_nodes_string, boost::is_any_of(","));
    if (!(strs.empty() || (strs.size() == 1 && strs[0].empty()))) {
      for (auto s : strs) {
        std::vector<std::string> tokens;
        boost::algorithm::split(tokens, s, boost::is_any_of(":"));
        if (tokens.size() != 2) {
          throw std::invalid_argument("Invalid bootstrap nodes format '" + s + "'");
        }
        bootstrap_nodes.emplace_back(tokens[0], tokens[1]);
      }
    }
  }

  if (self_node_id.empty()) {
    std::random_device device;
    std::mt19937 rng{std::random_device()()};
    std::uniform_int_distribution<uint8_t> dist;
    std::stringstream ss_hex;
    for (int i = 0; i < 20; i++) {
      ss_hex << std::hex << std::setfill('0') << std::setw(2) << (int)dist(rng);
    }
    self_node_id = ss_hex.str();
  }

}

}
