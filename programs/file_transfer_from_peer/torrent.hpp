#pragma once


#include <fstream>
#include <map>
#include <set>
#include <random>
#include <unordered_set>

#include <boost/asio/signal_set.hpp>

#include <albert/common/commom.hpp>
#include <albert/bt/peer_connection.hpp>
#include <albert/bt/peer.hpp>
#include <albert/log/log.hpp>
#include <albert/bencode/bencoding.hpp>
#include <albert/flow_control/rps_throttler.hpp>

using namespace albert;
using namespace albert::common;

struct Torrent {
  void parse_file(const std::string &file) {
    std::ifstream ifs(file, std::ios::binary);
    auto torrent = std::dynamic_pointer_cast<bencoding::DictNode>(bencoding::Node::decode(ifs));
    auto info = bencoding::get<bencoding::DictNode>(*torrent, "info");
    piece_length = bencoding::get<size_t>(info, "piece length");

    auto pieces_s = bencoding::get<std::string>(info, "pieces");
    std::stringstream ss_pieces(pieces_s);
    while (ss_pieces.peek() != EOF) {
      piece_hashes.emplace_back(u160::U160::decode(ss_pieces));
    }

    std::stringstream ss;
    info.encode(ss, bencoding::EncodeMode::Bencoding);
    info_hash = u160::U160::hash((const uint8_t*)ss.str().data(), ss.str().size());

    name = bencoding::get<std::string>(info, "name");

    total_pieces = piece_hashes.size();
    if (info.dict().find("files") == info.dict().end()) {
      // single file mode
      LOG(error) << "single file mode not implemented";
      assert(0);
    } else {
      // multi file mode
      auto files_node = bencoding::get<std::vector<std::shared_ptr<bencoding::DictNode>>>(info, "files");
      size_t current_offset = 0;
      for (auto file_node : files_node) {
        auto length = bencoding::get<size_t>(*file_node, "length");
        auto path = bencoding::get<bencoding::ListNode>(*file_node, "path");
        current_offset += length;
      }
      total_size = current_offset;
    }
  }
  u160::U160 info_hash;
  size_t piece_length;
  size_t total_size;
  size_t total_pieces;
  std::string name;
  std::vector<u160::U160> piece_hashes;
};

class BlockManager {
 public:
  BlockManager(size_t block_size, size_t piece_size, size_t total_size) : block_size_(block_size), piece_size_(piece_size), total_size_(total_size) { }

  size_t piece_count() const {
    return std::ceil((double)total_size_ / piece_size_);
  }
  void set_peer_has_piece(u160::U160 peer_id, size_t piece) {
//    LOG(info) << "set_peer_has_piece " << peer_id.to_string() << " " << piece;
    size_t blocks_in_piece = piece_size_ / block_size_;
    if (piece == piece_count() - 1) {
      size_t last_piece_blocks = std::ceil(double(total_size_ % piece_size_) / block_size_);
      for (int i = 0; i < blocks_in_piece; i++) {
        available_blocks[piece][i * block_size_].insert(peer_id);
      }
    } else {
      for (int i = 0; i < blocks_in_piece; i++) {
        available_blocks[piece][i * block_size_].insert(peer_id);
      }
    }
  }

  std::tuple<size_t, size_t> get_block(u160::U160 peer_id) {
    for (auto &item : available_blocks) {
      bool found = false;
      std::tuple<size_t, size_t> target;
      size_t block_offset = 0;
      for (auto &block : item.second) {
        if (block.second.find(peer_id) != block.second.end()) {
          target = std::make_tuple(item.first, block.first);
          block.second.erase(peer_id);
          request_queue[target].insert(peer_id);
          block_offset = block.first;
          found = true;
          break;
        }
      }
      if (found) {
        if (item.second.at(block_offset).empty()) {
          item.second.erase(block_offset);
        }

        return target;
      }
    }
    throw std::invalid_argument("peer has drain");
  }

  void mark_block_done(size_t piece, size_t offset) {
    if (request_queue.find({piece, offset}) == request_queue.end()) {
      LOG(error) << "error, unknown request done: " << piece << " " << offset;
    } else {
      request_queue.erase({piece, offset});
    }
  }

