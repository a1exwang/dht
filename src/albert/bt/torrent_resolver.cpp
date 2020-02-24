#include <albert/bt/torrent_resolver.hpp>

#include <cstddef>
#include <cstdint>

#include <memory>
#include <utility>

#include <boost/bind/bind.hpp>

#include <albert/bencode/bencoding.hpp>
#include <albert/bt/peer.hpp>
#include <albert/bt/peer_connection.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>

namespace albert::bt {

std::mutex TorrentResolver::pointers_lock_;
std::set<TorrentResolver*> TorrentResolver::pointers_;

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
    has_metadata_(false), expiration_at_(expiration_at) {
  {
    std::unique_lock _(pointers_lock_);
    pointers_.insert(this);
  }

}

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
  {
    std::unique_lock _(pointers_lock_);
    pointers_.insert(this);
  }

  timer_.expires_at(boost::asio::chrono::steady_clock::now());
  timer_.async_wait(std::bind(&TorrentResolver::timer_handler, this, std::placeholders::_1));
}

TorrentResolver::~TorrentResolver() {
  {
    std::unique_lock _(pointers_lock_);
    pointers_.erase(this);
  }
  for (auto &pc : peer_connections_) {
    pc.second->close();
  }
}

void TorrentResolver::add_peer(uint32_t ip, uint16_t port) {
  using namespace std::placeholders;

  auto pc = albert::common::make_shared<peer::PeerConnection>(
      io_,
      self(),
      info_hash_,
      bind_ip_,
      bind_port_,
      ip,
      port,
      use_utp_);
  auto pc2 = pc;
  peer_connections_[{ip, port}] = pc;

  pc->connect(
      [weak_resolver = weak_from_this(), ip, port](const boost::system::error_code &e) {
        if (e) {
          auto resolver = weak_resolver.lock();
          if (resolver) {
            if (resolver->peer_connections_.find({ip, port}) != resolver->peer_connections_.end()) {
              auto &pc = resolver->peer_connections_.at({ip, port});
              std::string key = pc->failed_reason();
              resolver->deleted_peers_stat_[key]++;
              resolver->peer_connections_.erase({ip, port});
            }
          }
        }
      },
      [resolver_weak = weak_from_this(), pc_weak = albert::common::wp<peer::PeerConnection>(pc)](int total_pieces, size_t metadata_size) {
        auto resolver = resolver_weak.lock();
        if (resolver) {
          resolver->handshake_handler(pc_weak, total_pieces, metadata_size);
        }
      });
}

void TorrentResolver::piece_handler(wp<peer::PeerConnection> pc, int piece, const std::vector<uint8_t> &data) {
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
        // TODO: this will cause SEGV?
//        torrent_handler_ = nullptr;
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
void TorrentResolver::handshake_handler(wp<peer::PeerConnection> weak_pc, int total_pieces, size_t metadata_size) {
  if (auto pc = weak_pc.lock(); !pc) {
    LOG(error) << "PeerConnection gone before handshake was handled info_hash: " << this->info_hash_.to_string();
  } else {
    if (total_pieces == 0) {
      LOG(error) << "Peer invalid data, total_pieces == 0, " << pc->peer().to_string();
      pc->close();
    }
    if (this->pieces_.size() == 0) {
      this->pieces_.resize(total_pieces);
      this->metadata_size_ = metadata_size;
    } else {
      if (pieces_.size() != total_pieces || metadata_size_ != metadata_size) {
        LOG(error) << "Peer total_pieces or metadata_size not matched, refusing: " << pc->peer().to_string();
        pc->close();
      }
    }
    pc->interest([weak_pc](){
      if (auto pc = weak_pc.lock(); pc) {
        LOG(info) << "TorrentResolver: peer unchoked " << pc->peer().to_string();
      }
    });
    using namespace boost::placeholders;
    pc->start_metadata_transfer(
        [weak_resolver = weak_from_this(), weak_pc](int piece, const std::vector<uint8_t> &data) {
          if (auto resolver = weak_resolver.lock(); resolver) {
            resolver->piece_handler(weak_pc, piece, data);
          }
        }
    );
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
    auto &pc = peer_connections_.at(ep);
    std::string key = pc->failed_reason();
    deleted_peers_stat_[key]++;
    peer_connections_.erase(ep);
  }

  timer_.expires_at(boost::asio::chrono::steady_clock::now() + timer_interval_);
  timer_.async_wait(std::bind(&TorrentResolver::timer_handler, this, std::placeholders::_1));
}
size_t TorrentResolver::memory_size() const {
  auto ret = sizeof(*this);
  for (auto &pc : peer_connections_) {
    ret += sizeof(pc.first);
    ret += pc.second->memory_size();
  }
  for (auto &piece : pieces_) {
    ret += piece.size();
  }
  return ret;
}
std::map<std::string, size_t> TorrentResolver::peers_stat() const {
  std::map<std::string, size_t> ret = deleted_peers_stat_;
  for (auto &pc : peer_connections_) {
    if (pc.second->status() == peer::ConnectionStatus::Connecting) {
      ret["connecting"]++;
    } else if (pc.second->status() == peer::ConnectionStatus::Connected) {
      ret["connected"]++;
    } else /* disconnected */ {
      ret[pc.second->failed_reason()]++;
    }
  }
  return ret;
}

}