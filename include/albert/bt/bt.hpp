#pragma once
#include <memory>

#include <boost/asio/steady_timer.hpp>

#include <albert/krpc/krpc.hpp>
#include <albert/bt/config.hpp>
#include <albert/u160/u160.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}

namespace albert::bt {
class TorrentResolver;

class BT {
 public:
  BT(boost::asio::io_service &io, Config config);
  std::weak_ptr<TorrentResolver> resolve_torrent(const u160::U160 &info_hash, std::function<void(const bencoding::DictNode &)> handler);
  std::weak_ptr<TorrentResolver> file_transfer(const u160::U160 &info_hash, const bencoding::DictNode &info);
  void start();

  size_t resolver_count() const { return resolvers_.size(); }
  size_t success_count() const { return success_count_; }
  size_t failure_count() const { return failed_count_; }
  size_t connected_peers() const;

 private:
  void handle_gc_timer(const boost::system::error_code &error);
  void reset_gc_timer();
 private:
  Config config_;
  boost::asio::io_service &io_;
  u160::U160 self_;
  std::map<albert::u160::U160, std::shared_ptr<TorrentResolver>> resolvers_;

  boost::asio::steady_timer gc_timer_;
  std::chrono::seconds expiration_time_ = std::chrono::seconds(30);

  size_t success_count_ = 0;
  size_t failed_count_ = 0;
};

}