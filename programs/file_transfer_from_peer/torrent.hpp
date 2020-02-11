#pragma once


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
    LOG(info) << "set_peer_has_piece " << peer_id.to_string() << " " << piece;
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

