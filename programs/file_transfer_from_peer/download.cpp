#include <exception>
#include <set>
#include <fstream>

#include <boost/asio/io_service.hpp>

#include <albert/cui/cui.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>
#include <albert/io_latency/io_latency.hpp>
#include <albert/bt/peer.hpp>

#include "torrent.hpp"


int main(int argc, const char* argv[]) {

  /* Config parsing */
  std::string torrent_file = argv[argc - 1];
  auto args = albert::config::argv2args(argc - 1, argv);
  albert::dht::Config dht_config;
  albert::bt::Config bt_config;
  args = dht_config.from_command_line(args);
  args = bt_config.from_command_line(args);
  albert::config::throw_on_remaining_args(args);

  /* Initialization */
  bool debug = dht_config.debug;
  bool use_utp = bt_config.use_utp;
  albert::log::initialize_logger(dht_config.debug);
  boost::asio::io_service io_service{};
  albert::dht::DHTInterface dht(std::move(dht_config), io_service);
  albert::bt::BT bt(io_service, std::move(bt_config));

  /* Start service */
  dht.start();

  Task task(io_service, bt.self(), torrent_file, use_utp);

  boost::asio::steady_timer wait_timer(io_service, boost::asio::chrono::seconds(5));
  std::function<void(const boost::system::error_code &)> timer_handler = [&](const boost::system::error_code &e) {
    if (e) {
      LOG(info) << "timer error";
      exit(1);
    }

    dht.get_peers(task.info_hash(), [&](uint32_t ip, uint16_t port) {
      task.add_peer(ip, port);
    });

    task.stat();

    wait_timer.expires_at(wait_timer.expiry() + boost::asio::chrono::seconds(5));
    wait_timer.async_wait(timer_handler);
  };

  wait_timer.async_wait(timer_handler);


//  albert::io_latency::IOLatencyMeter meter(io_service, debug);
#ifdef NDEBUG
  try {
#endif
  io_service.run();
//  meter.loop();
#ifdef NDEBUG
  } catch (const std::exception &e) {
    LOG(error) << "io_service Failure: \"" << e.what() << '"';
    exit(1);
  }
#endif

  return 0;
}

