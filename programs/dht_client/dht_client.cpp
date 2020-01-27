#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>


int main(int argc, char* argv[]) {

  auto config = albert::dht::Config::from_command_line(argc, argv);
  albert::log::initialize_logger(config.debug);

  std::stringstream ss;
  config.serialize(ss);
  LOG(info) << ss.str();

  auto dht = albert::dht::DHT::make(config);
  dht->bootstrap();
  dht->loop();

  return 0;
}

