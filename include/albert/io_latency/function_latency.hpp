#pragma once

#include <chrono>
#include <iomanip>
#include <string>

#include <albert/log/log.hpp>

namespace albert::io_latency {

class FunctionLatency {
 public:
  FunctionLatency(const std::string &name) :start_time_(std::chrono::high_resolution_clock::now()), name_(name) {}
  ~FunctionLatency() {
    LOG(info) <<  "FunctionLatency: '" << name_ << "': " << std::fixed << std::setprecision(2) << std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - start_time_).count() << "ms";
  }
 private:
  std::chrono::high_resolution_clock::time_point start_time_;
  std::string name_;
};
}

#define FLAT albert::io_latency::FunctionLatency _(__func__)
