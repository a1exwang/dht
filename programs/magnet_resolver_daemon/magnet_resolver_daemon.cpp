#include <exception>
#include <list>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind/bind.hpp>

#include <albert/bt/bt.hpp>
#include <albert/bt/torrent_resolver.hpp>
#include <albert/cui/cui.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/dht.hpp>
#include <albert/log/log.hpp>
#include <albert/store/sqlite3_store.hpp>
#include <albert/bt/config.hpp>

class Scanner :public std::enable_shared_from_this<Scanner> {
 public:
  Scanner(boost::asio::io_service &io,
          albert::bt::BT &bt,
          albert::dht::DHTInterface &dht,
          std::unique_ptr<albert::store::Store> store,
          size_t db_scan_interval_seconds)
      :db_scan_timer(io, boost::asio::chrono::seconds(db_scan_interval_seconds)),
       db_scan_interval(db_scan_interval_seconds),
       bt(bt),
       dht(dht),
       store_(std::move(store)),
       rng_(std::random_device()()){
  }

  void start() {
    db_scan_timer.async_wait(boost::bind(&Scanner::handle_timer, shared_from_this(), boost::asio::placeholders::error()));
  }

  void handle_timer(const boost::system::error_code &error) {
    if (error) {
      throw std::runtime_error("Scanner timer failure " + error.message());
    }

    if (bt.resolver_count() < max_concurrent_resolutions_) {
      LOG(info) << "Scanner: BT resolver count: " << bt.resolver_count();
      auto result = store_->get_empty_keys();
      auto you_are_the_chosen_one = rng_() % result.size();
      auto hero = result[you_are_the_chosen_one];
      auto super_hero = albert::krpc::NodeID::from_hex(hero);
      try {
        resolve(super_hero);
      } catch (const std::runtime_error &e) {
        LOG(error) << "Failed to resolve info hash: " << e.what();
      }
    }

    db_scan_timer.expires_at(db_scan_timer.expiry() + db_scan_interval);
    db_scan_timer.async_wait(boost::bind(&Scanner::handle_timer,shared_from_this(), boost::asio::placeholders::error()));
  }

  void resolve(const albert::krpc::NodeID &ih) {
    auto resolver = bt.resolve_torrent(ih, [ih, that = shared_from_this()](const albert::bencoding::DictNode &torrent) {
      auto file_name = "torrents/" + ih.to_string() + ".torrent";
      std::ofstream f(file_name, std::ios::binary);
      torrent.encode(f, albert::bencoding::EncodeMode::Bencoding);
      that->store_->update(ih.to_string(), file_name);
      LOG(info) << "torrent saved as '" << file_name << ", db updated";
    });

    dht.get_peers(ih, [ih, resolver](uint32_t ip, uint16_t port) {
      if (resolver.expired()) {
        LOG(debug) << "TorrentResolver gone before a get_peer request received";
      } else {
        resolver.lock()->add_peer(ip, port);
      }
    });
  };

  void cancel(const albert::krpc::NodeID &ih) {
    // TODO
    LOG(error) << "Cancelling " << ih.to_string() << ", not implemented";
  }

 private:
  std::unique_ptr<albert::store::Store> store_;

  boost::asio::steady_timer db_scan_timer;
  boost::asio::chrono::seconds db_scan_interval;
  albert::bt::BT &bt;
  albert::dht::DHTInterface &dht;
  std::mt19937_64 rng_;

  size_t max_concurrent_resolutions_ = 16;
};


int main(int argc, char* argv[]) {
  auto args = albert::config::argv2args(argc, argv);

  albert::dht::Config config;
  albert::bt::Config bt_config;
  args = config.from_command_line(args);
  args = bt_config.from_command_line(args);
  albert::config::throw_on_remaining_args(args);

  albert::log::initialize_logger(config.debug);
  boost::asio::io_service io_service{};
  albert::dht::DHTInterface dht(std::move(config), io_service);
  albert::bt::BT bt(io_service, std::move(bt_config));
  auto store = std::make_unique<albert::store::Sqlite3Store>("torrents/torrents.sqlite3");

  bt.start();
  dht.start();

  size_t db_scan_interval_seconds = 10;
  auto scanner = std::make_shared<Scanner>(io_service, bt, dht, std::move(store), db_scan_interval_seconds);
  scanner->start();

#ifdef NDEBUG
  try {
#endif
  io_service.run();
#ifdef NDEBUG
  } catch (const std::exception &e) {
    LOG(error) << "io_service Failure: \"" << e.what() << '"';
    exit(1);
  }
#endif

  return 0;
}

