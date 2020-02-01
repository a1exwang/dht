#include <albert/bt/bt.hpp>

#include <memory>

#include <albert/bt/torrent_resolver.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>

namespace albert::bt {

BT::BT(boost::asio::io_service &io, krpc::NodeID self, uint32_t bind_ip, uint16_t bind_port)
    :io_(io), self_(std::move(self)), bind_ip_(bind_ip), bind_port_(bind_port) { }

std::weak_ptr<TorrentResolver> albert::bt::BT::resolve_torrent(
    const krpc::NodeID &info_hash,
    std::function<void(const bencoding::DictNode &)> handler) {
  if (resolvers_.find(info_hash) == resolvers_.end()) {
    auto resolver = std::make_shared<TorrentResolver>(io_, info_hash, self_, bind_ip_, bind_port_);
    resolver->set_torrent_handler([h{std::move(handler)}, info_hash, this](const bencoding::DictNode &torrent) {
      h(torrent);
      LOG(info) << "Torrent finished, deleting resolver";
      resolvers_.erase(info_hash);
    });
    resolvers_.emplace(info_hash, resolver);
    return resolver;
  } else {
    throw std::runtime_error("TorrentResolver: info hash already exists '" + info_hash.to_string() + "'");
  }
}

}
