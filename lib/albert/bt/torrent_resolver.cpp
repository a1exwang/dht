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
#include <albert/u160/u160.hpp>

namespace albert::bt {

TorrentResolver::TorrentResolver(
    boost::asio::io_service &io,
    u160::U160 info_hash,
    u160::U160 self,
    uint32_t bind_ip,
    uint16_t bind_port,
    bool use_utp,
    std::chrono::high_resolution_clock::time_point expiration_at
    )
    :io_(io), info_hash_(info_hash), self_(self), bind_ip_(bind_ip), bind_port_(bind_port), use_utp_(use_utp), expiration_at_(expiration_at), has_metadata_(false) { }

TorrentResolver::TorrentResolver(
    boost::asio::io_service &io,
    u160::U160 info_hash,
    u160::U160 self,
    uint32_t bind_ip,
    uint16_t bind_port,
    bool use_utp,
    const bencoding::DictNode &info)
    :io_(io), info_hash_(info_hash), self_(self), bind_ip_(bind_ip), bind_port_(bind_port), use_utp_(use_utp), info_(info), has_metadata_(true) { }

void TorrentResolver::add_peer(uint32_t ip, uint16_t port) {
  using namespace std::placeholders;

  peer_connections_.push_back(
      std::make_shared<peer::PeerConnection>(
          io_,
          self(),
          info_hash_,
          bind_ip_,
          bind_port_,
          ip,
          port,
          use_utp_));
  auto pc = peer_connections_.back();
  pc->connect(
      []() { },
      std::bind(&TorrentResolver::handshake_handler, this, pc->weak_from_this(), _1, _2)
      );
}

void TorrentResolver::piece_handler(std::weak_ptr<peer::PeerConnection> pc, int piece, const std::vector<uint8_t> &data) {
  if (piece >= 0 && piece < pieces_.size()) {
    if (pieces_[piece].empty()) {
      pieces_[piece] = data;
      LOG(info) << "TorrentResolver: " << info_hash_.to_string() << ", got piece " << piece
                << ", piece: " << pieces_got() << "/" << pieces_.size()
                << ", data: " << data_got() << "/" << metadata_size_;
    } else {
      LOG(info) << "already have piece " << piece << ", ignored";
    }
  } else {
    LOG(error) << "Invalid piece id " << piece << ", " << pieces_.size() << " in total";
  }
  if (finished()) {
    LOG(info) << "torrent finished " << info_hash_.to_string();
    auto info_data = merged_pieces();
    auto calculated_hash = u160::U160::hash(info_data.data(), info_data.size());
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

u160::U160 TorrentResolver::self() const {
  return self_;
}

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
  return data_got() == metadata_size_;
}
void TorrentResolver::set_torrent_handler(std::function<void(const bencoding::DictNode &torrent)> handler) {
  torrent_handler_ = std::move(handler);
}
void TorrentResolver::handshake_handler(std::weak_ptr<peer::PeerConnection> pc, int total_pieces, size_t metadata_size) {
  using namespace boost::placeholders;
  if (total_pieces == 0) {
    throw std::invalid_argument("Invalid argument, total_pieces cannot be zero");
  }
  this->pieces_.resize(total_pieces);
  this->metadata_size_ = metadata_size;
  if (pc.expired()) {
    LOG(error) << "PeerConnection gone before handshake was handled info_hash: " << this->info_hash_.to_string();
  } else {
    pc.lock()->start_metadata_transfer(
        boost::bind(&TorrentResolver::piece_handler, this, pc, _1, _2));
  }
}
TorrentResolver::~TorrentResolver() {
  for (auto &pc : peer_connections_) {
    pc->close();
  }
}
size_t TorrentResolver::pieces_got() const {
  size_t n = 0;
  for (auto &piece : pieces_) {
    if (!piece.empty())
      n++;
  }
  return n;
}
size_t TorrentResolver::data_got() const {
  size_t n = 0;
  for (auto &piece : pieces_) {
    n += piece.size();
  }
  return n;
}

}