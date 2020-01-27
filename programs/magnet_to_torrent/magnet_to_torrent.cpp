#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/cui/cui.hpp>

#include <exception>
#include <sstream>

#include <boost/asio.hpp>


int main(int argc, char* argv[]) {

  auto config = albert::dht::Config::from_command_line(argc, argv);
  albert::log::initialize_logger(config.debug);

  std::stringstream ss;
  config.serialize(ss);
  LOG(info) << ss.str();

  boost::asio::io_service io_service{};

  albert::dht::DHTInterface dht(std::move(config), io_service);
  dht.start();

  auto bt_id = albert::krpc::NodeID::random();
  LOG(info) << "BT peer ID " << bt_id.to_string();
  albert::bt::BT bt(io_service, bt_id);
  bt.start();

  auto info_hash = config.resolve_torrent_info_hash;
  albert::cui::CommandLineUI cui(info_hash, io_service, dht, bt);
  cui.start();

  try {
    io_service.run();
  } catch (const std::exception &e) {
    LOG(error) << "io_service Failure: \"" << e.what() << '"';
    exit(1);
  }

  (void)bt;
  (void)cui;
  (void)dht;

  return 0;
}

