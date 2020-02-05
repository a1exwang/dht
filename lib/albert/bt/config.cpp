#include <albert/bt/config.hpp>

#include <string>
#include <vector>

#include <boost/program_options.hpp>


namespace albert::bt {
namespace po = boost::program_options;

bt::Config::Config() {
  po::options_description desc("BT Options");
  desc.add_options()
      ("bt-bind-ip", po::value(&bind_ip)->default_value("0.0.0.0"), "DHT Client bind IP address")
      ("bt-bind-port", po::value(&bind_port)->default_value(16667), "DHT Client bind port")
      ("bt-id", po::value(&id)->default_value(""), "BT Client ID, empty string stands for random value")
      ("bt-resolve-torrent-expiration-seconds", po::value(&resolve_torrent_expiration_seconds), "Resolve torrent expiration time in seconds")
      ;

  po::options_description hidden;
  hidden.add_options()
      ;

  all_options_ = std::make_unique<po::options_description>();
  all_options_->add(desc);
  all_options_->add(hidden);
}

void Config::serialize(std::ostream &os) const {
  os << "# bt::Config" << std::endl;
  os << "bt-bind-ip: " << bind_ip << std::endl;
  os << "bt-bind-port: " << bind_port << std::endl;
  os << "bt-id: " << id << std::endl;
  os << "bt-resolve-torrent-expiration-seconds" << resolve_torrent_expiration_seconds << std::endl;
  os << "# end of bt::Config";
}

}