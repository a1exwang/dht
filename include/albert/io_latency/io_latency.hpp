#pragma once

#include <cstddef>

#include <string>
#include <chrono>

#include <boost/asio/io_service.hpp>

namespace albert::io_latency {

class IOLatencyMeter {
 public:
  IOLatencyMeter(boost::asio::io_service &io, bool debug) :io_(io), debug_(debug) { }
  void loop();

 private:
  boost::asio::io_service &io_;
  bool debug_ = false;
};

}