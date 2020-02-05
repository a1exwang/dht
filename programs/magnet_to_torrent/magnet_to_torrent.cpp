#include <albert/bt/bt.hpp>
#include <albert/cui/cui.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>

#include <exception>
#include <sstream>

#include <boost/asio/io_service.hpp>


int main(int argc, char* argv[]) {
  // usage magnet_to_torrent --conf=dht.conf {bt_info_hash}
  std::string info_hash;
  if (argc >= 1) {
    auto last_arg = argv[argc - 1];
    if (strlen(last_arg) == albert::u160::U160Length*2) {
      info_hash = argv[argc-1];
      LOG(info) << "Using info_hash from command line: " << info_hash;
    }
  }

  albert::dht::Config config;
  albert::bt::Config bt_config;

  // skip last arg
  auto args = albert::config::argv2args(argc - 1, argv);
  args = config.from_command_line(args);
  args = bt_config.from_command_line(args);
  albert::config::throw_on_remaining_args(args);
  albert::log::initialize_logger(config.debug);
  if (info_hash.empty()) {
    info_hash = config.resolve_torrent_info_hash;
  }

  boost::asio::io_service io_service{};

  albert::dht::DHTInterface dht(std::move(config), io_service);
  dht.start();

  albert::bt::BT bt(io_service, std::move(bt_config));
  bt.start();

  albert::cui::CommandLineUI cui(info_hash, io_service, dht, bt);
  cui.start();

#ifdef NDEBUG
    try {
#endif
    io_service.run();
#ifdef NDEBUG
  } catch (const std::exception &e) {
    LOG(error) << "io_service Failure: \"" << e.what() << '"';
    exit(1);
  }
#endif

  return 0;
}

