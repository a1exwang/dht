#pragma once

#include <cstdint>
#include <cstddef>

#include <string>

namespace albert::bt::peer {

class Peer {
 public:
  Peer(uint32_t ip, uint16_t port) :ip_(ip), port_(port) {}

  uint32_t ip() const { return ip_; }
  uint16_t port() const { return port_;}

  std::string to_string() const;

 private:
  uint32_t ip_;
  uint16_t port_;
};

}
