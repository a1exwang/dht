#pragma once

#include <cstdint>

#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
#include <list>

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/udp.hpp>

namespace albert::utp {

class ConnectionError :public std::runtime_error {
 public:
  using runtime_error::runtime_error;
};
class InvalidHeader :public ConnectionError {
 public:
  using ConnectionError::ConnectionError;
};
class InvalidStatus :public ConnectionError {
 public:
  using ConnectionError::ConnectionError;
};
class SystemError :public ConnectionError {
 public:
  using ConnectionError::ConnectionError;
};

constexpr uint8_t UTPVersion = 1;

constexpr uint8_t UTPTypeData = 0;
constexpr uint8_t UTPTypeFin = 1;
constexpr uint8_t UTPTypeState = 2;
constexpr uint8_t UTPTypeReset = 3;
constexpr uint8_t UTPTypeSyn = 4;

struct Packet {
  uint8_t type;
  uint8_t version;
  uint16_t connection_id;
  uint32_t timestamp_microseconds;
  uint32_t timestamp_difference_microseconds;
  uint32_t wnd_size;
  uint16_t seq_nr;
  uint16_t ack_nr;

  std::vector<std::tuple<uint8_t, std::string>> extensions;

  std::vector<uint8_t> data;

  static Packet decode(std::istream &is);
  std::ostream &encode(std::ostream &os) const;
  std::ostream &pretty(std::ostream &os) const;
  std::string pretty() const;
};

enum class Status {
  SynSent,
  Connecting,
  Connected,
  Closed,
  Error
};

struct Connection {

  void reset_timeout_from_now() {
    timeout_at = std::chrono::high_resolution_clock::now() + std::chrono::seconds(1);
  }


  uint16_t seq_nr = 1;
  uint16_t ack_nr = 0;
  uint16_t conn_id_send = 0;
  uint16_t conn_id_recv = 0;
  Status status_ = Status::SynSent;

  uint16_t acked = 0;

  boost::asio::ip::udp::endpoint ep;

  std::function<void(const boost::system::error_code &error)> connect_handler;

  std::function<void(const boost::system::error_code &, size_t)> receive_handler;
  boost::asio::mutable_buffer user_buffer;
  size_t user_buffer_data_size = 0;
  std::list<std::vector<uint8_t>> buffered_received_packets;

  std::chrono::high_resolution_clock::time_point timeout_at;
};

enum class MainSocketStatus {
  Initialized,
  Accepted,
  Connected
};

class Socket :public std::enable_shared_from_this<Socket> {
 public:
  Socket(boost::asio::io_service &io,
         boost::asio::ip::udp::endpoint bind_ep);
  ~Socket();

  void async_connect(
      boost::asio::ip::udp::endpoint ep,
      std::function<void(const boost::system::error_code &error)> handler);

  void async_receive(
      boost::asio::mutable_buffer buffer,
      std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler);

  void async_send(
      boost::asio::const_buffer buffer,
      std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler);

  bool is_open() const {
    for (auto &item : connections_) {
      if (item.second->status_ != Status::Connected) {
        return false;
      }
    }
    return true;
  }
  void close();

 private:
  void setup();
  void close(boost::asio::ip::udp::endpoint ep);
  void cleanup(boost::asio::ip::udp::endpoint ep);
  void reset(boost::asio::ip::udp::endpoint ep);

  // Exhaunst either receive buffer or user's buffer
  bool poll_receive(std::shared_ptr<Connection> c);
  void timeout(Connection &c);

  void handle_receive_from(const boost::system::error_code &error, size_t size);
  void handle_send_to(boost::asio::ip::udp::endpoint ep, const boost::system::error_code &error);
  void handle_timer(const boost::system::error_code &error);

  void handle_send_to_fin(boost::asio::ip::udp::endpoint ep, const boost::system::error_code &error);

  void send_syn(Connection &c);
  void send_state(Connection &c);
  void send_data(Connection &c, boost::asio::const_buffer data);
  void send_fin(Connection &c);
 private:
  boost::asio::io_service &io_;
  boost::asio::ip::udp::socket socket_;

  boost::asio::ip::udp::endpoint bind_ep_;
  boost::asio::ip::udp::endpoint receive_ep_;
  std::map<boost::asio::ip::udp::endpoint, std::shared_ptr<Connection>> connections_;

  std::vector<uint8_t> receive_buffer_ = std::vector<uint8_t>(65536, 0);
  size_t receive_buffer_offset_ = 0;

  boost::asio::steady_timer timer_;
};


}