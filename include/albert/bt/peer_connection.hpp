#pragma once

#include <cstdint>

#include <array>
#include <memory>

#include <boost/asio/ip/tcp.hpp>

#include <albert/bt/bt.hpp>
#include <albert/krpc/krpc.hpp>

namespace albert::bt::peer {

class InvalidPeerMessage :public std::runtime_error {
 public:
  InvalidPeerMessage(const std::string &s) :runtime_error(s) { }
};

#pragma pack(1)
struct Handshake {
  char magic[20] = {19,'B','i','t','T','o','r','r','e','n','t',' ','p','r','o','t','o','c','o','l'};
  /**
   * Enabled bits:
   * BEP-10 extension bit
   * DHT bit
   */
  uint8_t reserved[8]{0, 0, 0, 0, 0, 0x10, 0, 0x01};
  uint8_t info_hash[20]{};
  uint8_t sender_id[20]{};
};
#pragma pack()

class PeerConnection {
 public:
  PeerConnection(
      boost::asio::io_context &io_context,
      const krpc::NodeID &self,
      const krpc::NodeID &target,
      uint32_t ip,
      uint16_t port);
  void connect();
  bool is_connected() const { return connected_; };

 private:
  void handle_connect(
      const boost::system::error_code& ec);
  void send_handshake();
  void send_metadata_request(int64_t piece);

  uint32_t read_size();
  bool has_data(size_t size) const { return this->read_ring_.size() >= size; }
  void pop_data(void *output, size_t size);

  void handle_message(uint32_t size, uint8_t type, const std::vector<uint8_t> &data);
  void handle_extended_message(
      uint8_t extended_id,
      std::shared_ptr<bencoding::DictNode> msg,
      const std::vector<uint8_t> &appended_data);

  void handle_receive(const boost::system::error_code &err, size_t bytes_transferred);

 private:
  // boost asio stuff
  boost::asio::ip::tcp::socket socket_;

  Handshake sent_handshake_;
  Handshake received_handshake_;

  krpc::NodeID self_;
  krpc::NodeID target_;
  krpc::NodeID peer_id_;

  std::array<uint8_t, 65536> read_buffer_;
  std::vector<uint8_t> read_ring_;
  std::array<uint8_t, 65536> write_buffer_;

  std::unique_ptr<Peer> peer_;
  bool connected_ = false;
  bool handshake_completed_ = false;
  bool in_message_completed_ = true;

  bool peer_interested_ = false;
  bool peer_choke_ = true;

  uint32_t last_message_size_ = 0;
  size_t piece_count_;

  std::stringstream pieces_stream_;
  std::shared_ptr<bencoding::DictNode> extended_handshake_;
  std::map<uint8_t, std::string> extended_message_id_;
};

}