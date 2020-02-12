#include <albert/bt/bt.hpp>

#include <memory>

#include <boost/bind/bind.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/steady_timer.hpp>

#include <albert/bt/torrent_resolver.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>

namespace albert::bt {

BT::BT(boost::asio::io_service &io, Config config)
    : config_(std::move(config)), io_(io), self_(u160::U160::from_hex(config_.id)), gc_timer_(io),
      expiration_time_(config_.resolve_torrent_expiration_seconds) { }

std::weak_ptr<TorrentResolver> albert::bt::BT::resolve_torrent(
    const u160::U160 &info_hash,
    std::function<void(const bencoding::DictNode &)> handler) {
  if (resolvers_.find(info_hash) == resolvers_.end()) {
    auto bind_ip = boost::asio::ip::address_v4::from_string(config_.bind_ip).to_uint();
    auto resolver = std::make_shared<TorrentResolver>(io_, info_hash, self_, bind_ip, config_.bind_port, config_.use_utp,
        std::chrono::high_resolution_clock::now() + expiration_time_);
    resolver->set_torrent_handler([h{std::move(handler)}, info_hash, this](const bencoding::DictNode &torrent) {
      h(torrent);
      LOG(info) << "Torrent finished, deleting resolver";
      resolvers_.erase(info_hash);
      success_count_++;
    });
    resolvers_.emplace(info_hash, resolver);
    return resolver;
  } else {
    throw std::runtime_error("TorrentResolver: info hash already exists '" + info_hash.to_string() + "'");
  }
}

void BT::reset_gc_timer() {
  gc_timer_.expires_at(boost::asio::chrono::steady_clock::now() + boost::asio::chrono::seconds(2));
  gc_timer_.async_wait(boost::bind(&BT::handle_gc_timer, this, boost::asio::placeholders::error()));
}

void BT::start() {
  gc_timer_.async_wait(boost::bind(&BT::handle_gc_timer, this, boost::asio::placeholders::error()));
}
void BT::handle_gc_timer(const boost::system::error_code &error) {
  if (error) {
    throw std::runtime_error("BT gc timer failed " + error.message());
  }

  std::list<u160::U160> to_delete;
  for (auto &resolver : resolvers_) {
    if (resolver.second->timeout()) {
      to_delete.push_back(resolver.first);
    }
  }
  for (auto &id : to_delete) {
    resolvers_.erase(id);
    LOG(info) << "BT::gc Deleted timeout resolution: " << id.to_string();
    // delete 1 at a time
    break;
  }
  failed_count_ += to_delete.size();

  reset_gc_timer();
}

size_t BT::connected_peers() const {
  size_t n = 0;
  for (auto &item : resolvers_) {
    n += item.second->connected_peers();
  }
  return n;
}
size_t BT::peer_count() const {
  size_t n = 0;
  for (auto &item : resolvers_) {
    n += item.second->peer_count();
  }
  return n;
}

}
