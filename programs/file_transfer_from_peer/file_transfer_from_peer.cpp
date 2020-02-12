#include <fstream>
#include <map>
#include <set>
#include <random>
#include <unordered_set>
#include <thread>

#include <boost/asio/signal_set.hpp>

#include <albert/common/commom.hpp>
#include <albert/bt/peer_connection.hpp>
#include <albert/log/log.hpp>
#include <albert/bencode/bencoding.hpp>

#include "torrent.hpp"

using namespace albert;
using namespace albert::common;


int main (int argc, char **argv) {
  if (argc < 5) {
    LOG(error) << "invalid argument";
    exit(1);
  }

  boost::asio::io_service io;
  albert::log::initialize_logger(argc >= 6);

  auto peer_ip = boost::asio::ip::address_v4::from_string(argv[1]);
  uint16_t peer_port = atoi(argv[2]);
  auto torrent_file = argv[3];
  int use_utp = atoi(argv[4]);

  auto self = u160::U160::random();
  Task task(io, self, torrent_file, use_utp);

  task.add_peer(peer_ip.to_uint(), peer_port);

  io.run();
}
