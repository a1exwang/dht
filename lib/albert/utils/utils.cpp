#include <albert/utils/utils.hpp>

#include <cmath>
#include <cctype>

#include <sstream>
#include <iomanip>

namespace albert::dht::utils {

std::string hexdump(const void *ptr, size_t length, bool verbose) {
  std::stringstream ss;
  auto p = static_cast<const uint8_t *>(ptr);
  auto hex_digits = size_t(ceil(log(length) / log(16)));
  const size_t column_width = 16;
  std::stringstream line;
  size_t i = 0;
  for (size_t i = 0; i < length; i++) {
    if (verbose && i % column_width == 0) {
      std::stringstream ss_address;
      ss_address << std::hex << std::setfill('0') << std::setw(hex_digits) << i;
      ss << "0x" << ss_address.str() << ": ";
    }
    ss << std::hex << std::setfill('0') << std::setw(2) << (uint32_t)p[i];
    if (verbose) {
      ss << ' ';
    }

    if (verbose) {
      auto c = (char)p[i];
      if (std::isprint(c)) {
        line << c;
      } else {
        line << '.';
      }
      if (i % column_width == column_width-1) {
        ss << "| " << line.str();
        ss << std::endl;
        line.str("");
        line.clear();
      }
    }
  }
  if (i % column_width != column_width-1) {
    for (size_t k = 0; k < column_width-i; k++) {
      ss << ' ';
    }
    ss << "| ";
    ss << line.str();
    ss << std::endl;
  }
  return ss.str();
}
template<>
uint32_t host_to_network<uint32_t>(uint32_t input) {
  uint64_t rval;
  auto *data = (uint8_t *)&rval;
  data[0] = input >> 24U;
  data[1] = input >> 16U;
  data[2] = input >> 8U;
  data[3] = input >> 0U;
  return rval;
}
template<>
uint16_t host_to_network<uint16_t>(uint16_t input) {
  uint64_t rval;
  auto *data = (uint8_t *)&rval;
  data[0] = input >> 8U;
  data[1] = input >> 0U;
  return rval;
}
template<typename T>
T network_to_host(T input) {
  return host_to_network(input);
}


template uint32_t network_to_host(uint32_t input);
template uint16_t network_to_host(uint16_t input);

}