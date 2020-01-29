#pragma once
#include <cstddef>
#include <sstream>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>


namespace boost {
namespace asio {
class io_context;
typedef io_context io_service;
}
namespace system {
class error_code;
}
}

namespace albert::bt {
class BT;
}

namespace albert::dht {
class DHTInterface;
}

namespace albert::cui {

class CommandLineUI {
 public:
  CommandLineUI(std::string info_hash, boost::asio::io_service &io, dht::DHTInterface &dht, bt::BT &bt);
  void handle_read_input(
      const boost::system::error_code &error,
      std::size_t bytes_transferred);

  void start();
  void start_search();

 private:
  boost::asio::io_service &io_;
  dht::DHTInterface &dht_;
  bt::BT &bt_;

  boost::asio::posix::stream_descriptor input_;
  boost::asio::streambuf input_buffer_;

  /**
   * The info hash of target to search.
   * If empty read from stdin
   */
  std::string target_info_hash_;
  bool is_searching_ = false;
};

}