  size_t pending_blocks() const {
    size_t ret = 0;
    for (auto &item : request_queue) {
      ret += item.second.size();
    }
    return ret;
  }

  bool peer_finished(u160::U160 peer_id) const {
    return false;
  }

  bool finished() const {
    return done_blocks.size() == std::ceil((double)total_size_ / block_size_);
  }
 public:
  size_t block_size_;
  size_t piece_size_;
  size_t total_size_;

  // piece -> <block_offset -> list of peer ids>
  std::map<size_t, std::map<size_t, std::set<u160::U160>>> available_blocks;
  // <piece, block_offset> -> list of peer ids
  std::map<std::tuple<size_t, size_t>, std::set<u160::U160>> request_queue;

  struct TupleHash {
    size_t operator()(const std::tuple<size_t, size_t> &item) const {
      char tmp[2*sizeof(size_t)];
      auto s0 = (size_t*)tmp;
      auto s1 = (size_t*)(tmp + sizeof(size_t));
      *s0 = std::get<0>(item);
      *s1 = std::get<1>(item);
      std::hash<std::string_view> h;
      return h(tmp);
    }
  };
  std::unordered_set<std::tuple<size_t, size_t>, TupleHash> done_blocks;

};

struct Peer {
  sp<bt::peer::PeerConnection> pc;
  std::string status = "initialized";
};

class Task {
 public:
  Task(boost::asio::io_service &io, u160::U160 self, std::string torrent_file, bool use_utp)
      :io(io), self(self), use_utp(use_utp), throttler_(io, true, 100) {
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
  static std::string make_progress(size_t x, size_t total) {
    std::stringstream ss;
    ss << utils::pretty_size(x) << "/" << utils::pretty_size(total) << "("
       << std::fixed << std::setprecision(2) << (100.0*x/total) << "%)";
    return ss.str();
  };

  void block_handler(std::shared_ptr<bt::peer::PeerConnection> pc, size_t piece, size_t offset, gsl::span<uint8_t> data) {
    connections_[{pc->peer().ip(), pc->peer().port()}].status = "DataReceived";
    total_got += data.size();
    total_blocks_got++;
    block_manager_->mark_block_done(piece, offset);
    if (block_manager_->finished()) {
      LOG(info) << "task finished";
    } else {
      if (block_manager_->peer_finished(pc->peer_id())) {
        LOG(info) << "peer finished";
      } else {
        auto [new_piece, new_offset] = block_manager_->get_block(pc->peer_id());
        LOG(debug) << "start to requesting " << new_piece << " " << new_offset;
        throttler_.throttle([=, new_piece = new_piece, new_offset = new_offset]() {
          pc->request(new_piece, new_offset, block_size);
        });
      }
    }

    auto time_diff = std::chrono::high_resolution_clock::now() - last_report_time;
    if (time_diff > std::chrono::seconds(1)) {
      auto diff = total_got - last_report_size;
      auto blocks_diff = total_blocks_got - last_blocks_got;
      auto secs = std::chrono::duration<double>(time_diff).count();

      LOG(info) << "FileTransfer report: "
                << "progress " << make_progress(total_got, block_manager_->total_size_) << " "
                << "speed " << utils::pretty_size(diff/secs) << "/s "
                << "blocks " << utils::pretty_size(blocks_diff/secs, false) << "/s "
                ;
      throttler_.stat();

      last_report_size = total_got;
      last_report_time = std::chrono::high_resolution_clock::now();
      last_blocks_got = total_blocks_got;
    }
  };

 private:
  boost::asio::io_service& io;
  u160::U160 self;

  std::map<std::tuple<uint32_t, uint16_t>, Peer> connections_;
  sp<BlockManager> block_manager_;
  size_t max_queue_size = 16;

  Torrent torrent;
  bool use_utp;
  size_t block_size;

  size_t total_got = 0;
  size_t total_blocks_got = 0;
  size_t last_report_size = 0;
  size_t last_blocks_got = 0;
  std::chrono::high_resolution_clock::time_point last_report_time;

  flow_control::RPSThrottler throttler_;
};
