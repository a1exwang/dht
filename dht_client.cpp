#include <dht/dht.hpp>
#include <krpc/krpc.hpp>
#include <utils/log.hpp>

#include <sstream>

int main(int argc, char* argv[]) {
  bool debug = false;
  if (argc == 2) {
    debug = argv[1] == std::string("--debug");
  }
  dht::log::initialize_logger(debug);

  dht::Config config;
  config.bind_ip = "0.0.0.0";
  config.bind_port = 16667;
  config.bootstrap_nodes = {
      {"router.utorrent.com", "6881"},
      {"router.bittorrent.com", "6881"},
      {"dht.transmissionbt.com", "6881"},
      {"dht.aelitis.com", "6881"},
  };
  std::stringstream ss;
  krpc::NodeID::from_hex("c8db9c5b37c71d0f3b28788b94b8efa5d2d92731").encode(ss);
  config.self_node_id = ss.str();

  auto dht = dht::DHT::make(config);
  dht->bootstrap();
  dht->loop();

  return 0;
}

