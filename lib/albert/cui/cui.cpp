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

namespace albert::cui {

void CommandLineUI::handle_read_input(
    const boost::system::error_code &error,
    std::size_t bytes_transferred) {

  if (!error) {
    auto ih = krpc::NodeID::from_hex(target_info_hash_);
    auto resolver = bt_.resolve_torrent(ih);
    dht_.get_peers(ih, [this, ih, resolver](uint32_t ip, uint16_t port) {
      if (resolver.expired()) {
        LOG(error) << "TorrentResolver gone before a get_peer request received";
      } else {
        resolver.lock()->add_peer(ip, port);
      }
      LOG(info) << "CUI::get_peers(" << ih.to_string() << ") = " << boost::asio::ip::address_v4(ip) << ":" << port;
    });

    std::vector<char> data(input_buffer_.size());
    input_buffer_.sgetn(data.data(), data.size());
    input_buffer_.consume(input_buffer_.size());
    input_ss_.write(data.data(), data.size());

    std::string command_line = input_ss_.str();
    std::vector<std::string> cmd;
    boost::split(cmd, command_line, boost::is_any_of("\t "));

    if (cmd.size() == 2) {
      auto function_name = cmd[0];
      if (function_name == "get-peers") {
//        auto info_hash = cmd[1];
//        dht_->get_peers(krpc::NodeID::from_hex(info_hash));
      } else {
        LOG(error) << "Unknown function name " << function_name;
      }
    } else {
      LOG(error) << "Invalid command size " << cmd.size();
    }

  } else if (error == boost::asio::error::not_found) {
    std::vector<char> data(input_buffer_.size());
    input_buffer_.sgetn(data.data(), data.size());
    input_buffer_.consume(input_buffer_.size());
    input_ss_.write(data.data(), data.size());
    /* ignore if new line not reached */
  } else {
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

}
