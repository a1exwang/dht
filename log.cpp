#include "log.hpp"

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

namespace logging = boost::log;


namespace dht::log {

void initialize_logger() {
  logging::core::get()->set_filter(
      logging::trivial::severity >= logging::trivial::debug
  );
  LOG(debug) << "Logger initialized";
}

}
