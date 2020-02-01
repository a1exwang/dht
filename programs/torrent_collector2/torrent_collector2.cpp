#include <exception>
#include <sstream>

#include <albert/bt/bt.hpp>
#include <albert/cui/cui.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>

#include <boost/asio/io_service.hpp>

int main(int argc, char* argv[]) {
  // usage magnet_to_torrent --conf=dht.conf {bt_info_hash}
  albert::dht::Config config;
  try {
    config = albert::dht::Config::from_command_line(argc, argv);
  } catch (const std::exception &e) {
    LOG(error) << "Failed to parse command line: " << e.what();
    exit(1);
  }
  albert::log::initialize_logger(config.debug);

  std::stringstream ss;
  config.serialize(ss);
  LOG(info) << ss.str();

  boost::asio::io_service io_service{};

  albert::dht::DHTInterface dht(std::move(config), io_service);
  dht.start();


  std::ofstream ofs("ih.txt");
  dht.set_announce_peer_handler([&ofs](const albert::krpc::NodeID &ih) {
    LOG(info) << "announce_peer " << ih.to_string();
    ofs << ih.to_string() << std::endl;
    ofs.flush();
  });

//  auto bt_id = albert::krpc::NodeID::random();
//  LOG(info) << "BT peer ID " << bt_id.to_string();
//  albert::bt::BT bt(io_service, bt_id);
//  bt.start();

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

