#include <exception>
#include <set>
#include <sstream>

#include <albert/bt/bt.hpp>
#include <albert/cui/cui.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/bt/torrent_resolver.hpp>

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

  auto bind_ip = boost::asio::ip::address_v4::from_string(config.bind_ip).to_uint();
  uint16_t bind_port = config.bind_port;
  albert::dht::DHTInterface dht(std::move(config), io_service);
  dht.start();

  auto bt_id = albert::krpc::NodeID::random();
  LOG(info) << "BT peer ID " << bt_id.to_string();
  albert::bt::BT bt(io_service, bt_id, bind_ip, bind_port);
  bt.start();

  std::ofstream ofs("ih.txt");
  std::set<albert::krpc::NodeID> ihs;
  dht.set_announce_peer_handler([&ofs, &ihs, &bt, &dht](const albert::krpc::NodeID &ih) {
    auto result = ihs.insert(ih);
    if (result.second) {
      LOG(info) << "got info_hash " << ih.to_string() << " started to resolving torrent";
      ofs << ih.to_string() << std::endl;
      ofs.flush();

//      auto resolver = bt.resolve_torrent(ih, [ih](const albert::bencoding::DictNode &torrent) {
//        auto file_name = ih.to_string() + ".torrent";
//        std::ofstream f(file_name, std::ios::binary);
//        torrent.encode(f, albert::bencoding::EncodeMode::Bencoding);
//        LOG(info) << "torrent saved as '" << file_name;
//      });
//
//      dht.get_peers(ih, [ih, resolver](uint32_t ip, uint16_t port) {
//        if (resolver.expired()) {
//          LOG(error) << "TorrentResolver gone before a get_peer request received";
//        } else {
//          resolver.lock()->add_peer(ip, port);
//        }
//      });
    }
  });


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

