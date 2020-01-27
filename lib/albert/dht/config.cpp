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

namespace {
namespace po = boost::program_options;

class Options {
 public:
  explicit Options(albert::dht::Config &config) :c(config) { }
  bool parse(int argc, char** argv) {
    std::string bootstrap_nodes;
    std::vector<std::string> config_fnames;

    po::options_description desc("General Options");
    desc.add_options()
        ("help,h", "Display help message")
        ("config", po::value(&config_fnames), "Config file where options may be specified (can be specified more than once)")
        ("debug", po::value(&c.debug), "Enable debug mode")
        ("bind-ip", po::value(&c.bind_ip)->default_value("0.0.0.0"), "DHT Client bind IP address")
        ("bind-port", po::value(&c.bind_port)->default_value(16667), "DHT Client bind port")
        ("id", po::value(&c.self_node_id)->default_value(""), "DHT Client Node ID, empty string stands for random value")
        ("bootstrap-nodes", po::value(&bootstrap_nodes)->default_value(
            "router.utorrent.com:6881,"
            "router.bittorrent.com:6881,"
            "dht.transmissionbt.com:6881"), "DHT Bootstrap node list")
        ;

    po::options_description hidden;
    hidden.add_options()
        ("info-hash-save-path", po::value(&c.info_hash_save_path)->default_value("info_hash.txt"), "Received infohashes save path")
        ("routing-table-save-path", po::value(&c.routing_table_save_path)->default_value("route.txt"), "Received infohashes save path")
        ("discovery-interval-seconds", po::value(&c.discovery_interval_seconds), "DHT discovery interval in seconds")
        ("report-interval-seconds", po::value(&c.report_interval_seconds), "DHT routing table report interval in seconds")
        ("refresh-nodes-check-interval", po::value(&c.refresh_nodes_check_interval_seconds), "")
        ("get-peers-refresh-interval", po::value(&c.get_peers_refresh_interval_seconds), "")
        ("get-peers-reqeust-expiration", po::value(&c.get_peers_request_expiration_seconds), "")
        ;

    po::options_description all_options;
    all_options.add(desc);
    all_options.add(hidden);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).
                  options(all_options).
                  run(),
              vm);

    po::positional_options_description p;

    if(vm.count("help")) {
      std::cout << make_usage_string_(basename_(argv[0]), desc, p) << std::endl;
      return false;
    }

    if(vm.count("config") > 0) {
      config_fnames = vm["config"].as<std::vector<std::string> >();

      for (size_t i = 0; i < config_fnames.size(); ++i) {
        std::ifstream ifs(config_fnames[i].c_str());

        if(ifs.fail())
        {
          std::cerr << "Error opening config file: " << config_fnames[i] << std::endl;
          return false;
        }

        po::store(po::parse_config_file(ifs, all_options), vm);
      }
    }

    po::notify(vm);

    std::vector<std::string> strs;
    boost::algorithm::split(strs, bootstrap_nodes, boost::is_any_of(","));
    if (!(strs.empty() || (strs.size() == 1 && strs[0].empty()))) {
      for (auto s : strs) {
        std::vector<std::string> tokens;
        boost::algorithm::split(tokens, s, boost::is_any_of(":"));
        if (tokens.size() != 2) {
          std::cerr << "Invalid bootstrap nodes format" << std::endl;
          return false;
        }
        c.bootstrap_nodes.emplace_back(tokens[0], tokens[1]);
      }
    }

    if (c.self_node_id.empty()) {
      std::random_device device;
      std::mt19937 rng{std::random_device()()};
      std::uniform_int_distribution<uint8_t> dist;
      std::stringstream ss_hex;
      for (int i = 0; i < 20; i++) {
        ss_hex << std::hex << std::setfill('0') << std::setw(2) << (int)dist(rng);
      }
      c.self_node_id = ss_hex.str();
    }

    return true;
  }

 private:
  static std::string basename_(const std::string& p)
  {
#ifdef HAVE_BOOST_FILESYSTEM
    return boost::filesystem::path(p).stem().string();
#else
    size_t start = p.find_last_of('/');
    if(start == std::string::npos)
      start = 0;
    else
      ++start;
    return p.substr(start);
#endif
  }

  // Boost doesn't offer any obvious way to construct a usage string
  // from an infinite list of positional parameters.  This hack
  // should work in most reasonable cases.
  static std::vector<std::string> get_unlimited_positional_args_(const po::positional_options_description& p) {
    assert(p.max_total_count() == std::numeric_limits<unsigned>::max());

    std::vector<std::string> parts;

    // reasonable upper limit for number of positional options:
    const int MAX = 1000;
    const std::string& last = p.name_for_position(MAX);

    for(size_t i = 0; true; ++i) {
      const std::string& cur = p.name_for_position(i);
      if(cur == last) {
        parts.push_back(cur);
        parts.push_back('[' + cur + ']');
        parts.push_back("...");
        return parts;
      }
      parts.push_back(cur);
    }
    return parts; // never get here
  }

  static std::string make_usage_string_(const std::string& program_name, const po::options_description& desc, po::positional_options_description& p) {
    std::vector<std::string> parts;
    parts.push_back("Usage: ");
    parts.push_back(program_name);
    size_t N = p.max_total_count();
    if(N == std::numeric_limits<unsigned>::max()) {
      std::vector<std::string> args = get_unlimited_positional_args_(p);
      parts.insert(parts.end(), args.begin(), args.end());
    }
    else {
      for(size_t i = 0; i < N; ++i) {
        parts.push_back(p.name_for_position(i));
      }
    }
    if(!desc.options().empty()) {
      parts.push_back("[options]");
    }
    std::ostringstream oss;
    std::copy(
        parts.begin(),
        parts.end(),
        std::ostream_iterator<std::string>(oss, " "));
    oss << '\n' << desc;
    return oss.str();
  }

 public:
  albert::dht::Config &c;
};
}

namespace albert::dht {

Config Config::from_command_line(int argc, char **argv) {
  Config config;
  Options(config).parse(argc, argv);
  return std::move(config);
}
void Config::serialize(std::ostream &os) const {
  os << "# DHT config" << std::endl;
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
  os << "debug = " << debug << std::endl;
  os << "# end of config." << std::endl;
}

}
