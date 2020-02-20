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
      :
      store_(std::move(store)),
       db_scan_timer(db_service, boost::asio::chrono::seconds(db_scan_interval_seconds)),
       db_scan_interval(db_scan_interval_seconds),
       bt(bt_service, std::move(bt_config)),
       dht(std::move(dht_config), dht_service),
       rng_(std::random_device()()),
       signal_(signal_service, {&bt_service, &dht_service, &db_service}) {
  }

  void start() {
    dht_service.post([weak_scanner = weak_from_this()]() {
      if (auto scanner = weak_scanner.lock(); scanner) {
        scanner->dht.start();
      }
    });
    bt_service.post([weak_scanner = weak_from_this()]() {
      if (auto scanner = weak_scanner.lock(); scanner) {
        scanner->bt.start();
      }
    });
    db_scan_timer.async_wait([weak_scanner = weak_from_this()](const boost::system::error_code &error) {
      if (auto scanner = weak_scanner.lock(); scanner) {
        scanner->handle_db_scan_timer(error);
      }
    });

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
        if (empty_keys.empty()) {
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

    bt_service.post([weak_scanner = weak_from_this()]() {
      if (auto scanner = weak_scanner.lock(); scanner) {
        auto resolver_count = scanner->bt.resolver_count();
        auto peer_count = scanner->bt.peer_count();
        auto success_count = scanner->bt.success_count();
        auto failure_count = scanner->bt.failure_count();
        auto bt_memory_size = scanner->bt.memory_size();
        auto peers_stat = scanner->bt.peers_stat();

        scanner->db_service.post([weak_scanner, resolver_count, peer_count, success_count, failure_count, bt_memory_size, peers_stat]() {
          if (auto scanner = weak_scanner.lock(); scanner) {
            if (peer_count < scanner->max_concurrent_peers_) {
              std::vector<std::string> results;
              if (resolver_count < scanner->max_concurrent_resolutions_)  {
                auto n = scanner->max_concurrent_resolutions_ - resolver_count;
                for (size_t i = 0; i < n; i++) {
                  auto ih = scanner->db_get_info_hash();
                  if (ih.has_value()) {
                    results.push_back(ih.value());
                  } else {
                    break;
                  }
                }
              }
              for (size_t i = 0; i < results.size() && i < scanner->max_add_count_at_a_time; i++) {
                try {
                  scanner->resolve_s(albert::u160::U160::from_hex(results[i]));
                } catch (const std::runtime_error &e) {
                  LOG(error) << "Failed to resolve info hash: " << e.what();
                }
              }
            }

            LOG(info) << "Scanner: BTResolver count: " << resolver_count
                      << " success " << success_count
                      << " failure " << failure_count;

            auto [vsize, rss] = albert::utils::process_mem_usage();
            LOG(info) << "Memory stat: BT memsize " << albert::utils::pretty_size(bt_memory_size)
                      << " DHT memsize " << albert::utils::pretty_size(scanner->dht.memory_size())
                      << " DB memsize " << albert::utils::pretty_size(scanner->store_->memory_size())
                      << " VIRT " << albert::utils::pretty_size(vsize)
                      << " RES " << albert::utils::pretty_size(rss)
                      << " RES-BT " << albert::utils::pretty_size(rss - bt_memory_size)
                  ;
            for (auto [name, count] : peers_stat) {
              LOG(info) << "Peers '" << name << "': " << count;
            }
            LOG(info) << "Peers still have hope " << peer_count;
            LOG(info) << "Memory stat: PeerConnection instances " << albert::bt::peer::PeerConnection::counter;
          }
        });
      }
    });

    db_scan_timer.expires_at(db_scan_timer.expiry() + db_scan_interval);
    db_scan_timer.async_wait([weak_scanner = weak_from_this()](const boost::system::error_code &error) {
      if (auto scanner = weak_scanner.lock(); scanner) {
        scanner->handle_db_scan_timer(error);
      }
    });
  }

  void resolve_s(const albert::u160::U160 &ih) {
    bt_service.post([weak_scanner = weak_from_this(), ih]() {
      if (auto scanner = weak_scanner.lock(); scanner) {
        auto resolver_weak = scanner->bt.resolve_torrent(ih, [ih, weak_scanner](const albert::bencoding::DictNode &torrent) {
          if (auto scanner = weak_scanner.lock(); scanner) {
            scanner->db_service.post([ih, torrent, weak_scanner]() {
              if (auto scanner = weak_scanner.lock(); scanner) {
                auto file_name = "torrents/" + ih.to_string() + ".torrent";
                std::ofstream f(file_name, std::ios::binary);
                torrent.encode(f, albert::bencoding::EncodeMode::Bencoding);
                try {
                  scanner->store_->update(ih.to_string(), file_name);
                  LOG(info) << "torrent saved as '" << file_name << ", db updated";
                } catch (const albert::store::Sqlite3TimeoutError &e) {
                  auto backup_file = "failed_to_save_torrents.txt";
                  std::ofstream ofs(backup_file, std::ios::app);
                  ofs << ih.to_string() << " " << file_name << std::endl;
                  LOG(error) << "failed to save torrent to database, database too busy, saving to " << backup_file;
                }
              }
            });
          }
        });
        using namespace std::placeholders;
        scanner->dht_service.post([weak_scanner, ih, resolver_weak]() {
          if (auto scanner = weak_scanner.lock(); scanner) {
            scanner->dht.get_peers(ih, [weak_scanner, resolver_weak](uint32_t ip, uint16_t port) {
              if (auto scanner = weak_scanner.lock()) {
                scanner->handle_get_peers_s(resolver_weak, ip, port);
              }
            });
          }
        });
      }
    });
  }

  void handle_get_peers_s(std::weak_ptr<albert::bt::TorrentResolver> resolver_weak, uint32_t ip, uint16_t port) {
    bt_service.post([ip, port, resolver_weak, weak_scanner = weak_from_this()]() {
      auto resolver = resolver_weak.lock();
      auto scanner = weak_scanner.lock();
      if (!resolver || !scanner) {
        LOG(debug) << "TorrentResolver or Scanner gone before a get_peer request received";
      } else {
        if (scanner->bt.peer_count() < scanner->max_concurrent_peers_) {
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
  {
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
//    {
//      auto [virt, rss] = albert::utils::process_mem_usage();
//      LOG(info) << "1VIRT, RSS = " << albert::utils::pretty_size(virt) << " " << albert::utils::pretty_size(rss);
//    }
//    std::vector<std::unique_ptr<char>> ps;
//    for (size_t i = 0; i < 30; i++) {
//      size_t size = 1ul << i;
//      ps.push_back(std::unique_ptr<char>(new char[size]));
//      memset(ps.back().get(), 0, size);
//
//      {
//        auto [virt, rss] = albert::utils::process_mem_usage();
//        LOG(info) << "1VIRT, RSS = " << albert::utils::pretty_size(virt) << " " << albert::utils::pretty_size(rss);
//      }
//    }
//    {
//
//      auto [virt, rss] = albert::utils::process_mem_usage();
//      LOG(info) << "2VIRT, RSS = " << albert::utils::pretty_size(virt) << " " << albert::utils::pretty_size(rss);
//    }
  }
  auto [virt, rss] = albert::utils::process_mem_usage();
  LOG(info) << "When exiting VIRT, RSS = " << albert::utils::pretty_size(virt) << " " << albert::utils::pretty_size(rss);

  return 0;
}

