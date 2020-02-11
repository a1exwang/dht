#include <exception>
#include <set>
#include <fstream>

#include <boost/asio/io_service.hpp>

#include <albert/cui/cui.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>
#include <albert/io_latency/io_latency.hpp>
#include <albert/bt/peer.hpp>

#include "torrent.hpp"

enum class PeerStatus {

};

struct Peer {
  sp<bt::peer::PeerConnection> pc;
  std::string status = "initialized";
};

class Task {
 public:
  Task(boost::asio::io_service &io, u160::U160 self, std::string torrent_file) :io(io), self(self) {
    torrent.parse_file(torrent_file);
    LOG(info) << "Downloading '" << torrent.name << "', piece length " << torrent.piece_length;
    block_size = std::min(torrent.piece_length, 16 * 1024ul);
    block_manager_ = std::make_shared<BlockManager>(block_size, torrent.piece_length, torrent.total_size);
  }

  void stat() {
    LOG(info) << "Task: " << torrent.name;
    size_t available_count = 0;
    size_t queued_count = 0;
    for (auto &item: block_manager_->available_blocks) {
      auto piece = item.first;
      for (auto &block : item.second) {
        if (block.second.size() > 0) {
          available_count++;
        } else {
          LOG(warning) << "inconsistend data avialble blocks[" << piece << "][" << block.first << "] is empty";
        }
      }
    }
    for (auto &item : block_manager_->request_queue) {
      queued_count++;
    }

    auto make_progress = [](size_t x, size_t total) -> std::string {
      std::stringstream ss;
      ss << utils::pretty_size(x) << "/" << utils::pretty_size(total) << "("
         << std::fixed << std::setprecision(2) << (100.0*x/total) << "%)";
      return ss.str();
    };

    size_t total_not_downloaded = block_manager_->total_size_ - total_got;
    LOG(info) << "BlockManager: total peers " << connections_.size() << " "
              << "available "
              << make_progress(available_count*block_manager_->block_size_, total_not_downloaded) << " "
              << "progress "
              << make_progress(total_got, block_manager_->total_size_);

    LOG(info) << "Peers:";
    for (auto &c : connections_) {
      LOG(info) << c.second.pc->peer().to_string() << " status: '" << c.second.status << "'";
    }
  }

  void add_peer(uint32_t ip, uint16_t port) {
    using namespace std::placeholders;
    auto pc = std::make_shared<albert::bt::peer::PeerConnection>(
        io, self, torrent.info_hash, 0, 0, ip, port, use_utp);
    pc->set_block_handler(std::bind(&Task::block_handler, this, pc, _1, _2, _3));
    pc->connect(
        [this, pc, ip, port](const boost::system::error_code &e) {
          if (e) {
            connections_[{ip, port}].status = "Failed: " + e.message();
          } else {
            connections_[{ip, port}].status = "BitTorrentConnected";
            pc->interest(std::bind(&Task::handle_unchoke, this, ip, port, pc));
          }
        },
        [pc](int piece, size_t size) {
          LOG(info) << "got metadata info from " << pc->peer().to_string() <<  ", pieces: " << piece << " total size " << size;
        });
    connections_[{ip, port}] = {pc};
  }

  void handle_unchoke(uint32_t ip, uint16_t port, std::shared_ptr<bt::peer::PeerConnection> pc) {

    connections_[{ip, port}].status = "Unchoke";
    LOG(info) << "Peer unchoke, start requsting data " << pc->peer().to_string();
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
  u160::U160 info_hash() const { return torrent.info_hash; }

 private:

  void block_handler(std::shared_ptr<bt::peer::PeerConnection> pc, size_t piece, size_t offset, gsl::span<uint8_t> data) {
    connections_[{pc->peer().ip(), pc->peer().port()}].status = "DataReceived";
    total_got += data.size();
    LOG(info) << "got block, " << piece << " " << offset/block_size << "/" << torrent.piece_length/block_size << " "
              << "from " << pc->peer().to_string() << " "
              << "size " << data.size() << " "
              << "progress " << utils::pretty_size(total_got) << "/" << utils::pretty_size(torrent.total_size) << " "
              << std::setprecision(2) << std::fixed << (100.0*total_got/torrent.total_size) << "%";
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
  };

 private:
  boost::asio::io_service& io;
  u160::U160 self;

  std::map<std::tuple<uint32_t, uint16_t>, Peer> connections_;
  sp<BlockManager> block_manager_;
  size_t max_queue_size = 16;

  Torrent torrent;
  bool use_utp = true;
  size_t block_size;

  size_t total_got = 0;
};

int main(int argc, const char* argv[]) {

  /* Config parsing */
  std::string torrent_file = argv[argc - 1];
  auto args = albert::config::argv2args(argc - 1, argv);
  albert::dht::Config dht_config;
  albert::bt::Config bt_config;
  args = dht_config.from_command_line(args);
  args = bt_config.from_command_line(args);
  albert::config::throw_on_remaining_args(args);

  /* Initialization */
  bool debug = dht_config.debug;
  albert::log::initialize_logger(dht_config.debug);
  boost::asio::io_service io_service{};
  albert::dht::DHTInterface dht(std::move(dht_config), io_service);
  albert::bt::BT bt(io_service, std::move(bt_config));

  /* Start service */
  dht.start();

  Task task(io_service, bt.self(), torrent_file);

  boost::asio::steady_timer wait_timer(io_service, boost::asio::chrono::seconds(5));
  std::function<void(const boost::system::error_code &)> timer_handler = [&](const boost::system::error_code &e) {
    if (e) {
      LOG(info) << "timer error";
      exit(1);
    }

    dht.get_peers(task.info_hash(), [&](uint32_t ip, uint16_t port) {
      task.add_peer(ip, port);
    });

    task.stat();

    wait_timer.expires_at(wait_timer.expiry() + boost::asio::chrono::seconds(5));
    wait_timer.async_wait(timer_handler);
  };

  wait_timer.async_wait(timer_handler);


//  albert::io_latency::IOLatencyMeter meter(io_service, debug);
#ifdef NDEBUG
  try {
#endif
  io_service.run();
//  meter.loop();
#ifdef NDEBUG
  } catch (const std::exception &e) {
    LOG(error) << "io_service Failure: \"" << e.what() << '"';
    exit(1);
  }
#endif

  return 0;
}

