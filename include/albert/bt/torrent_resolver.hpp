#pragma once
#include <list>
#include <memory>
#include <set>

#include <mutex>
#include <atomic>

#include <albert/krpc/krpc.hpp>
#include <albert/u160/u160.hpp>
#include <albert/common/commom.hpp>

#include <boost/asio/steady_timer.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}
namespace boost::system {
class error_code;
}

namespace albert::bt {
namespace peer {
class PeerConnection;
}
class TorrentResolver :public std::enable_shared_from_this<TorrentResolver> {
  static std::mutex pointers_lock_;
  static std::set<TorrentResolver*> pointers_;
 public:
  static std::set<TorrentResolver*> get_pointers() {
    std::unique_lock _(pointers_lock_);
    return pointers_;
  }
  static size_t get_pointers_size() {
    std::unique_lock _(pointers_lock_);
    return pointers_.size();
  }

  TorrentResolver(
      boost::asio::io_service &io,
      u160::U160 info_hash,
      u160::U160 self,
      uint32_t bind_ip,
      uint16_t bind_port,
      bool use_utp,
      std::chrono::high_resolution_clock::time_point expiration_at_);
  TorrentResolver(
      boost::asio::io_service &io,
      u160::U160 info_hash,
      u160::U160 self,
      uint32_t bind_ip,
      uint16_t bind_port,
      bool use_utp,
      const bencoding::DictNode &info);
  ~TorrentResolver();
  void add_peer(uint32_t ip, uint16_t port);

  [[nodiscard]]
  bool finished() const;

  [[nodiscard]]
  size_t pieces_got() const;

  [[nodiscard]]
  size_t data_got() const;

  [[nodiscard]]
  u160::U160 self() const;

  [[nodiscard]]
  size_t connected_peers() const;

  [[nodiscard]]
  size_t peer_count() const {
    return peer_connections_.size();
  }

  [[nodiscard]]
  bool timeout() const {
    return std::chrono::high_resolution_clock::now() > expiration_at_;
  }

  [[nodiscard]]
  size_t memory_size() const;

  [[nodiscard]]
  std::map<std::string, size_t> peers_stat() const;

  void set_torrent_handler(std::function<void(const bencoding::DictNode &torrent)> handler);
 private:
  void piece_handler(wp<peer::PeerConnection> pc, int piece, const std::vector<uint8_t> &data);
  void handshake_handler(wp<peer::PeerConnection> pc, int total_pieces, size_t metdata_size);

  [[nodiscard]]
  std::vector<uint8_t> merged_pieces() const;

  void timer_handler(const boost::system::error_code &e);

 public:
  boost::asio::io_service &io_;
  boost::asio::steady_timer timer_;
  boost::asio::chrono::milliseconds timer_interval_ = boost::asio::chrono::milliseconds(300);
  u160::U160 info_hash_;
  u160::U160 self_;
  std::map<std::tuple<uint32_t, uint16_t>, sp<albert::bt::peer::PeerConnection>> peer_connections_;
  std::map<std::string, size_t> deleted_peers_stat_;

  uint32_t bind_ip_;
  uint16_t bind_port_;
  bool use_utp_;
  std::vector<std::vector<uint8_t>> pieces_;
  size_t metadata_size_;

  std::function<void(const bencoding::DictNode &torrent)> torrent_handler_;

  bencoding::DictNode info_;
  bool has_metadata_;
  size_t total_size_;

  std::chrono::high_resolution_clock::time_point expiration_at_;
};
}