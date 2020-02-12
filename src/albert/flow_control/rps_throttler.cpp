#include <albert/flow_control/rps_throttler.hpp>

#include <algorithm>
#include <numeric>
#include <iomanip>

#include <boost/asio/io_service.hpp>

#include <albert/log/log.hpp>

namespace albert::flow_control {

RPSThrottler::RPSThrottler(boost::asio::io_service &io, double max_rps) :io_(io), timer_(io), max_rps_(max_rps) {
  timer_.expires_at(boost::asio::chrono::high_resolution_clock::now() + timer_interval_);
  timer_.async_wait(std::bind(&RPSThrottler::timer_handler, this, std::placeholders::_1));
}

void RPSThrottler::done(size_t n) {
  auto now = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < n; i++) {
    complete_times_.push_back(now);
    if (complete_times_.size() > max_complete_times_) {
      complete_times_.pop_front();
    }
  }
}

double RPSThrottler::current_rps() const {
  if (complete_times_.size() > 2) {
    return complete_times_.size() /
        std::chrono::duration<double>(complete_times_.back() - complete_times_.front()).count();
  } else {
    return 0;
  }
}
void RPSThrottler::throttle(std::function<void()> action) {
  request_queue_.emplace_back(std::move(action), std::chrono::high_resolution_clock::now());
  if (request_queue_.size() > max_queue_size_) {
    throw std::overflow_error("RPSThrottler, Max queue size reached");
  }
}

void RPSThrottler::timer_handler(const boost::system::error_code &e) {
  if (e) {
    LOG(error) << "RPSThrottler timer error";
    return;
  }

  auto now = boost::asio::chrono::high_resolution_clock::now();


  if (max_latency_.count() != 0) {
    while (true) {
      if (now - std::get<1>(request_queue_.front()) > max_latency_) {
        deq();
      } else {
        break;
      }
    }
  }

  if (complete_times_.size() >= 2) {
    auto delta_t = std::chrono::duration<double>(now - complete_times_.front()).count();
    double max_requests = max_rps_ * delta_t;
    if (complete_times_.size() < max_requests) {
      // max requests rate not reached
      size_t remaining = max_requests - complete_times_.size();
      for (size_t i = 0; i < remaining && !request_queue_.empty(); i++) {
        deq();
      }
      timer_.expires_at(now + timer_interval_);
    } else {
      // max request rate reach, calculate next request time to match max_rps
      // assume next action finishes immediately after firing
      size_t us = (complete_times_.size() + wait_requests_at_a_time) / max_rps_ * 1e6;
      auto next_tp = complete_times_.front() + boost::asio::chrono::microseconds(us);
      timer_.expires_at(next_tp);
      LOG(debug) << "target rps reached next expiration: "
                 << boost::asio::chrono::duration<double>(next_tp-now).count()*1000 << "ms "
                 << "last t " << delta_t << " "
                 << "size " << request_queue_.size() << " "
            ;
    }
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

void RPSThrottler::stat() {
  auto [min, max] = std::minmax_element(last_latencies.begin(), last_latencies.end());
  auto sum = std::reduce(last_latencies.begin(), last_latencies.end());
  size_t n = last_latencies.size();
  LOG(info) << "RPSThrottler: min/max/avg/qsize "
            << std::fixed << std::setprecision(2)
            << min->count()/1e6 << "ms/"
            << max->count()/1e6 << "ms/"
            << (n > 0 ? sum.count()/n : 0)/1e6 << "ms/"
            << request_queue_.size();
}

}