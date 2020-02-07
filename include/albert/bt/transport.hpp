#pragma once

#include <cstddef>

#include <functional>
#include <memory>

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/buffer.hpp>

#include <albert/utp/utp.hpp>

namespace albert::bt::transport {

class Socket {
 public:
  virtual void async_connect(
      boost::asio::ip::address ip,
      uint16_t port,
      std::function<void(const boost::system::error_code &error)> handler) = 0;

  virtual void async_receive(
      boost::asio::mutable_buffer buffer,
      std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) = 0;

  virtual void async_send(
      boost::asio::const_buffer buffer,
      std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) = 0;

  virtual bool is_open() const = 0;
  virtual void close() = 0;

  virtual ~Socket() = default;
};

class UTPSocket :public Socket {
 public:
  UTPSocket(boost::asio::io_service &io,
            boost::asio::ip::udp::endpoint bind_ep) :socket_(std::make_shared<utp::Socket>(io, bind_ep)) { }

  void async_connect(
      boost::asio::ip::address ip,
      uint16_t port,
      std::function<void(const boost::system::error_code &error)> handler) override {
    socket_->async_connect(boost::asio::ip::udp::endpoint(ip, port), std::move(handler));
  }

  void async_receive(
      boost::asio::mutable_buffer buffer,
      std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) override {
    socket_->async_receive(buffer, std::move(handler));
  }

  void async_send(
      boost::asio::const_buffer buffer,
      std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) override {
    socket_->async_send(buffer, std::move(handler));
  }

  void close() override {
    socket_->close();
  }
  bool is_open() const override {
    return socket_->is_open();
  }
 private:
  std::shared_ptr<utp::Socket> socket_;
};

class TCPSocket :public Socket {
 public:
  TCPSocket(boost::asio::io_service &io,
            boost::asio::ip::tcp::endpoint bind_ep) :socket_(io, bind_ep) { }

  void async_connect(
      boost::asio::ip::address ip,
      uint16_t port,
      std::function<void(const boost::system::error_code &error)> handler) override {
    socket_.async_connect(boost::asio::ip::tcp::endpoint(ip, port), std::move(handler));
  }

  void async_receive(
      boost::asio::mutable_buffer buffer,
      std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) override {
    socket_.async_receive(buffer, std::move(handler));
  }

  void async_send(
      boost::asio::const_buffer buffer,
      std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) override {
    socket_.async_send(buffer, std::move(handler));
  }

  void close() override {
    socket_.close();
  }
  bool is_open() const override {
    return socket_.is_open();
  }
 private:
  boost::asio::ip::tcp::socket socket_;
};


}