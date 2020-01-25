#pragma once

#include <cstdint>
#include <cstddef>

namespace bt {
namespace peer {

constexpr uint8_t MessageTypeChoke = 0;
constexpr uint8_t MessageTypeUnchoke = 1;
constexpr uint8_t MessageTypeInterested = 2;
constexpr uint8_t MessageTypeNotInterested = 3;
constexpr uint8_t MessageTypeExtended = 20;
constexpr uint8_t ExtendedMessageTypeRequest = 0;
constexpr uint8_t ExtendedMessageTypeData = 1;
constexpr uint8_t ExtendedMessageTypeReject = 2;

class Peer {
 public:
  Peer(uint32_t ip, uint16_t port) :ip_(ip), port_(port) {}

  void connect();

  uint32_t ip() const { return ip_; }
  uint16_t port() const { return port_;}

 private:
  uint32_t ip_;
  uint16_t port_;
};

}
}