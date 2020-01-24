#include "utils/log.hpp"

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

namespace logging = boost::log;


namespace dht::log {

void initialize_logger(bool debug) {
  auto severity = debug ? logging::trivial::debug : logging::trivial::info;
  logging::core::get()->set_filter(
      logging::trivial::severity >= severity
  );
  LOG(debug) << "Logger initialized, debug " << debug;
}

}
