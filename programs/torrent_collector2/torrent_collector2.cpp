#include <exception>
#include <set>

#include <albert/cui/cui.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/store/sqlite3_store.hpp>
#include <albert/u160/u160.hpp>
#include <albert/io_latency/io_latency.hpp>


#include <boost/asio/io_service.hpp>

int main(int argc, const char* argv[]) {

  /* Config parsing */
  auto args = albert::config::argv2args(argc, argv);
  albert::dht::Config dht_config;
  args = dht_config.from_command_line(args);
  albert::config::throw_on_remaining_args(args);

  /* Initialization */
  bool debug = dht_config.debug;
  albert::log::initialize_logger(dht_config.debug);
  boost::asio::io_service io_service{};
  albert::dht::DHTInterface dht(std::move(dht_config), io_service);
  albert::store::Sqlite3Store store("torrents/torrents.sqlite3");

  /* Start service */
  dht.start();
  dht.set_announce_peer_handler([&dht, &store](const albert::u160::U160 &ih) {
    auto ih_hex = ih.to_string();
    auto item = store.read(ih_hex);
    if (!item.has_value()) {
      LOG(info) << "got info_hash " << ih.to_string() << ", saved to db";
      store.create(ih_hex, "");
    }
  });

  albert::io_latency::IOLatencyMeter meter(io_service, debug);
#ifdef NDEBUG
  try {
#endif
//  io_service.run();
  meter.loop();
#ifdef NDEBUG
  } catch (const std::exception &e) {
    LOG(error) << "io_service Failure: \"" << e.what() << '"';
    exit(1);
  }
#endif

  return 0;
}

