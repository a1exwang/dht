#pragma once

#include <boost/log/trivial.hpp>
#define LOG(x) BOOST_LOG_TRIVIAL(x)

namespace albert::log {

void initialize_logger(bool debug);
boost::log::trivial::severity_level get_severity();
bool is_debug();

}