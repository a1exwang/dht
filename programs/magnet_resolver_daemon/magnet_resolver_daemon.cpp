#include <exception>
#include <list>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <type_traits>
#include <thread>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind/bind.hpp>

#include <albert/bt/bt.hpp>
#include <albert/bt/config.hpp>
#include <albert/bt/torrent_resolver.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/store/sqlite3_store.hpp>
#include <albert/u160/u160.hpp>
#include <albert/utils/utils.hpp>
#include <albert/signal/cancel_all_io_services.hpp>


class Scanner :public std::enable_shared_from_this<Scanner> {
 public:
  Scanner(albert::bt::Config bt_config,
          albert::dht::Config dht_config,
          std::unique_ptr<albert::store::Store> store,
          size_t db_scan_interval_seconds)
      :store_(std::move(store)),
       db_scan_timer(db_service, boost::asio::chrono::seconds(db_scan_interval_seconds)),
       db_scan_interval(db_scan_interval_seconds),
       bt(bt_service, std::move(bt_config)),
       dht(std::move(dht_config), dht_service),
       rng_(std::random_device()()),
       signal_(signal_service, {&bt_service, &dht_service, &db_service}) {
  }

  void start() {
    dht_service.post([that = shared_from_this()]() {
      that->dht.start();
    });
    bt_service.post([that = shared_from_this()]() {
      that->bt.start();
    });
    db_scan_timer.async_wait(boost::bind(&Scanner::handle_db_scan_timer, shared_from_this(), boost::asio::placeholders::error()));

    thread_pool_.emplace_back(boost::bind(&boost::asio::io_context::run, &bt_service));
    thread_pool_.emplace_back(boost::bind(&boost::asio::io_context::run, &dht_service));
    thread_pool_.emplace_back(boost::bind(&boost::asio::io_context::run, &db_service));
    thread_pool_.emplace_back(boost::bind(&boost::asio::io_context::run, &signal_service));
  }

  void join() {
    for (auto &t : thread_pool_) {
      t.join();
    }
  }

  std::optional<std::string> db_get_info_hash() {
    if (cached_info_hashes_.empty()) {
      try {
        auto empty_keys = store_->get_empty_keys();
        if (empty_keys.size() == 0) {
          LOG(info) << "All torrents in database has been downloaded";
          return {};
        }
        std::sample(empty_keys.begin(), empty_keys.end(),
                    std::back_inserter<std::list<std::string>>(cached_info_hashes_),
                    100, rng_);
      } catch(const albert::store::Sqlite3TimeoutError &e) {
        LOG(warning) << "failed to read info_hash from database, too busy, should retry later";
        return {};
      }
    }
    auto ret = cached_info_hashes_.front();
    cached_info_hashes_.pop_front();
    return ret;
  }

  void handle_db_scan_timer(const boost::system::error_code &error) {
    if (error) {
      throw std::runtime_error("Scanner timer failure " + error.message());
    }

    bt_service.post([that = shared_from_this()]() {
      auto resolver_count = that->bt.resolver_count();
      auto peer_count = that->bt.peer_count();
      auto success_count = that->bt.success_count();
      auto failure_count = that->bt.failure_count();
      auto bt_memory_size = that->bt.memory_size();
      auto peers_stat = that->bt.peers_stat();

      that->db_service.post([that, resolver_count, peer_count, success_count, failure_count, bt_memory_size, peers_stat]() {
        if (peer_count < that->max_concurrent_peers_) {
          std::vector<std::string> results;
          if (resolver_count < that->max_concurrent_resolutions_)  {
            auto n = that->max_concurrent_resolutions_ - resolver_count;
            for (size_t i = 0; i < n; i++) {
              auto ih = that->db_get_info_hash();
              if (ih.has_value()) {
                results.push_back(ih.value());
              } else {
                break;
              }
            }
          }
          for (size_t i = 0; i < results.size() && i < that->max_add_count_at_a_time; i++) {
            try {
              that->resolve_s(albert::u160::U160::from_hex(results[i]));
            } catch (const std::runtime_error &e) {
              LOG(error) << "Failed to resolve info hash: " << e.what();
            }
          }
        }

        LOG(info) << "Scanner: BT resolver count: " << resolver_count
                  << " success " << success_count
                  << " failure " << failure_count
                  << " memsize " << albert::utils::pretty_size(bt_memory_size)
              ;
        for (auto [name, count] : peers_stat) {
          LOG(info) << "Peers '" << name << "': " << count;
        }
      });
    });

    db_scan_timer.expires_at(db_scan_timer.expiry() + db_scan_interval);
    db_scan_timer.async_wait(boost::bind(&Scanner::handle_db_scan_timer, shared_from_this(), boost::asio::placeholders::error()));
  }

