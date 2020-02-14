#pragma once

#include <cstdint>
#include <cstddef>

#include <chrono>
#include <functional>
#include <list>
#include <random>

#include <boost/asio/high_resolution_timer.hpp>

namespace boost::asio {
class io_context;
typedef io_context io_service;
}

namespace albert::flow_control {

class RPSThrottler {
 public:
  RPSThrottler(boost::asio::io_service &io, bool enable, double max_rps, double leak_probability,
      size_t max_queue_size = 100,
      size_t max_latency_ns = 100ul * 1000ul* 1000ul,
      size_t timer_interval_ns = 10ul * 1000ul*1000ul,
      size_t wait_requests_at_a_time = 10,
      size_t max_complete_times = 100);

  void throttle(std::function<void()> action);
  bool full() const { return request_queue_.size() >= max_queue_size_; }
  double current_rps() const;
  bool enabled() const { return enabled_; }

  std::string stat();
 private:
  bool roll_dice_leaky();

  void deq();
  void timer_handler(const boost::system::error_code &e);

 private:
  boost::asio::io_service &io_;
  boost::asio::high_resolution_timer timer_;
  bool enabled_;
  double max_rps_;
  double leak_probability_;
  size_t max_queue_size_;
  boost::asio::chrono::nanoseconds max_latency_;
  boost::asio::chrono::nanoseconds timer_interval_;
  size_t wait_requests_at_a_time;
  size_t max_complete_times_;
  size_t dropped_ = 0;
  size_t last_dropped_ = 0;
  std::chrono::high_resolution_clock::time_point last_stat_time_;

  std::list<std::tuple<std::chrono::high_resolution_clock::time_point, size_t>> fire_times_;
  std::list<std::tuple<std::function<void()>, std::chrono::high_resolution_clock::time_point>> request_queue_;
  std::list<std::chrono::nanoseconds> last_latencies;

  std::random_device rng_;
  std::uniform_real_distribution<double> dist_ = std::uniform_real_distribution<double>(0, 1);
};

}