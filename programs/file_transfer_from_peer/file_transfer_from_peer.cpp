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

class Task {
 public:
  Task(boost::asio::io_service &io, u160::U160 self, std::string torrent_file, bool use_utp) :io(io), self(self), use_utp(use_utp) {
    torrent.parse_file(torrent_file);
    LOG(info) << "Downloading '" << torrent.name << "', piece length " << torrent.piece_length;
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

//    size_t blocks_requested = 0;
//    for (size_t i = 0; i < torrent.total_pieces; i++) {
//      if (pc->has_piece(i)) {
//        block_manager_->set_peer_has_piece(pc->peer_id(), i);
//        for (size_t block_id = 0;
//             blocks_requested < max_queue_size && block_id*block_manager_->block_size_ < block_manager_->piece_size_;
//             block_id++, blocks_requested++) {
//
//          auto [piece, offset] = block_manager_->get_block(pc->peer_id());
//          pc->request(piece, offset, block_size);
//        }
//        if (blocks_requested >= max_queue_size) {
//          break;
//        }
//      }
//    }
//
//    std::thread([this](){
//      for (size_t i = 0; i < torrent.total_pieces; i++) {
//        if (pc->has_piece(i)) {
//          io.post([this, i]() {
//            block_manager_->set_peer_has_piece(pc->peer_id(), i);
//            LOG(info) << "set_peer_has_piece " << pc->peer_id().to_string() << " " << i;
//          });
//          std::this_thread::sleep_for(std::chrono::milliseconds(2));
//        }
//      }
//    }).detach();
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
  auto make_progress(size_t x, size_t total) -> std::string {
    std::stringstream ss;
    ss << utils::pretty_size(x) << "/" << utils::pretty_size(total) << "("
       << std::fixed << std::setprecision(2) << (100.0*x/total) << "%)";
    return ss.str();
  };

  void block_handler(size_t piece, size_t offset, gsl::span<uint8_t> data) {
    total_got += data.size();
    block_manager_->mark_block_done(piece, offset);
    if (block_manager_->finished()) {
      LOG(info) << "task finished";
    } else {
      if (block_manager_->peer_finished(pc->peer_id())) {
        LOG(info) << "peer finished";
      } else {
        auto [piece, offset] = block_manager_->get_block(pc->peer_id());
        pc->request(piece, offset, block_size);
      }
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto time_diff = now - last_report_time;
    if (time_diff > std::chrono::seconds(1)) {
      auto diff = total_got - last_report_size;
      auto secs = std::chrono::duration<double>(time_diff).count();

      LOG(info) << "FileTransfer report: "
                << "progress " << make_progress(total_got, block_manager_->total_size_) << " "
                << "speed " << utils::pretty_size(diff/secs) << "/s";


      last_report_size = total_got;
      last_report_time = std::chrono::high_resolution_clock::now();
    }
  };
 private:
  boost::asio::io_service& io;
  u160::U160 self;

  sp<bt::peer::PeerConnection> pc;
  sp<BlockManager> block_manager_;
  size_t max_queue_size = 16;

  Torrent torrent;
  bool use_utp;
  size_t block_size;

  size_t total_got = 0;
  size_t last_report_size = 0;
  std::chrono::high_resolution_clock::time_point last_report_time;
};

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

  task.start(peer_ip.to_uint(), peer_port);

  io.run();
}
