#include <albert/bt/torrent_resolver.hpp>

namespace albert::bt {

void TorrentResolver::add_peer(uint32_t ip, uint16_t port) {
  peer_connections_.emplace_back(
      io_,
      self(),
      info_hash_,
      ip,
      port);
  peer_connections_.back().connect();
}

krpc::NodeID TorrentResolver::self() const {
  return self_;
}
TorrentResolver::TorrentResolver(
    boost::asio::io_service &io, krpc::NodeID info_hash, krpc::NodeID self)
    :io_(io), info_hash_(info_hash), self_(self) { }

}