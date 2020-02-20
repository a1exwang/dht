#pragma once

#include <cstdint>

#include <array>
#include <memory>
#include <gsl/span>

#include <boost/asio/ip/tcp.hpp>

#include <albert/bt/bt.hpp>
#include <albert/bt/transport.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/u160/u160.hpp>
#include <albert/bt/ring_buffer.hpp>

namespace albert::bt::peer {
class Peer;

constexpr uint8_t MessageTypeChoke = 0;
constexpr uint8_t MessageTypeUnchoke = 1;
constexpr uint8_t MessageTypeInterested = 2;
constexpr uint8_t MessageTypeNotInterested = 3;
constexpr uint8_t MessageTypeHave = 4;
constexpr uint8_t MessageTypeBitfield = 5;
constexpr uint8_t MessageTypeRequest = 6;
constexpr uint8_t MessageTypePiece = 7;
constexpr uint8_t MessageTypeCancel = 8;
constexpr uint8_t MessageTypePort = 9;
constexpr uint8_t MessageTypeExtended = 20;
constexpr uint8_t ExtendedMessageTypeRequest = 0;
constexpr uint8_t ExtendedMessageTypeData = 1;
constexpr uint8_t ExtendedMessageTypeReject = 2;
constexpr size_t MetadataPieceSize = 16 * 1024;

constexpr size_t MCU = 65536;

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
  Disconnected,
};
class PeerConnection :public std::enable_shared_from_this<PeerConnection> {
 public:
  static std::atomic<size_t> counter;
  PeerConnection(
      boost::asio::io_context &io_context,
      const u160::U160 &self,
      const u160::U160 &target,
      uint32_t bind_ip,
      uint16_t bind_port,
      uint32_t ip,
      uint16_t port,
      bool use_utp);
  ~PeerConnection();
  PeerConnection(const PeerConnection &) = delete;
  PeerConnection &operator=(const PeerConnection &) = delete;
  void connect(
      std::function<void(const boost::system::error_code &)> connect_handler = [](const boost::system::error_code&) { },
      std::function<void(int, size_t)> extended_handshake_handler = [](int, size_t) { }
  );
  ConnectionStatus status() const { return connection_status_; }
  std::string failed_reason() const { return failed_reason_; }
  void close();

  void interest(std::function<void()> unchoke_handler);
  bool has_piece(size_t piece) const;
  size_t next_valid_piece(size_t piece) const;

  void set_block_handler(std::function<void(size_t piece, size_t offset, gsl::span<uint8_t> data)> block_handler) { block_handler_ = block_handler; }
  void request(size_t index, size_t begin, size_t length);

  const Peer &peer() { return *peer_; }
  const u160::U160 &peer_id() const { return peer_id_; }

  void start_metadata_transfer(
      std::function<void(
          int piece,
          const std::vector<uint8_t> &piece_data
      )> piece_data_handler
  );

  size_t memory_size() const;
 private:
  /**
   * Handlers
   */
  void handle_connect(const boost::system::error_code& ec);
  void handle_receive(const boost::system::error_code &err, size_t bytes_transferred);

  void handle_message(uint8_t type, const gsl::span<uint8_t> data);
  void handle_extended_message(
      uint8_t extended_id,
      std::shared_ptr<bencoding::DictNode> msg,
      const std::vector<uint8_t> &appended_data);
  void handle_keep_alive();
  /**
   * Send helpers
   */
  void send_handshake();
  void send_metadata_request(int64_t piece);
  void send_peer_message(uint8_t type, std::vector<uint8_t> data);

  void continue_receive();

  uint32_t read_size() {
    uint32_t ret;
    read_ring_.pop_data(&ret, sizeof(uint32_t));
    return utils::network_to_host(ret);
  }

  void set_peer_has_piece(size_t piece);

  uint8_t has_peer_extended_message(const std::string &message_name) const;
  uint8_t get_peer_extended_message_id(const std::string &message_name);
 private:
  // boost asio stuff
//  boost::asio::ip::tcp::socket socket_;
  std::shared_ptr<transport::Socket> socket_;

  Handshake sent_handshake_;
  Handshake received_handshake_;

  u160::U160 self_;
  u160::U160 target_;
  u160::U160 peer_id_;

  // buffers
  std::array<uint8_t, 65536> write_buffer_{};
  ring_buffer::RingBuffer read_ring_;

  // connection status
  std::unique_ptr<Peer> peer_;
  ConnectionStatus connection_status_ = ConnectionStatus::Connecting;
  std::string failed_reason_;
  bool handshake_completed_ = false;
  bool message_segmented = false;

  // Peer status
  bool peer_interested_ = false;
  bool peer_choke_ = true;
  std::vector<uint8_t> peer_bitfield_;
  std::shared_ptr<bencoding::DictNode> extended_handshake_;
  std::map<std::string, std::shared_ptr<bencoding::Node>> m_dict_;
  std::map<uint8_t, std::string> extended_message_id_ = {
      {2, MetadataMessage}
  };

  // stats
  uint32_t last_message_size_ = 0;
  size_t piece_count_ = 0;

  // handlers
  std::function<void(
      int piece,
      const std::vector<uint8_t> &piece_data
  )> piece_data_handler_;
  std::function<void(int total_pieces, size_t total_size)> extended_handshake_handler_;
  std::function<void(const boost::system::error_code &)> connect_handler_;

  std::function<void()> unchoke_handler_;
  std::function<void(size_t piece, size_t offset, gsl::span<uint8_t> data)> block_handler_;
};

}