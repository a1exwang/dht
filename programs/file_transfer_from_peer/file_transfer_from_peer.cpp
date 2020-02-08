#include <fstream>
#include <boost/asio/signal_set.hpp>

#include <albert/bt/peer_connection.hpp>
#include <albert/log/log.hpp>

#include <albert/bencode/bencoding.hpp>

using namespace albert;

int main (int argc, char **argv) {
  if (argc < 4) {
    LOG(error) << "invalid argument";
    exit(1);
  }

  boost::asio::io_service io;

  albert::log::initialize_logger(argc >= 5);

  auto peer_ip = boost::asio::ip::address_v4::from_string(argv[1]);
  uint16_t peer_port = atoi(argv[2]);
  std::ifstream ifs(argv[3], std::ios::binary);
  bool use_utp = false;

  auto torrent = std::dynamic_pointer_cast<bencoding::DictNode>(bencoding::Node::decode(ifs));
  auto info = bencoding::get<bencoding::DictNode>(*torrent, "info");
  auto piece_length = bencoding::get<size_t>(info, "piece length");
  auto pieces_s = bencoding::get<std::string>(info, "pieces");
  std::stringstream ss_pieces(pieces_s);
  std::vector<u160::U160> piece_hashes;
  while (ss_pieces.peek() != EOF) {
    piece_hashes.emplace_back(u160::U160::decode(ss_pieces));
  }

  size_t block_size = std::min(piece_length, 1 * 1024ul);

  std::stringstream ss;
  info.encode(ss, bencoding::EncodeMode::Bencoding);
  auto info_hash = u160::U160::hash((const uint8_t*)ss.str().data(), ss.str().size());

  auto name = bencoding::get<std::string>(info, "name");
  LOG(info) << "Downloading '" << name << "', piece length " << piece_length;

  auto self = u160::U160::random();

  auto pc = std::make_shared<albert::bt::peer::PeerConnection>(
      io, self, info_hash, 0, 0, peer_ip.to_uint(), peer_port, use_utp);

  boost::asio::steady_timer timer(io);

  size_t current_index = 0;
  size_t current_offset = 0;

  std::function<void(const boost::system::error_code&)> timer_handler =
      [piece_length, &piece_hashes, &timer, &timer_handler, pc, block_size, &current_index, &current_offset](const boost::system::error_code &e) {
        if (e) {
          LOG(error) << "timer error";
          return;
        }

        if (current_index < piece_hashes.size()) {
          if (current_offset + block_size >= piece_length) {
            current_index++;
            current_offset = 0;
          } else {
            current_offset += block_size;
          }

          pc->request(current_index, current_offset, block_size);
          timer.expires_at(boost::asio::chrono::steady_clock::now() + boost::asio::chrono::microseconds(100));
          timer.async_wait(timer_handler);
        }
      };
  size_t total_got = 0;
  auto block_handler = [&total_got, pc, piece_length, block_size, &current_index, &current_offset](size_t piece, size_t offset, std::vector<uint8_t> data) {
      total_got += data.size();
      LOG(info) << "got block " << piece << " " << offset << " " << data.size() << " total " << total_got;
  };

  pc->set_block_handler(block_handler);
  pc->connect(
      [pc, block_size, &timer, &timer_handler]() {
        LOG(info) << "BitTorrent protocol: Connected to peer";
        pc->interest([=, &timer, &timer_handler]() {
          timer.async_wait(timer_handler);
        });
      },
      [pc](int piece, size_t size) {
        LOG(info) << "got metadata info, pieces: " << piece << " total size " << size;
      });

  io.run();
}
