#include "log.hpp"

#include <dht.hpp>
#include <krpc.hpp>

int main(int argc, char* argv[]) {
  dht::Config config;
  config.bind_ip = "0.0.0.0";
  config.bind_port = 16667;
  config.bootstrap_nodes = {
      {"router.utorrent.com", "6881"},
      {"router.bittorrent.com", "6881"},
      {"dht.transmissionbt.com", "6881"},
      {"dht.aelitis.com", "6881"},
  };

  auto dht = dht::DHT::make(config);
  dht->bootstrap();
  dht->loop();

  return 0;
}

