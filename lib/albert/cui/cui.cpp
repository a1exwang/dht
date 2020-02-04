#include <albert/cui/cui.hpp>

#include <cstddef>
#include <cstdint>

#include <vector>
#include <string>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/bind/bind.hpp>
#include <boost/system/error_code.hpp>

#include <albert/bt/torrent_resolver.hpp>
#include <albert/dht/dht.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>

namespace albert::cui {

void CommandLineUI::handle_read_input(
    const boost::system::error_code &error,
    std::size_t bytes_transferred) {

  if (!error) {
    if (is_searching_) {
      input_buffer_.consume(input_buffer_.size());
      LOG(error) << "Already in search, ignored";
    } else {
      std::vector<char> data(input_buffer_.size());
      input_buffer_.sgetn(data.data(), data.size());
      input_buffer_.consume(input_buffer_.size());
      std::string command_line(data.data(), data.size());
      std::vector<std::string> cmd;
      boost::split(cmd, command_line, boost::is_any_of("\t "));

      if (cmd.size() == 2) {
        auto function_name = cmd[0];
        if (function_name == "ih") {
          target_info_hash_ = cmd[1];
        } else {
          if (target_info_hash_.empty()) {
            LOG(error) << "Unknown function name " << function_name;
          }
        }
      } else {
        if (target_info_hash_.empty()) {
          LOG(error) << "Invalid command size " << cmd.size();
        }
      }
      if (!target_info_hash_.empty()) {
        start_search();
      }
    }
  } else {
    input_buffer_.consume(input_buffer_.size());
    LOG(error) << "Failed to read from stdin " << error.message();
  }

  boost::asio::async_read_until(
      input_,
      input_buffer_,
      '\n',
      boost::bind(
          &CommandLineUI::handle_read_input,
          this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred)
  );

}

CommandLineUI::CommandLineUI(std::string info_hash, boost::asio::io_service &io, dht::DHTInterface &dht, bt::BT &bt)
    :io_(io), dht_(dht), bt_(bt), input_(io, ::dup(STDIN_FILENO)), target_info_hash_(info_hash) {

}
void CommandLineUI::start() {
  // set stdin read handler
  boost::asio::async_read_until(
      input_,
      input_buffer_,
      '\n',
      boost::bind(
          &CommandLineUI::handle_read_input,
          this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred)
  );
}
void CommandLineUI::start_search() {
  this->is_searching_ = true;
  auto ih = u160::U160::from_hex(target_info_hash_);

  auto resolver = bt_.resolve_torrent(ih, [this, ih](const bencoding::DictNode &torrent) {
    is_searching_ = false;
    auto file_name = ih.to_string() + ".torrent";
    std::ofstream f(file_name, std::ios::binary);
    torrent.encode(f, bencoding::EncodeMode::Bencoding);
    LOG(info) << "torrent saved as '" << file_name;
  });

  dht_.get_peers(ih, [this, ih, resolver](uint32_t ip, uint16_t port) {
    if (resolver.expired()) {
      LOG(error) << "TorrentResolver gone before a get_peer request received";
    } else {
      resolver.lock()->add_peer(ip, port);
    }
  });
}

}
