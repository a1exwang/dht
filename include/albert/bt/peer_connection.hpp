#pragma once

#include <cstdint>

#include <array>
#include <memory>

#include <boost/asio/ip/tcp.hpp>

#include <albert/bt/bt.hpp>
#include <albert/krpc/krpc.hpp>

namespace albert::bt::peer {
class Peer;

constexpr uint8_t MessageTypeChoke = 0;
constexpr uint8_t MessageTypeUnchoke = 1;
constexpr uint8_t MessageTypeInterested = 2;
constexpr uint8_t MessageTypeNotInterested = 3;
constexpr uint8_t MessageTypeExtended = 20;
constexpr uint8_t ExtendedMessageTypeRequest = 0;
constexpr uint8_t ExtendedMessageTypeData = 1;
constexpr uint8_t ExtendedMessageTypeReject = 2;
constexpr size_t MetadataPieceSize = 16 * 1024;

class InvalidPeerMessage :public std::runtime_error {
 public:
  InvalidPeerMessage(const std::string &s) :runtime_error(s) { }
};
class InvalidStatus :public std::runtime_error {
 public:
  InvalidStatus(const std::string &s) :runtime_error(s) { }
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

constexpr const char *MetadataMessage = "ut_metadata";

enum class ConnectionStatus {
  Connecting,
  Connected,
  Disconnected
};
class PeerConnection {
 public:
  PeerConnection(
      boost::asio::io_context &io_context,
      const krpc::NodeID &self,
      const krpc::NodeID &target,
      uint32_t ip,
      uint16_t port);
  ~PeerConnection();
  void connect();
  ConnectionStatus status() const { return connection_status_; }
  void close();

  void set_piece_data_handler(std::function<void(int, const std::vector<uint8_t> &)> handler);
  void set_metadata_handshake_handler(std::function<void(int, size_t)> handler);

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

  uint8_t has_peer_extended_message(const std::string &message_name) const {
    return extended_handshake_->dict().find(message_name) == extended_handshake_->dict().end();
  }
  uint8_t get_peer_extended_message_id(const std::string &message_name);

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
  ConnectionStatus connection_status_ = ConnectionStatus::Connecting;
  bool handshake_completed_ = false;
  bool message_segmented = false;

  bool peer_interested_ = false;
  bool peer_choke_ = true;

  uint32_t last_message_size_ = 0;
  size_t piece_count_;

  std::shared_ptr<bencoding::DictNode> extended_handshake_;
  std::map<std::string, std::shared_ptr<bencoding::Node>> m_dict_;
  std::map<uint8_t, std::string> extended_message_id_ = {
      {2, MetadataMessage}
  };

  std::function<void(
      int piece,
      const std::vector<uint8_t> &piece_data
  )> piece_data_handler_;
  std::function<void(int total_pieces, size_t total_size)> metadata_handshake_handler_;
};

}