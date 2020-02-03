#pragma once
#include <memory>

#include <boost/asio/steady_timer.hpp>

#include <albert/krpc/krpc.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}

namespace albert::bt {
class TorrentResolver;

class BT {
 public:
  BT(boost::asio::io_service &io, krpc::NodeID self, uint32_t bind_ip, uint16_t bind_port);
  std::weak_ptr<TorrentResolver> resolve_torrent(const krpc::NodeID &info_hash, std::function<void(const bencoding::DictNode &)> handler);
  void start();

  size_t resolver_count() const { return resolvers_.size(); }

 private:
  void handle_gc_timer(const boost::system::error_code &error);
  void reset_gc_timer();
 private:
  uint32_t bind_ip_;
  uint16_t bind_port_;
  boost::asio::io_service &io_;
  krpc::NodeID self_;
  std::map<albert::krpc::NodeID, std::shared_ptr<TorrentResolver>> resolvers_;

  boost::asio::steady_timer gc_timer_;
  std::chrono::seconds expiration_time_ = std::chrono::seconds(30);
};

}