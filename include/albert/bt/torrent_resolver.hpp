#pragma once
#include <list>
#include <albert/krpc/krpc.hpp>
#include <albert/bt/peer_connection.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}

namespace albert::bt {
class TorrentResolver {
 public:
  TorrentResolver(
      boost::asio::io_service &io,
      krpc::NodeID info_hash,
      krpc::NodeID self,
      uint32_t bind_ip,
      uint16_t bind_port);
  ~TorrentResolver();
  void add_peer(uint32_t ip, uint16_t port);

  [[nodiscard]]
  bool finished() const;

  [[nodiscard]]
  size_t pieces_got() const;

  [[nodiscard]]
  size_t data_got() const;

  [[nodiscard]]
  krpc::NodeID self() const;

  void set_torrent_handler(std::function<void(const bencoding::DictNode &torrent)> handler);
 private:
  void piece_handler(int piece, const std::vector<uint8_t> &data);
  void handshake_handler(int total_pieces, size_t metdata_size);

  [[nodiscard]]
  std::vector<uint8_t> merged_pieces() const;

 private:
  uint32_t bind_ip_;
  uint16_t bind_port_;
  std::vector<std::vector<uint8_t>> pieces_;
  size_t metadata_size_;

  std::function<void(const bencoding::DictNode &torrent)> torrent_handler_;

  boost::asio::io_service &io_;
  krpc::NodeID info_hash_;
  krpc::NodeID self_;
  std::list<std::shared_ptr<albert::bt::peer::PeerConnection>> peer_connections_;
};
}