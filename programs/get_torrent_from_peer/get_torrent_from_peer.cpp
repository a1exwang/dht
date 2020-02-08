#include <boost/asio/signal_set.hpp>

#include <albert/bt/peer_connection.hpp>
#include <albert/log/log.hpp>

#include <albert/bencode/bencoding.hpp>
#include <backward.hpp>

using namespace albert;

int main (int argc, char **argv) {
  if (argc < 4) {
    LOG(error) << "invalid argument";
    exit(1);
  }

  backward::SignalHandling sh;
  boost::asio::io_service io;

  albert::log::initialize_logger(argc >= 5);
//  boost::asio::signal_set signals(io, SIGINT);
//  signals.async_wait([&](const boost::system::error_code& error, int signal_number) {
//    if (error) {
//      LOG(error) << "Failed to async wait signals '" << error.message() << "'";
//      return;
//    }
//    LOG(info) << "Exiting due to signal " << signal_number;
//    io.stop();
//  });

  auto peer_ip = boost::asio::ip::address_v4::from_string(argv[1]);
  uint16_t peer_port = atoi(argv[2]);
  auto target = u160::U160::from_hex(argv[3]);

  auto self = u160::U160::random();
  std::vector<uint8_t> metadata;
  std::vector<int> piece_ok;

  std::shared_ptr<albert::bt::peer::PeerConnection> pc;
  auto piece_handler = [&piece_ok, &metadata, &target](std::shared_ptr<albert::bt::peer::PeerConnection> pc, int piece, const std::vector<uint8_t> &data) {
    LOG(info) << "got piece data " << piece << ", size " << data.size();
    std::copy(data.begin(), data.end(), std::next(metadata.begin(), piece * 16 * 1024));
    piece_ok[piece] = 1;
    bool all_ok = true;
    for (int i = 0; i < piece_ok.size(); i++) {
      if (!piece_ok[i]) {
        all_ok = false;
        LOG(info) << "not ok " << i;
        break;
      }
    }
    if (all_ok) {
      auto calculated_hash = u160::U160::hash(metadata.data(), metadata.size());
      if (calculated_hash == target) {
        std::stringstream ss(std::string((const char *)metadata.data(), metadata.size()));
        auto info = bencoding::Node::decode(ss);
        std::map<std::string, std::shared_ptr<bencoding::Node>> dict;
        dict["announce"] =
            std::make_shared<bencoding::DictNode>(std::map<std::string, std::shared_ptr<bencoding::Node>>());
        dict["info"] = info;
        bencoding::DictNode torrent(std::move(dict));

        auto file_name = target.to_string() + ".torrent";
        std::ofstream ofs(file_name, std::ios::binary);
        torrent.encode(ofs, bencoding::EncodeMode::Bencoding);
        LOG(info) << "torrent save to " << file_name;
      } else {
        LOG(info) << "Invalid info_hash, torrent corrupted";
      }
      pc->close();
    }
  };
  auto extended_handshake_handler = [&metadata, &piece_ok, &piece_handler](
      std::shared_ptr<albert::bt::peer::PeerConnection> pc, int piece, size_t size) {
    LOG(info) << "got metadata info, pieces: " << piece << " total size " << size;
    metadata.resize(size);
    piece_ok.resize(piece);
    pc->start_metadata_transfer(piece_handler);
  };

  pc = std::make_shared<albert::bt::peer::PeerConnection>(
      io, self, target, 0, 0, peer_ip.to_uint(), peer_port, true);

  pc->connect([](std::shared_ptr<albert::bt::peer::PeerConnection> pc) {
    LOG(info) << "BitTorrent protocol: Connected to peer";
  }, extended_handshake_handler);

  io.run();
}