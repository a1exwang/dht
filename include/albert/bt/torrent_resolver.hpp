#pragma once
#include <list>

#include <albert/krpc/krpc.hpp>
#include <albert/bt/peer_connection.hpp>
#include <albert/u160/u160.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}

namespace albert::bt {
class TorrentResolver {
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
  size_t connected_peers() const {
    size_t ret = 0;
    for (auto &item : peer_connections_) {
      if (item->status() == peer::ConnectionStatus::Connected) {
        ret++;
      }
    }
    return ret;
  }

  [[nodiscard]]
  bool timeout() const {
    return std::chrono::high_resolution_clock::now() > expiration_at_;
  }

  void set_torrent_handler(std::function<void(const bencoding::DictNode &torrent)> handler);
 private:
  void piece_handler(std::weak_ptr<peer::PeerConnection> pc, int piece, const std::vector<uint8_t> &data);
  void handshake_handler(std::weak_ptr<peer::PeerConnection> pc, int total_pieces, size_t metdata_size);

  [[nodiscard]]
  std::vector<uint8_t> merged_pieces() const;

 private:
  uint32_t bind_ip_;
  uint16_t bind_port_;
  bool use_utp_;
  std::vector<std::vector<uint8_t>> pieces_;
  size_t metadata_size_;

  std::function<void(const bencoding::DictNode &torrent)> torrent_handler_;

  boost::asio::io_service &io_;
  u160::U160 info_hash_;
  u160::U160 self_;
  std::list<std::shared_ptr<albert::bt::peer::PeerConnection>> peer_connections_;

  bencoding::DictNode info_;
  bool has_metadata_;
  size_t total_size_;

  std::chrono::high_resolution_clock::time_point expiration_at_;
};
}