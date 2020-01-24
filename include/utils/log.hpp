#pragma once

#include <boost/log/trivial.hpp>
#define LOG(x) BOOST_LOG_TRIVIAL(x)

namespace dht::log {

void initialize_logger(bool debug);

}