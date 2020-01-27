#include <albert/bt/bt.hpp>

#include <memory>

#include <albert/bt/torrent_resolver.hpp>
#include <albert/krpc/krpc.hpp>

namespace albert::bt {

BT::BT(boost::asio::io_service &io, krpc::NodeID self)
    :io_(io), self_(std::move(self)) { }

std::weak_ptr<TorrentResolver> albert::bt::BT::resolve_torrent(const krpc::NodeID &info_hash) {
  auto resolver = std::make_shared<TorrentResolver>(io_, info_hash, self_);
  if (resolvers_.find(info_hash) == resolvers_.end()) {
    resolvers_.emplace(info_hash, resolver);
    return resolver;
  } else {
    throw std::runtime_error("TorrentResolver: info hash already exists '" + info_hash.to_string() + "'");
  }
}

}
