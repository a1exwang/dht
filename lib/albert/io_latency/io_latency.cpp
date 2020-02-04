#include <albert/io_latency/io_latency.hpp>

#include <algorithm>
#include <numeric>
#include <iomanip>

#include <albert/log/log.hpp>


namespace albert::io_latency {

void IOLatencyMeter::loop() {
  size_t last_n = 10;
  std::vector<double> latencies;
  latencies.resize(last_n);

  size_t current = 0;
  size_t total = 0;
  auto last_time = std::chrono::high_resolution_clock::now();
  size_t last_total = 0;

  while (true) {
    auto t0 = std::chrono::high_resolution_clock::now();
    if (t0 - last_time > std::chrono::seconds(1)) {
      std::vector<double> l2(latencies.begin(), latencies.end());
      std::sort(l2.begin(), l2.end());
      auto [min, max] = std::array{l2.front(), l2.back()};
      auto average = std::accumulate(l2.begin(), l2.end(), 0.0) / last_n;
      auto median = l2[l2.size() / 2];
      LOG(info) << "Latency in last " << last_n << ": (min/max/avg/med) in ms = " << std::setprecision(2) << std::fixed
                << min << "/" << max << "/" << average << "/" << median;
      LOG(info) << "Counters in last " << last_n << ": (inc/total) = "
                << total - last_total << "/" << total;
      last_time = t0;
      last_total = total;
    }

    size_t n = io_.run_one();
    if (n > 0) {
      auto t1 = std::chrono::high_resolution_clock::now();
      double secs = std::chrono::duration<double, std::milli>(t1 - t0).count();

      if (debug_) {
        LOG(info) << "latency " << secs << "ms";
      }

      latencies[current] = secs;

      current++;
      current %= last_n;

      total++;
    } else {
      break;
    }
  }
}
}