#include <albert/utils/utils.hpp>

#include <cmath>
#include <cctype>

#include <sstream>
#include <iomanip>

namespace albert::utils {

/**
 * NOTE:
 * This is a workaround for glibc 2.29 static link symbol. log@GLIBC_2.29.
 */
int fastlog2(size_t length) {
  if (length == 0) {
    throw std::invalid_argument("log(x) x cannot be zero");
  }
  int ret = 0;
  while (length != 0) {
    length >>= 1;
    ret++;
  }
  return ret;
}

std::string hexdump(const void *ptr, size_t length, bool verbose) {
  std::stringstream ss;
  auto p = static_cast<const uint8_t *>(ptr);
  size_t hex_digits;
  if (length == 0) {
    hex_digits = 1;
  } else {
    hex_digits = size_t(ceil((double)fastlog2(length*8+1) / 4));
  }
  const size_t column_width = 16;
  std::stringstream line;
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
  if (verbose && length > 0 && length%column_width != 0) {
    for (size_t k = 0; k < column_width-(length%column_width); k++) {
      ss << "   ";
    }
    ss << "| ";
    ss << line.str();
    ss << std::endl;
  }
  return ss.str();
}

std::string hexload(const std::string &hex_string) {
  std::stringstream ss;
  if (hex_string.size() % 2 != 0) {
    throw std::invalid_argument("hex string length cannot be divided by 2, '" + hex_string + "'");
  }

  for (int i = 0; i < hex_string.size()/2; i++) {
    if (!(std::isxdigit(hex_string[2*i]) && std::isxdigit(hex_string[2*i+1]))) {
      throw std::invalid_argument("hex string is not hex number: '" + hex_string + "'");
    }
    uint8_t value = std::stoi(hex_string.substr(2*i, 2), nullptr, 16);
    ss << static_cast<char>(value);
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
template <>
uint8_t host_to_network<uint8_t>(uint8_t input) {
  return input;
}

template<typename T>
T network_to_host(T input) {
  return host_to_network(input);
}
std::string pretty_size(size_t size) {
  std::stringstream ss;
  static const char *SIZES[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  int div = 0;
  size_t rem = 0;

  while (size >= 1024 && div < (sizeof(SIZES) / sizeof(SIZES[0]))) {
    rem = (size % 1024);
    div++;
    size /= 1024;
  }

  double size_d = (float)size + (float)rem / 1024.0;
  ss << std::fixed << std::setprecision(2) << size_d << SIZES[div];
  return ss.str();
}

std::string hexdump(const std::string &data, bool verbose) {
  return hexdump(data.data(), data.size(), verbose);
}

template uint32_t network_to_host(uint32_t input);
template uint16_t network_to_host(uint16_t input);
template uint8_t network_to_host(uint8_t input);

}