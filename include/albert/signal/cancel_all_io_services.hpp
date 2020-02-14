#pragma once

#include <initializer_list>
#include <vector>

#include <boost/asio/signal_set.hpp>
#include <boost/asio/io_service.hpp>

namespace albert::signal {
class CancelAllIOServices {
 public:
  CancelAllIOServices(boost::asio::io_service &io, std::vector<boost::asio::io_service*> args);

  // single io_service mode
  CancelAllIOServices(boost::asio::io_service &io);
 private:
  std::vector<boost::asio::io_service*> io_services_;
  boost::asio::signal_set signals_;
};
}