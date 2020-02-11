#include <fstream>
#include <map>
#include <set>
#include <random>
#include <unordered_set>

#include <boost/asio/signal_set.hpp>

#include <albert/common/commom.hpp>
#include <albert/bt/peer_connection.hpp>
#include <albert/log/log.hpp>
#include <albert/bencode/bencoding.hpp>

#include "torrent.hpp"

using namespace albert;
using namespace albert::common;

class Task {
 public:
  Task(boost::asio::io_service &io, u160::U160 self, std::string torrent_file) :io(io), self(self) {
    LOG(info) << "Downloading '" << torrent.name << "', piece length " << torrent.piece_length;
    torrent.parse_file(torrent_file);
    block_size = std::min(torrent.piece_length, 16 * 1024ul);
  }

  void start(uint32_t ip, uint16_t port) {
    using namespace std::placeholders;
    pc = std::make_shared<albert::bt::peer::PeerConnection>(
        io, self, torrent.info_hash, 0, 0, ip, port, use_utp);
    pc->set_block_handler(std::bind(&Task::block_handler, this, _1, _2, _3));
    pc->connect(
        [this](const boost::system::error_code &e) {
          if (!e) {
            LOG(info) << "BitTorrent protocol: Connected to peer";
            pc->interest(std::bind(&Task::handle_unchoke, this));
          }
        },
        [](int piece, size_t size) {
          LOG(info) << "got metadata info, pieces: " << piece << " total size " << size;
        });
  }

  void handle_unchoke() {
    block_manager_ = std::make_shared<BlockManager>(block_size, torrent.piece_length, torrent.total_size);
    for (size_t i = 0; i < torrent.total_pieces; i++) {
      if (pc->has_piece(i)) {
        block_manager_->set_peer_has_piece(pc->peer_id(), i);
      }
    }
    for (int i = 0; i < max_queue_size; i++) {
      auto [piece, offset] = block_manager_->get_block(pc->peer_id());
      pc->request(piece, offset, block_size);
    }
  }

 private:

  void block_handler(size_t piece, size_t offset, gsl::span<uint8_t> data) {
    total_got += data.size();
    LOG(info) << "got block, " << piece << " " << offset/block_size << "/" << torrent.piece_length/block_size << " "
          << " size " << data.size() << " "
          << total_got << "/" << torrent.total_size << " "
          << std::setprecision(2) << std::fixed << (100.0*total_got/torrent.total_size) << "%";
    block_manager_->mark_block_done(piece, offset);
    if (block_manager_->finished()) {
      LOG(info) << "task finished";
    } else {
      if (block_manager_->peer_finished(pc->peer_id())) {
        LOG(info) << "peer finished";
      } else {
        auto [piece, offset] = block_manager_->get_block(pc->peer_id());
        LOG(info) << "queue size " << block_manager_->pending_blocks();
        pc->request(piece, offset, block_size);
      }
    }
  };
 private:
  boost::asio::io_service& io;
  u160::U160 self;

  sp<bt::peer::PeerConnection> pc;
  sp<BlockManager> block_manager_;
  size_t max_queue_size = 16;

  Torrent torrent;
  bool use_utp = true;
  size_t block_size;

  size_t total_got = 0;
};

int main (int argc, char **argv) {
  if (argc < 4) {
    LOG(error) << "invalid argument";
    exit(1);
  }

  boost::asio::io_service io;
  albert::log::initialize_logger(argc >= 5);

  auto peer_ip = boost::asio::ip::address_v4::from_string(argv[1]);
  uint16_t peer_port = atoi(argv[2]);
  auto torrent_file = argv[3];

  auto self = u160::U160::random();
  Task task(io, self, torrent_file);

  task.start(peer_ip.to_uint(), peer_port);

  io.run();
}
