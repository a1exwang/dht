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

using namespace albert;
using namespace albert::common;

void start_transmission_fence() { }

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
 private:
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

  std::random_device rng{};
  std::uniform_int_distribution<size_t> dist;
};

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
        [this]() {
          LOG(info) << "BitTorrent protocol: Connected to peer";
          pc->interest(std::bind(&Task::handle_unchoke, this));
        },
        [](int piece, size_t size) {
          LOG(info) << "got metadata info, pieces: " << piece << " total size " << size;
        });
  }

  void handle_unchoke() {
    start_transmission_fence();
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
  bool use_utp = false;
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
