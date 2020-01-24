#pragma once

#include <cstddef>

#include <string>

namespace dht::utils {
std::string hexdump(const void *ptr, size_t length, bool verbose);
}