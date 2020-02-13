#pragma once
#include <list>
#include <memory>

#include <albert/krpc/krpc.hpp>
#include <albert/bt/peer_connection.hpp>
#include <albert/u160/u160.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}
namespace boost::system {
class error_code;
}

namespace albert::bt {
class TorrentResolver :public std::enable_shared_from_this<TorrentResolver> {
 public:
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

  void set_torrent_handler(std::function<void(const bencoding::DictNode &torrent)> handler);

  [[nodiscard]]
  size_t memory_size() const {
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
 private:
  void piece_handler(std::weak_ptr<peer::PeerConnection> pc, int piece, const std::vector<uint8_t> &data);
  void handshake_handler(std::weak_ptr<peer::PeerConnection> pc, int total_pieces, size_t metdata_size);

  [[nodiscard]]
  std::vector<uint8_t> merged_pieces() const;

  void timer_handler(const boost::system::error_code &e);

 private:
  boost::asio::io_service &io_;
  boost::asio::steady_timer timer_;
  boost::asio::chrono::milliseconds timer_interval_ = boost::asio::chrono::milliseconds(300);
  u160::U160 info_hash_;
  u160::U160 self_;
  std::map<std::tuple<uint32_t, uint16_t>, std::shared_ptr<albert::bt::peer::PeerConnection>> peer_connections_;

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