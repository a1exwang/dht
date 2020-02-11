#include <albert/bt/peer_connection.hpp>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <stdexcept>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

#include <albert/bencode/bencoding.hpp>
#include <albert/bt/bt.hpp>
#include <albert/bt/peer.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>
#include <albert/utils/utils.hpp>


using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace {

using bdict = std::map<std::string, std::shared_ptr<albert::bencoding::Node>>;
auto newdict(const bdict &dic) {
  return std::make_shared<albert::bencoding::DictNode>(dic);
};
auto newint(int64_t i) {
  return std::make_shared<albert::bencoding::IntNode>(i);
};

}

namespace albert::bt::peer {
static std::string get_string_or_throw(
    const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict, const std::string &key, const std::string &context) {
  if (dict.find(key) == dict.end()) {
    throw InvalidPeerMessage(context + ", '" + key + "' not found");
  }
  auto node = std::dynamic_pointer_cast<bencoding::StringNode>(dict.at(key));
  if (!node) {
    throw InvalidPeerMessage(context + ", '" + key + "' is not a string");
  }
  return *node;
}
static int64_t get_int64_or_throw(
    const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict, const std::string &key, const std::string &context) {
  if (dict.find(key) == dict.end()) {
    throw InvalidPeerMessage(context + ", '" + key + "' not found");
  }
  auto node = std::dynamic_pointer_cast<bencoding::IntNode>(dict.at(key));
  if (!node) {
    throw InvalidPeerMessage(context + ", '" + key + "' is not a int");
  }
  return *node;
}
static std::map<std::string, std::shared_ptr<bencoding::Node>> get_dict_or_throw(
    const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict, const std::string &key, const std::string &context) {
  if (dict.find(key) == dict.end()) {
    throw InvalidPeerMessage(context + ", '" + key + "' not found");
  }
  auto node = std::dynamic_pointer_cast<bencoding::DictNode>(dict.at(key));
  if (!node) {
    throw InvalidPeerMessage(context + ", '" + key + "' is not a int");
  }
  return node->dict();
}
namespace {
std::string make_message(uint8_t type, const uint8_t *data, size_t size) {
  std::stringstream ss;
  auto data_size = size + 1;
  ss.put((uint8_t)((data_size >> 24u) & 0xff));
  ss.put((uint8_t)((data_size >> 16u) & 0xff));
  ss.put((uint8_t)((data_size >> 8u) & 0xff));
  ss.put((uint8_t)((data_size >> 0u) & 0xff));
  ss.put(type);
  if (data) {
    ss.write((const char*)data, size);
  }
  return ss.str();
}
std::string make_extended(const bencoding::Node &payload, uint8_t extended_id) {
  std::stringstream ss;
  ss.put(extended_id);
  payload.encode(ss);
  auto s = ss.str();
  return make_message(MessageTypeExtended, (const uint8_t*)s.data(), s.size());
}
std::string make_empty_message(uint8_t message_type) {
  std::stringstream ss;
  ss.put(message_type);
  auto s = ss.str();
  return make_message(MessageTypeExtended, (const uint8_t*)s.data(), s.size());
}
}

PeerConnection::PeerConnection(
    boost::asio::io_context &io_context,
    const u160::U160 &self,
    const u160::U160 &target,
    uint32_t bind_ip,
    uint16_t bind_port,
    uint32_t ip,
    uint16_t port,
    bool use_utp)
    : self_(self),
      target_(target),
      peer_(std::make_unique<Peer>(ip, port)) {

  if (use_utp) {
    socket_ = std::make_shared<transport::UTPSocket>(io_context, udp::endpoint(boost::asio::ip::address_v4(bind_ip), bind_port));
  } else {
    socket_ = std::make_unique<transport::TCPSocket>(io_context, tcp::endpoint(boost::asio::ip::address_v4(bind_ip), bind_port));
  }
}

void PeerConnection::connect(
    std::function<void(const boost::system::error_code &)> connect_handler,
    std::function<void(int, size_t)> extended_handshake_handler) {
  // Start the asynchronous connect operation.
  connect_handler_ = std::move(connect_handler);
  extended_handshake_handler_ = std::move(extended_handshake_handler);

  LOG(debug) << "PeerConnection::connect, connecting to " << peer_->to_string();
  // Why using shared_from_this(). https://stackoverflow.com/a/35469759
  socket_->async_connect(
      boost::asio::ip::address_v4(peer_->ip()),
      peer_->port(),
      boost::bind(
          &PeerConnection::handle_connect,
          shared_from_this(),
          _1));

}

void PeerConnection::handle_connect(
    const boost::system::error_code &ec) {
  if (!socket_->is_open()) {
    LOG(debug) << "Connect timed out " << this->peer_->to_string();
    connect_handler_(boost::asio::error::timed_out);
  } else if (ec) {
    LOG(debug) << "Connect error: " << this->peer_->to_string() << " " << ec.message();
    close();
    connect_handler_(ec);
  } else {
    connection_status_ = ConnectionStatus::Connected;
    LOG(info) << "PeerConnection: connected to " << this->peer_->to_string();

    continue_receive();
    send_handshake();
  }
}
void PeerConnection::send_handshake() {
  {
    std::stringstream ss;
    self_.encode(ss);
    auto s = ss.str();
    if (s.size() != u160::U160Length) {
      throw std::runtime_error("self_ Invalid node id length, s.size() != NodeIDLength");
    }
    memcpy(sent_handshake_.sender_id, s.data(), u160::U160Length);
  }
  {
    std::stringstream ss;
    target_.encode(ss);
    auto s = ss.str();
    if (s.size() != u160::U160Length) {
      throw std::runtime_error("target_ Invalid node id length, s.size() != NodeIDLength");
    }
    memcpy(sent_handshake_.info_hash, s.data(), u160::U160Length);
  }

  size_t write_size = sizeof(sent_handshake_);
  std::copy(
      (char*)&sent_handshake_,
      (char*)&sent_handshake_ + sizeof(sent_handshake_),
      write_buffer_.begin());
  socket_->async_send(
      boost::asio::buffer(write_buffer_.data(), write_size),
      [](const boost::system::error_code &err, size_t bytes_transferred) {
        if (err) {
          throw std::runtime_error("Failed to write to socket " + err.message());
        }
      });


  auto m = bdict();
  for (auto &item : extended_message_id_) {
    m[item.second] = std::make_shared<bencoding::IntNode>(item.first);
  }
  // extended handshake
  bencoding::DictNode node(
      bdict({
                {
                    "m", newdict(m)
                },
                {"p", newint(6881)},
                {"reqq", newint(500)},
                {"v", std::make_shared<bencoding::StringNode>("wtf/0.0")}
            }));
  socket_->async_send(
      boost::asio::buffer(make_extended(node, 0)),
      [](const boost::system::error_code &err, size_t bytes_transferred) {
        if (err) {
          throw std::runtime_error("Failed to write to socket " + err.message());
        }
      });
}
void PeerConnection::handle_message(uint8_t type, gsl::span<uint8_t> data) {
  if (type == MessageTypeChoke) {
    peer_choke_ = true;
  } else if (type == MessageTypeUnchoke) {
    if (peer_choke_) {
      LOG(debug) << "peer " << peer_->to_string() << " unchoke";
      peer_choke_ = false;
      if (unchoke_handler_) {
        unchoke_handler_();
      }
    }
  } else if (type == MessageTypeInterested) {
    LOG(debug) << "peer " << peer_->to_string() << " interested";
    peer_interested_ = true;
  } else if (type == MessageTypeNotInterested) {
    LOG(debug) << "peer " << peer_->to_string() << " interested";
    peer_interested_ = false;
  } else if (type == MessageTypeBitfield) {
    LOG(debug) << "Bitfield: " << utils::hexdump(data.data(), data.size(), true);
    peer_bitfield_ = std::vector(data.begin(), data.end());
  } else if (type == MessageTypeHave) {
    if (data.size() != sizeof(uint32_t)) {
      LOG(error) << "invalid have message, data length != " << sizeof(uint32_t);
    } else {
      auto piece = utils::host_to_network(*(uint32_t*)data.data());
      set_peer_has_piece(piece);
    }
  } else if (type == MessageTypeRequest) {
    LOG(info) << "Request ";
  } else if (type == MessageTypePiece) {
    std::stringstream ss(std::string((const char*)data.data(), (const char*)data.data()+2*sizeof(uint32_t)));
    uint32_t index = 0, begin = 0;
    ss.read((char*)&index, sizeof(index));
    index = utils::network_to_host(index);
    ss.read((char*)&begin, sizeof(begin));
    begin = utils::network_to_host(begin);
    size_t block_size = data.size() - 2*sizeof(uint32_t);
    if (block_handler_) {
      block_handler_(index, begin, gsl::span<uint8_t>(data.data()+2*sizeof(uint32_t), data.size()-2*sizeof(uint32_t)));
    }
  } else if (type == MessageTypeExtended) {
    if (data.size() > 0) {
      uint8_t extended_id = data[0];
      auto content_size = data.size() - 1;
      std::stringstream ss(std::string((const char*)data.data() + 1, content_size));
      auto node = bencoding::Node::decode(ss);

      std::istreambuf_iterator<char> eos;
      std::string rest(std::istreambuf_iterator<char>(ss), eos);
      std::vector<uint8_t> appended_data(rest.size());
      std::copy(rest.begin(), rest.end(), appended_data.begin());
      if (auto dict = std::dynamic_pointer_cast<bencoding::DictNode>(node); dict) {
        handle_extended_message(extended_id, dict, appended_data);
      } else {
        LOG(error) << "Invalid extended message, root node is not a dict. Closing connection";
        close();
      }
    } else {
      LOG(error) << "PeerConnection: Invalid extended message, expected size";
      close();
    }
  } else {
    LOG(debug) << "PeerConnection: Unknown message type ignored " << (int)type;
  }
}

void PeerConnection::handle_extended_message(
    uint8_t extended_id,
    std::shared_ptr<bencoding::DictNode> msg,
    const std::vector<uint8_t> &appended_data
    ) {
  auto &dict = msg->dict();
  if (extended_id == 0) {
    // extended handshake
    std::stringstream ss;
    msg->encode(ss, bencoding::EncodeMode::JSON);
    extended_handshake_ = msg;

    auto total_size = get_int64_or_throw(dict, "metadata_size", "ut_metadata");
    m_dict_ = get_dict_or_throw(dict, "m", "ut_metadata");
    piece_count_ = ceil(double(total_size) / MetadataPieceSize);
    extended_handshake_handler_(piece_count_, total_size);

    if (piece_count_ == 0) {
      close();
      throw InvalidPeerMessage("piece count cannot be zero");
    }

    LOG(debug) << "Extended handshake: from " << peer_->to_string() << std::endl
               << "total pieces: " << piece_count_
               << "data: " << ss.str();

  } else {
    if (extended_message_id_.find(extended_id) == extended_message_id_.end()) {
      LOG(error) << "Invalid extended message, unknown exteneded id " << extended_id;
    } else {
      auto message_type = extended_message_id_[extended_id];
      if (message_type == "ut_metadata") {
        auto msg_type = get_int64_or_throw(dict, "msg_type", "ut_metadata");
        if (msg_type == ExtendedMessageTypeRequest) {
          LOG(error) << "msg_type request not implemented";
        } else if (msg_type == ExtendedMessageTypeData) {
          auto piece = get_int64_or_throw(dict, "piece", "ut_metadata");
//          auto total_size = get_int64_or_throw(dict, "total_size", "ut_metadata");
          piece_data_handler_(piece, appended_data);
        } else if (msg_type == ExtendedMessageTypeReject) {
          LOG(error) << "msg_type reject not implemented";
        } else {
          LOG(error) << "unknown msg_type";
        }
      } else {
        LOG(error) << "Invalid extended message, unknown message type " << message_type;
      }
    }

  }
}

void PeerConnection::handle_receive(const boost::system::error_code &err, size_t bytes_transferred) {
  // we may arrive here if the torrent download complete before handle_receive() is called
  if (status() == ConnectionStatus::Disconnected) {
    return;
  }

//  LOG(info) << "PeerConnection: received " << bytes_transferred << std::endl
//        << utils::hexdump(read_buffer_.data(), bytes_transferred, true);

  if (err == boost::asio::error::eof) {
    connection_status_ = ConnectionStatus::Disconnected;
    return;
  } else if (err == boost::asio::error::connection_reset) {
    LOG(warning) << "Peer reset the connection " << peer_->to_string() << ", id " << peer_id_.to_string();
    connection_status_ = ConnectionStatus::Disconnected;
    return;
  } else if (err) {
    throw std::runtime_error("Unhandled error when reading to from socket " + err.message());
  }

  if (connection_status_ == ConnectionStatus::Connecting) {
    connection_status_ = ConnectionStatus::Connected;
  }

  read_ring_.appended(bytes_transferred);

  try {
    while (read_ring_.data_size() > 0) {
      // Handle handshake
      if (!handshake_completed_) {
        if (read_ring_.has_data(sizeof(Handshake))) {
          // pop front sizeof(Handshake)
          read_ring_.pop_data(&received_handshake_, sizeof(Handshake));
          std::stringstream ss(
          std::string((char *) &received_handshake_.sender_id,
                      sizeof(u160::U160)));
          peer_id_ = u160::U160::decode(ss);
          handshake_completed_ = true;
          auto ss1 = std::stringstream(std::string((char *) &received_handshake_.sender_id, sizeof(u160::U160)));
          auto received_info_hash = u160::U160::decode(ss1);
//            if (received_info_hash != target_) {
//              LOG(error) << "Peer info_hash not matched, closing connection. target: " << target_.to_string() << ", peer: " << received_info_hash.to_string();
//              close();
//              return;
//            } else {
          if (connect_handler_) {
            connect_handler_(boost::system::error_code());
          }
//            }
        } else {
          LOG(debug) << "handshake not complete, segmented " << peer_->to_string();
          break;
        }
      } else {
        uint32_t message_size = 0;
        bool need_skip_message_size = false;
        if (message_segmented) {
          message_size = last_message_size_;
        } else {
          if (read_ring_.has_data(sizeof(uint32_t))) {
            read_ring_.pop_data(&message_size, sizeof(uint32_t));
            message_size = utils::network_to_host<uint32_t>(message_size);
          } else {
            LOG(debug) << "message size not complete, segmented " << peer_->to_string();
            break;
          }
        }

        if (message_size == 0) {
          // This is a keep alive message
          handle_keep_alive();
        } else {
          if (read_ring_.has_data(message_size)) {
            uint8_t message_type;
            read_ring_.pop_data(&message_type, sizeof(message_type));
            auto content_size = message_size - sizeof(message_type);
            auto buf = read_ring_.use_data(content_size);
            handle_message(message_type, buf);
            read_ring_.skip_data(content_size);
            message_segmented = false;
          } else {
            last_message_size_ = message_size;
            message_segmented = true;
            LOG(debug) << "message content not complete, segmented " << peer_->to_string() << " " << read_ring_.data_size() << "/" << message_size;
            break;
          }
        }
      }
    }
    if (read_ring_.data_size() == 0) {
      LOG(debug) << "handle_receive complete because read_ring_ is empty";
    }
  } catch (const bencoding::InvalidBencoding &e) {
    LOG(error) << "parse BT handshake: Invalid bencoding: " << e.what();
    close();
  } catch (const InvalidPeerMessage &e){
    LOG(error) << "Invalid peer message " << e.what();
    close();
  }

  continue_receive();
}
void PeerConnection::send_metadata_request(int64_t piece) {
  if (!extended_handshake_) {
    throw InvalidStatus("Cannot send metadata request before receiving extended handshake");
  } else {
    if (!has_peer_extended_message(MetadataMessage)) {
      throw InvalidStatus("Peer(" + peer_->to_string() + ") does not support metadata message");
    } else {
      auto extended_id = get_peer_extended_message_id(MetadataMessage);
      // extended message
      bencoding::DictNode node(
          bdict({
                    {
                        "msg_type",
                        newint(ExtendedMessageTypeRequest),
                    },
                    {
                        "piece",
                        newint(piece),
                    }
                }));
      socket_->async_send(
          boost::asio::buffer(make_extended(node, extended_id)),
          [](const boost::system::error_code &err, size_t bytes_transferred) {
            if (err) {
              throw std::runtime_error("Failed to write to socket " + err.message());
            }
          });
    }
  }
}
PeerConnection::~PeerConnection() = default;
uint8_t PeerConnection::get_peer_extended_message_id(const std::string &message_name) {
  return get_int64_or_throw(m_dict_, message_name, "PeerConenction::get_peer_extended_message_id");
}

void PeerConnection::close() {
  socket_->close();
  connection_status_ = ConnectionStatus::Disconnected;
}

uint8_t PeerConnection::has_peer_extended_message(const std::string &message_name) const {
  return extended_handshake_->dict().find(message_name) == extended_handshake_->dict().end();
}

void PeerConnection::start_metadata_transfer(
    std::function<void(
        int piece,
        const std::vector<uint8_t> &piece_data
    )> piece_data_handler) {

  piece_data_handler_ = std::move(piece_data_handler);

  // start metadata pieces transfer, but in a random order, to increase concurrency
  std::vector<int> piece_ids;
  for (int i = 0; i < piece_count_; i++) {
    piece_ids.push_back(i);
  }
  std::shuffle(piece_ids.begin(), piece_ids.end(), std::random_device{});

  for (auto i : piece_ids) {
    LOG(debug) << "sending metadata request to " << peer_->to_string() << ", " << i;
    send_metadata_request(i);
  }
}

void PeerConnection::send_peer_message(uint8_t type, std::vector<uint8_t> data) {
  uint32_t packet_size = data.size() + sizeof(uint8_t);
  uint32_t total_size = packet_size + sizeof(uint32_t);
  std::stringstream ss;
  auto send_write_size = utils::host_to_network(packet_size);
  ss.write((const char*)&send_write_size, sizeof(send_write_size));
  ss.put(type);
  ss.write((const char*)data.data(), data.size());

  assert(total_size < write_buffer_.size());

  auto s = ss.str();
  std::copy(
      s.begin(),
      s.end(),
      write_buffer_.begin());
  socket_->async_send(
      boost::asio::buffer(write_buffer_.data(), total_size),
      [](const boost::system::error_code &err, size_t bytes_transferred) {
        if (err) {
          throw std::runtime_error("Failed to write to socket " + err.message());
        }
      });

}

void PeerConnection::interest(std::function<void()> unchoke_handler) {
  unchoke_handler_ = std::move(unchoke_handler);
  send_peer_message(MessageTypeInterested, {});
  if (!peer_choke_) {
    unchoke_handler_();
  }
}
void PeerConnection::request(size_t index, size_t begin, size_t length) {
  std::stringstream ss;

  auto index_n = utils::host_to_network<uint32_t>(index);
  ss.write((const char*)&index_n, sizeof(index_n));
  auto begin_n = utils::host_to_network<uint32_t>(begin);
  ss.write((const char*)&begin_n, sizeof(index_n));
  auto length_n = utils::host_to_network<uint32_t>(length);
  ss.write((const char*)&length_n, sizeof(index_n));

  auto s = ss.str();
  std::vector<uint8_t> data((const uint8_t*)s.data(), (const uint8_t*)s.data() + ss.str().size());
  LOG(debug) << "requesting piece " << index << " " << begin << " " << length;
  send_peer_message(MessageTypeRequest, data);
}
void PeerConnection::set_peer_has_piece(size_t piece) {
  size_t byte = 0;
  if (piece != 0) {
    byte = (piece-1u) / 8u + 1u;
  }
  auto bit = 7u - (piece % 8u);

  if (byte < peer_bitfield_.size()) {
    peer_bitfield_[byte] |= 1u << bit;
  } else {
    LOG(error) << "cannot set piece " << piece << ", out of range: " << (peer_bitfield_.size()*8);
  }
}
bool PeerConnection::has_piece(size_t piece) const {
  size_t byte = 0;
  if (piece != 0) {
    // 0-7 => 0
    //   0 -> 7
    //   1 -> 6
    //   7 -> 0
    // 8-15 => 1
    byte = piece / 8u;
  }
  auto bit = 7u - (piece % 8u);

  if (byte < peer_bitfield_.size()) {
    return (peer_bitfield_[byte] >> bit) & 1u;
  } else {
    throw std::invalid_argument("cannot get piece " + std::to_string(piece) + ", out of range: " + std::to_string(peer_bitfield_.size()*8));
  }
}
size_t PeerConnection::next_valid_piece(size_t piece) const {
  for (size_t i = piece; i < piece_count_; i++) {
    if (has_piece(i)) {
      return i;
    }
  }
  return piece_count_;
}
void PeerConnection::handle_keep_alive() {
  LOG(info) << "Peer keep alive " << peer_->to_string();
}
void PeerConnection::continue_receive() {
  auto buf = read_ring_.use_for_append(MCU);
  socket_->async_receive(
      boost::asio::buffer(buf.data(), buf.size()),
      boost::bind(
          &PeerConnection::handle_receive,
          shared_from_this(),
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
}

}
