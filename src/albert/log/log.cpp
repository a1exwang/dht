#include <albert/log/log.hpp>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

namespace logging = boost::log;


namespace albert::log {

boost::log::trivial::severity_level severity;
boost::log::trivial::severity_level get_severity() {
  return severity;
}

bool is_debug() {
  return severity == boost::log::trivial::debug;
}

void initialize_logger(bool debug) {
  severity = debug ? logging::trivial::debug : logging::trivial::info;
  logging::core::get()->set_filter(
      logging::trivial::severity >= severity
  );
  LOG(debug) << "Logger initialized, debug " << debug;
}


}