  void resolve_s(const albert::u160::U160 &ih) {
    bt_service.post([that = shared_from_this(), ih]() {
      auto resolver_weak = that->bt.resolve_torrent(ih, [ih, that](const albert::bencoding::DictNode &torrent) {
        that->db_service.post([ih, torrent, that]() {
          auto file_name = "torrents/" + ih.to_string() + ".torrent";
          std::ofstream f(file_name, std::ios::binary);
          torrent.encode(f, albert::bencoding::EncodeMode::Bencoding);
          try {
            that->store_->update(ih.to_string(), file_name);
            LOG(info) << "torrent saved as '" << file_name << ", db updated";
          } catch (const albert::store::Sqlite3TimeoutError &e) {
            auto backup_file = "failed_to_save_torrents.txt";
            std::ofstream ofs(backup_file, std::ios::app);
            ofs << ih.to_string() << " " << file_name << std::endl;
            LOG(error) << "failed to save torrent to database, database too busy, saving to " << backup_file;
          }
        });
      });
      using namespace std::placeholders;
      that->dht_service.post([that, ih, resolver_weak]() {
          that->dht.get_peers(ih, std::bind(&Scanner::handle_get_peers_s, that, resolver_weak, _1, _2));
      });
    });
  }

  void handle_get_peers_s(std::weak_ptr<albert::bt::TorrentResolver> resolver_weak, uint32_t ip, uint16_t port) {
    bt_service.post([ip, port, resolver_weak, that = shared_from_this()]() {
      auto resolver = resolver_weak.lock();
      if (!resolver) {
        LOG(debug) << "TorrentResolver gone before a get_peer request received";
      } else {
        if (that->bt.peer_count() < that->max_concurrent_peers_) {
          resolver->add_peer(ip, port);
        }
      }
    });
  }

  void cancel(const albert::u160::U160 &ih) {
    // TODO
    LOG(error) << "Cancelling " << ih.to_string() << ", not implemented";
  }

 private:
  boost::asio::io_service db_service;
  boost::asio::io_service bt_service;
  boost::asio::io_service dht_service;
  boost::asio::io_service signal_service;
  std::unique_ptr<albert::store::Store> store_;

  boost::asio::steady_timer db_scan_timer;
  boost::asio::chrono::seconds db_scan_interval;
  albert::bt::BT bt;
  albert::dht::DHTInterface dht;
  std::mt19937_64 rng_;

  const size_t max_concurrent_resolutions_ = 15;
  const size_t max_concurrent_peers_ = 1000;
  const size_t max_add_count_at_a_time = 3;
  std::list<std::string> cached_info_hashes_;
  std::vector<std::thread> thread_pool_;
  albert::signal::CancelAllIOServices signal_;
};


int main(int argc, char* argv[]) {
  auto args = albert::config::argv2args(argc, argv);

  albert::dht::Config dht_config;
  albert::bt::Config bt_config;
  args = dht_config.from_command_line(args);
  args = bt_config.from_command_line(args);
  albert::config::throw_on_remaining_args(args);

  albert::log::initialize_logger(dht_config.debug);
  boost::asio::io_service io_service{};
  auto store = std::make_unique<albert::store::Sqlite3Store>("torrents/torrents.sqlite3");

  size_t db_scan_interval_seconds = 5;
  auto scanner = std::make_shared<Scanner>(std::move(bt_config), std::move(dht_config), std::move(store), db_scan_interval_seconds);
  scanner->start();
  scanner->join();
  return 0;
}

