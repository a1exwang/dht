#pragma once

#include <cstdint>
#include <cstddef>

#include <chrono>
#include <functional>
#include <list>

#include <boost/asio/high_resolution_timer.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}

namespace albert::flow_control {

class RPSThrottler {
 public:
  RPSThrottler(boost::asio::io_service &io, double max_rps);

  void throttle(std::function<void()> action);
  void done(size_t n = 1);

  double current_rps() const;

  void timer_handler(const boost::system::error_code &e);

  void deq();

  void stat();

 private:
  boost::asio::io_service &io_;
  boost::asio::high_resolution_timer timer_;
  boost::asio::chrono::nanoseconds timer_interval_ = boost::asio::chrono::nanoseconds(1000 * 1000);
  boost::asio::chrono::nanoseconds max_latency_ = boost::asio::chrono::milliseconds(200);
  double max_rps_;
  size_t max_complete_times_ = 100;
  size_t max_queue_size_ = 100;
  size_t wait_requests_at_a_time = 10;
  std::list<std::chrono::high_resolution_clock::time_point> complete_times_;
  std::list<std::tuple<std::function<void()>, std::chrono::high_resolution_clock::time_point>> request_queue_;
  std::list<std::chrono::nanoseconds> last_latencies;
};

}