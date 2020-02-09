#include <fstream>
#include <map>
#include <set>
#include <random>

#include <boost/asio/signal_set.hpp>

#include <albert/bt/peer_connection.hpp>
#include <albert/log/log.hpp>
#include <albert/bencode/bencoding.hpp>

using namespace albert;

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
  bool use_utp = false;

  Torrent torrent;
  torrent.parse_file(torrent_file);

  size_t block_size = std::min(torrent.piece_length, 16 * 1024ul);
  LOG(info) << "Downloading '" << torrent.name << "', piece length " << torrent.piece_length;

  auto self = u160::U160::random();

  auto pc = std::make_shared<albert::bt::peer::PeerConnection>(
      io, self, torrent.info_hash, 0, 0, peer_ip.to_uint(), peer_port, use_utp);

  size_t max_queue_size = 10;
  size_t total_got = 0;
  std::map<size_t, std::set<size_t>> unfinished_blocks;
  std::map<size_t, std::set<size_t>> available_blocks;
  std::map<std::tuple<size_t, size_t>, size_t> request_queue;

  auto send_block_request = [&, pc, block_size, max_queue_size](size_t piece, size_t offset) {
    if (request_queue.find({piece, offset}) == request_queue.end()) {
      pc->request(piece, offset, block_size);
      LOG(info) << "queue size " << request_queue.size();
      request_queue.insert(std::make_pair(std::make_tuple(piece, offset), block_size));
      available_blocks[piece].erase(offset);
      if (available_blocks[piece].empty()) {
        available_blocks.erase(piece);
      }
    } else {
      LOG(error) << "block request already exists " << piece << " " << offset;
    }
  };

  auto block_finished = [&](size_t piece, size_t offset) {
    if (request_queue.find({piece, offset}) == request_queue.end()) {
      LOG(error) << "error, unknown request done: " << piece << " " << offset;
    } else {
      request_queue.erase({piece, offset});
    }
  };

  std::random_device rng{};
  std::uniform_int_distribution<size_t> dist;
  auto find_unfinished_block = [&, pc]() -> std::tuple<size_t, size_t> {
    if (available_blocks.size() > 0) {
//      auto it = std::next(available_blocks.begin(), dist(rng) % available_blocks.size());
      auto it = available_blocks.begin();
      return {it->first, *it->second.begin()};
    } else {
      throw std::runtime_error("available block not found");
    }
  };

  auto block_handler = [=, &block_finished, &total_got, &torrent, &request_queue](size_t piece, size_t offset, std::vector<uint8_t> data) {
    total_got += data.size();
    LOG(info) << "got block " << piece << " " << offset << " " << data.size() << " " << total_got << "/" << torrent.total_size << " "
              << std::setprecision(2) << std::fixed << (100.0*total_got/torrent.total_size) << "%";
    block_finished(piece, offset);
    if (request_queue.size() <= max_queue_size) {
      try {
        auto [piece, offset] = find_unfinished_block();
        send_block_request(piece, offset);
      } catch (const std::exception &e) {
        LOG(info) << "error occurred while finding next block to download: " << e.what();
      }
    }
  };

  auto fill_block_info = [&]() {
    for (int i = 0; i < torrent.total_pieces; i++) {
      if (pc->has_piece(i)) {
        if (i == torrent.piece_length - 1) {
          size_t last_piece_size = torrent.total_size % torrent.piece_length;
          size_t last_block_count = last_piece_size/block_size;
          for (int j = 0; j < last_block_count; j++) {
            available_blocks[i].insert(j*block_size);
          }
        } else {
          for (int j = 0; j < torrent.piece_length/block_size; j++) {
            available_blocks[i].insert(j*block_size);
          }
        }
      } else {
        if (i == torrent.piece_length - 1) {
          size_t last_piece_size = torrent.total_size % torrent.piece_length;
          size_t last_block_count = last_piece_size/block_size;
          for (int j = 0; j < last_block_count; j++) {
            unfinished_blocks[i].insert(j*block_size);
          }
        } else {
          for (int j = 0; j < torrent.piece_length/block_size; j++) {
            unfinished_blocks[i].insert(j*block_size);
          }
        }
      }
    }
  };

  pc->set_block_handler(block_handler);
  pc->connect(
      [pc, block_size, max_queue_size, &fill_block_info, &find_unfinished_block, &send_block_request]() {
        LOG(info) << "BitTorrent protocol: Connected to peer";
        pc->interest([=, &fill_block_info, &find_unfinished_block, &send_block_request]() {
          fill_block_info();
          for (int i = 0; i < max_queue_size; i++) {
            auto [piece, offset] = find_unfinished_block();
            send_block_request(piece, offset);
          }
        });
      },
      [pc](int piece, size_t size) {
        LOG(info) << "got metadata info, pieces: " << piece << " total size " << size;
      });

  io.run();
}
