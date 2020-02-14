#include <albert/flow_control/rps_throttler.hpp>

#include <algorithm>
#include <numeric>
#include <iomanip>

#include <boost/asio/io_service.hpp>

#include <albert/log/log.hpp>
#include <albert/utils/utils.hpp>

namespace albert::flow_control {

RPSThrottler::RPSThrottler(boost::asio::io_service &io,
                           bool enabled,
                           double max_rps,
                           double leak_probability,
                           size_t max_queue_size,
                           size_t max_latency_ns,
                           size_t timer_interval_ns,
                           size_t wait_requests_at_a_time,
                           size_t max_complete_times
) :io_(io), timer_(io), enabled_(enabled), max_rps_(max_rps), leak_probability_(leak_probability),
   max_queue_size_(max_queue_size), max_latency_(max_latency_ns), timer_interval_(timer_interval_ns),
   wait_requests_at_a_time(wait_requests_at_a_time), max_complete_times_(max_complete_times) {

  if (enabled) {
    timer_.expires_at(boost::asio::chrono::high_resolution_clock::now() + timer_interval_);
    timer_.async_wait(std::bind(&RPSThrottler::timer_handler, this, std::placeholders::_1));
  }
}

double RPSThrottler::current_rps() const {
  if (fire_times_.size() > 2) {
    return fire_times_.size() /
        std::chrono::duration<double>(std::get<0>(fire_times_.back()) - std::get<0>(fire_times_.front())).count();
  } else {
    return 0;
  }
}
void RPSThrottler::throttle(std::function<void()> action) {
  if (!enabled_) {
    io_.post(std::move(action));
  } else if (full()) {
    if (roll_dice_leaky()) {
      request_queue_.pop_front();
      auto now = std::chrono::high_resolution_clock::now();
      request_queue_.emplace_back(std::move(action), now);
    }
    dropped_++;
  } else {
    auto now = std::chrono::high_resolution_clock::now();
    request_queue_.emplace_back(std::move(action), now);
  }
}

void RPSThrottler::timer_handler(const boost::system::error_code &e) {
  if (e) {
    LOG(error) << "RPSThrottler timer error";
    return;
  }

  auto now = boost::asio::chrono::high_resolution_clock::now();
//  if (max_latency_.count() != 0) {
//    while (!request_queue_.empty()) {
//      if (now - std::get<1>(request_queue_.front()) > max_latency_) {
//        deq();
//      } else {
//        break;
//      }
//    }
//  }

  if (!request_queue_.empty()) {
    if (fire_times_.size() <= 2) {
      deq();
      fire_times_.emplace_back(std::chrono::high_resolution_clock::now(), 1);
      timer_.expires_at(now + timer_interval_);
    } else {
      auto t0 = std::get<0>(fire_times_.front());
      auto delta_t = std::chrono::duration<double>(now - t0).count();
      double max_requests = max_rps_ * delta_t;
      size_t n = 0;
      size_t total_times = 0;
      for (auto [_, times] : fire_times_) {
        total_times += times;
      }
      while (total_times + n + 1 < max_requests && !request_queue_.empty()) {
        deq();
        n++;
      }

      LOG(debug) << "target rps not reached executing request "
                 << "size " << request_queue_.size() << " "
                 << "deltat " << delta_t << " "
                 << "tsize " << fire_times_.size() << " "
                 << "n " << n;

      if (n > 0) {
        fire_times_.emplace_back(std::chrono::high_resolution_clock::now(), n);
        while (fire_times_.size() > max_complete_times_) {
          fire_times_.pop_front();
        }
      }

      // max request rate reach, calculate next request time to match max_rps
      // assume next action finishes immediately after firing
      size_t us = (fire_times_.size() + wait_requests_at_a_time) / max_rps_ * 1e6;
      auto next_tp = std::get<0>(fire_times_.front()) + boost::asio::chrono::microseconds(us);
      timer_.expires_at(next_tp);
      LOG(debug) << "target rps reached next expiration: "
                 << boost::asio::chrono::duration<double>(next_tp-now).count()*1000 << "ms "
                 << "deltat " << delta_t << " "
                 << "size " << request_queue_.size() << " "
                 << "tsize " << fire_times_.size()
            ;
    }
  } else {
    LOG(debug) << "RPSThrottler: queue empty";
    timer_.expires_at(now + timer_interval_);
  }

  timer_.async_wait(std::bind(&RPSThrottler::timer_handler, this, std::placeholders::_1));
}

void RPSThrottler::deq() {
  std::chrono::nanoseconds ns = std::chrono::high_resolution_clock::now() - std::get<1>(request_queue_.front());
  last_latencies.push_back(ns);
  if (last_latencies.size() > max_queue_size_) {
    last_latencies.pop_front();
  }

  io_.post(std::move(std::get<0>(request_queue_.front())));
  request_queue_.pop_front();
}

std::string RPSThrottler::stat() {
  std::stringstream ss;
  auto [min, max] = std::minmax_element(last_latencies.begin(), last_latencies.end());
  auto sum = std::accumulate(
      last_latencies.begin(), last_latencies.end(),
      std::chrono::nanoseconds(0),
      [](const std::chrono::nanoseconds &lhs, const std::chrono::nanoseconds &rhs) { return lhs + rhs; });
  auto now = std::chrono::high_resolution_clock::now();
  size_t n = last_latencies.size();
  if (enabled_) {
    auto delta_t = std::chrono::duration<double>(now - last_stat_time_).count();
    ss << "RPSThrottler: min/max/avg/qsize/rps/droprate "
       << std::fixed << std::setprecision(2)
       << min->count()/1e6 << "ms/"
       << max->count()/1e6 << "ms/"
       << (n > 0 ? sum.count()/n : 0)/1e6 << "ms/"
       << request_queue_.size() << "/"
       << utils::pretty_size(current_rps(), false)  << "/"
       << utils::pretty_size(delta_t > 0 ? (dropped_ - last_dropped_) / delta_t : 0, false);
    last_stat_time_ = now;
    last_dropped_ = dropped_;
    return ss.str();
  } else {
    return "RPSThrottler: disabled";
  }
}
bool RPSThrottler::roll_dice_leaky() {
  return dist_(rng_) < leak_probability_;
}

}