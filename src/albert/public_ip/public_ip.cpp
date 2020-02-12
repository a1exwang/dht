#include <albert/public_ip/public_ip.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>


namespace albert::public_ip {

static std::string exec(const char* cmd) {
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  std::stringstream ss;
  int c = EOF;
  while (c = fgetc(pipe.get()), c != EOF) {
    ss.put(c);
  }
  return ss.str();
}

uint32_t my_v4() {
  auto cmd_line = "curl -L https://api.ipify.org/";
  std::string part;
  auto stdout = exec(cmd_line);
  std::stringstream ss(stdout);
  uint32_t ip = 0;

  for (size_t i = 0; i < sizeof(ip); i++) {
    uint32_t tmp = 0;
    std::stringstream ss2;
    if (std::getline(ss, part, '.')) {
      ss2 << part;
      ss2 >> tmp;
      ip |= tmp << 8*(3-i);
    } else {
      throw std::runtime_error("Failed to get my public IP v4 address, stdout: " + stdout);
    }
  }

  return ip;
}

}