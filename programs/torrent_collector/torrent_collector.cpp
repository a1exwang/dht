#include <exception>
#include <sstream>

#include <albert/bt/bt.hpp>
#include <albert/cui/cui.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>

#include <boost/asio/io_service.hpp>

int main(int argc, char* argv[]) {
  albert::dht::Config config;
  auto args = albert::config::argv2args(argc, argv);
  args = config.from_command_line(args);
  albert::config::throw_on_remaining_args(args);
  albert::log::initialize_logger(config.debug);

  boost::asio::io_service io_service{};
  albert::dht::DHTInterface dht(std::move(config), io_service);
  dht.start();
  dht.sample_infohashes([](const albert::u160::U160 &info_hash) {
    LOG(info) << "got info hash " << info_hash.to_string();
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

