#include <albert/bt/torrent_resolver.hpp>

#include <cstddef>
#include <cstdint>

#include <memory>
#include <utility>

#include <boost/bind/bind.hpp>

#include <albert/bencode/bencoding.hpp>
#include <albert/bt/peer.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>

namespace albert::bt {

void TorrentResolver::add_peer(uint32_t ip, uint16_t port) {
  using namespace boost::placeholders;
  peer_connections_.push_back(
      std::make_shared<peer::PeerConnection>(
          io_,
          self(),
          info_hash_,
          ip,
          port));
  auto &pc = peer_connections_.back();
  pc->set_piece_data_handler(
      boost::bind(
          &TorrentResolver::piece_handler, this, _1, _2));
  pc->set_metadata_handshake_handler(boost::bind(&TorrentResolver::handshake_handler, this, _1, _2));
  pc->connect();
}

void TorrentResolver::piece_handler(int piece, const std::vector<uint8_t> &data) {
  if (piece >= 0 && piece < pieces_.size()) {
    if (pieces_[piece].empty()) {
      LOG(info) << "got piece " << piece << ", total " << pieces_.size();
      pieces_[piece] = data;
    } else {
      LOG(info) << "already have piece " << piece << ", ignored";
    }
  } else {
    LOG(error) << "Invalid piece id " << piece << ", " << pieces_.size() << " in total";
  }
  if (finished()) {
    LOG(info) << "torrent finished " << info_hash_.to_string();
    auto info_data = merged_pieces();
    auto calculated_hash = krpc::NodeID::hash(info_data.data(), info_data.size());
    if (calculated_hash == info_hash_) {
      std::stringstream ss(std::string((const char*)info_data.data(), info_data.size()));
      auto info = bencoding::Node::decode(ss);
      std::map<std::string, std::shared_ptr<bencoding::Node>> dict;
      dict["announce"] = std::make_shared<bencoding::DictNode>(std::map<std::string, std::shared_ptr<bencoding::Node>>());
      dict["info"] = info;
      bencoding::DictNode torrent(std::move(dict));
      if (torrent_handler_) {
        torrent_handler_(torrent);
      }
    } else {
      LOG(error) << "hash of Torrent.info(" << calculated_hash.to_string() << ") not match info-hash(" << info_hash_.to_string() << ")";
    }
  }
}

krpc::NodeID TorrentResolver::self() const {
  return self_;
}
TorrentResolver::TorrentResolver(
    boost::asio::io_service &io, krpc::NodeID info_hash, krpc::NodeID self)
    :io_(io), info_hash_(info_hash), self_(self) { }

std::vector<uint8_t> TorrentResolver::merged_pieces() const {
  std::vector<uint8_t> ret(metadata_size_);
  auto it = ret.begin();
  for (const auto &piece : pieces_) {
    it = std::copy(piece.begin(), piece.end(), it);
  }
  assert(it == ret.end());
  return std::move(ret);
}
bool TorrentResolver::finished() const {
  size_t n = 0;
  for (auto &piece : pieces_) {
    n += piece.size();
  }
  return n == metadata_size_;
}
void TorrentResolver::set_torrent_handler(std::function<void(const bencoding::DictNode &torrent)> handler) {
  torrent_handler_ = std::move(handler);
}
void TorrentResolver::handshake_handler(int total_pieces, size_t metadata_size) {
  if (total_pieces == 0) {
    throw std::invalid_argument("Invalid argument, total_pieces cannot be zero");
  }
  this->pieces_.resize(total_pieces);
  this->metadata_size_ = metadata_size;
}
TorrentResolver::~TorrentResolver() {
  for (auto &pc : peer_connections_) {
    pc->close();
  }
}

}