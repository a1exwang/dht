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
  TorrentResolver(boost::asio::io_service &io, krpc::NodeID info_hash, krpc::NodeID self);
  void add_peer(uint32_t ip, uint16_t port);

  [[nodiscard]] krpc::NodeID self() const;
 private:
  boost::asio::io_service &io_;
  krpc::NodeID info_hash_;
  krpc::NodeID self_;
  std::list<albert::bt::peer::PeerConnection> peer_connections_;
};
}