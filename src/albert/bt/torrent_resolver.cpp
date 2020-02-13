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
    :io_(io), timer_(io_), info_hash_(info_hash), self_(self), bind_ip_(bind_ip), bind_port_(bind_port), use_utp_(use_utp),
    has_metadata_(false), expiration_at_(expiration_at) { }

TorrentResolver::TorrentResolver(
    boost::asio::io_service &io,
    u160::U160 info_hash,
    u160::U160 self,
    uint32_t bind_ip,
    uint16_t bind_port,
    bool use_utp,
    const bencoding::DictNode &info)
    :io_(io), timer_(io_),
     info_hash_(info_hash), self_(self), bind_ip_(bind_ip), bind_port_(bind_port),
     use_utp_(use_utp), info_(info), has_metadata_(true) {

  timer_.expires_at(boost::asio::chrono::steady_clock::now());
  timer_.async_wait(std::bind(&TorrentResolver::timer_handler, this, std::placeholders::_1));
}

void TorrentResolver::add_peer(uint32_t ip, uint16_t port) {
  using namespace std::placeholders;

  auto pc = std::make_shared<peer::PeerConnection>(
      io_,
      self(),
      info_hash_,
      bind_ip_,
      bind_port_,
      ip,
      port,
      use_utp_);
  peer_connections_[{ip, port}] = pc;
  pc->connect(
      [that = shared_from_this(), ip, port](const boost::system::error_code &e) {
        if (e) {
          if (that->peer_connections_.find({ip, port}) != that->peer_connections_.end()) {
            that->peer_connections_.erase({ip, port});
          }
        }
      },
      std::bind(&TorrentResolver::handshake_handler, this, pc, _1, _2)
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
  return ret;
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
    auto locked_pc = pc.lock();
    locked_pc->interest([locked_pc](){
      LOG(info) << "TorrentResolver: peer unchoked " << locked_pc->peer().to_string();
    });
    locked_pc->start_metadata_transfer(
        boost::bind(&TorrentResolver::piece_handler, this, pc, _1, _2));
  }
}
TorrentResolver::~TorrentResolver() {
  for (auto &pc : peer_connections_) {
    pc.second->close();
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
size_t TorrentResolver::connected_peers() const {
  size_t ret = 0;
  for (auto &item : peer_connections_) {
    if (item.second->status() == peer::ConnectionStatus::Connected) {
      ret++;
    }
  }
  return ret;
}

void TorrentResolver::timer_handler(const boost::system::error_code &e) {
  if (e) {
    LOG(error) << "TorrentResolver timer_handler error";
    return;
  }

  std::list<std::tuple<uint32_t, uint16_t>> to_delete;
  for (auto &item : peer_connections_) {
    if (item.second->status() == peer::ConnectionStatus::Disconnected) {
      to_delete.push_back(item.first);
    }
  }
  if (to_delete.size() > 0) {
    LOG(info) << "TorrentResolver: deleted " << to_delete.size() << " failed connections";
  }
  for (auto &ep : to_delete) {
    peer_connections_.erase(ep);
  }

  timer_.expires_at(boost::asio::chrono::steady_clock::now() + timer_interval_);
  timer_.async_wait(std::bind(&TorrentResolver::timer_handler, this, std::placeholders::_1));
}

}