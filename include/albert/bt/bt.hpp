#pragma once
#include <memory>

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
  void start() {}
 private:
  uint32_t bind_ip_;
  uint16_t bind_port_;
  boost::asio::io_service &io_;
  krpc::NodeID self_;
  std::map<albert::krpc::NodeID, std::shared_ptr<TorrentResolver>> resolvers_;
};

}