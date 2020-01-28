#include <albert/bt/peer_connection.hpp>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

#include <albert/bencode/bencoding.hpp>
#include <albert/bt/bt.hpp>
#include <albert/bt/peer.hpp>
#include <albert/log/log.hpp>
#include <albert/utils/utils.hpp>

using boost::asio::ip::tcp;

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
    const krpc::NodeID &self,
    const krpc::NodeID &target,
    uint32_t ip,
    uint16_t port)
    :socket_(io_context),
     self_(self),
     target_(target),
     peer_(std::make_unique<Peer>(ip, port))
{ }

void PeerConnection::connect() {
  // Start the asynchronous connect operation.

  // Why using shared_from_this(). https://stackoverflow.com/a/35469759
  socket_.async_connect(
      tcp::endpoint(boost::asio::ip::address_v4(peer_->ip()), peer_->port()),
      boost::bind(
          &PeerConnection::handle_connect,
          shared_from_this(),
          _1));

}

void PeerConnection::handle_connect(
    const boost::system::error_code &ec) {
  if (!socket_.is_open()) {
    LOG(debug) << "Connect timed out " << this->peer_->to_string();
  } else if (ec) {
    LOG(debug) << "Connect error: " << this->peer_->to_string() << " " << ec.message();
    close();
  } else {
    connection_status_ = ConnectionStatus::Connected;
    LOG(info) << "PeerConnection: connected to " << this->peer_->to_string();

    socket_.async_receive(
        boost::asio::buffer(read_buffer_.data(), read_buffer_.size()),
        0,
        boost::bind(
            &PeerConnection::handle_receive,
            shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    send_handshake();
  }
}
void PeerConnection::send_handshake() {
  {
    std::stringstream ss;
    self_.encode(ss);
    auto s = ss.str();
    if (s.size() != krpc::NodeIDLength) {
      throw std::runtime_error("self_ Invalid node id length, s.size() != NodeIDLength");
    }
    memcpy(sent_handshake_.sender_id, s.data(), krpc::NodeIDLength);
  }
  {
    std::stringstream ss;
    target_.encode(ss);
    auto s = ss.str();
    if (s.size() != krpc::NodeIDLength) {
      throw std::runtime_error("target_ Invalid node id length, s.size() != NodeIDLength");
    }
    memcpy(sent_handshake_.info_hash, s.data(), krpc::NodeIDLength);
  }

  size_t write_size = sizeof(sent_handshake_);
  std::copy(
      (char*)&sent_handshake_,
      (char*)&sent_handshake_ + sizeof(sent_handshake_),
      write_buffer_.begin());
  socket_.async_send(
      boost::asio::buffer(write_buffer_.data(), write_size),
      0,
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
  socket_.async_send(
      boost::asio::buffer(make_extended(node, 0)),
      0,
      [](const boost::system::error_code &err, size_t bytes_transferred) {
        if (err) {
          throw std::runtime_error("Failed to write to socket " + err.message());
        }
      });
}

uint32_t PeerConnection::read_size() {
  uint32_t ret;
  pop_data(&ret, sizeof(uint32_t));
  return dht::utils::network_to_host(ret);
}
void PeerConnection::pop_data(void *output, size_t size) {
  memcpy(output, read_ring_.data(), size);
  auto tmp = std::vector<uint8_t>(
      std::next(read_ring_.begin(), size) ,
      read_ring_.end());
  read_ring_ = tmp;
}
void PeerConnection::handle_message(uint32_t size, uint8_t type, const std::vector<uint8_t> &data) {
  if (type == MessageTypeChoke) {
    peer_choke_ = true;
  } else if (type == MessageTypeUnchoke) {
    LOG(debug) << "peer " << peer_->to_string() << " unchoke";
    peer_choke_ = false;
  } else if (type == MessageTypeInterested) {
    LOG(debug) << "peer " << peer_->to_string() << " interested";
    peer_interested_ = true;
  } else if (type == MessageTypeNotInterested) {
    LOG(debug) << "peer " << peer_->to_string() << " interested";
    peer_interested_ = false;
  } else if (type == MessageTypeExtended) {
    if (size > 0) {
      uint8_t extended_id = data[0];
      auto content_size = size - 1;
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
    metadata_handshake_handler_(piece_count_, total_size);

    LOG(debug) << "Extended handshake: from " << peer_->to_string() << std::endl
              << "total pieces: " << piece_count_
              << "data: " << ss.str();
    if (piece_count_ == 0) {
      close();
      throw InvalidPeerMessage("piece count cannot be zero");
    }

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

  if (err == boost::asio::error::eof) {
    connection_status_ = ConnectionStatus::Disconnected;
  } else if (err == boost::asio::error::connection_reset) {
    LOG(warning) << "Peer reset the connection " << peer_->to_string() << ", id " << peer_id_.to_string();
    connection_status_ = ConnectionStatus::Disconnected;
  } else if (err) {
    throw std::runtime_error("Unhandled error when reading to from socket " + err.message());
  } else {
    if (connection_status_ == ConnectionStatus::Connecting) {
      connection_status_ = ConnectionStatus::Connected;
    }

    try {
      read_ring_.reserve(read_ring_.size() + bytes_transferred);
      for (int i = 0; i < bytes_transferred; i++) {
        read_ring_.push_back(read_buffer_[i]);
      }
      while (true) {
        if (message_segmented) {
          auto message_size = last_message_size_;
          if (has_data(message_size)) {
            LOG(debug) << "message content complete, segmentation done " << peer_->to_string() << " " << read_ring_.size() << "/" << message_size;
            uint8_t message_id;
            auto content_size = message_size - 1;
            pop_data(&message_id, 1);
            std::vector<uint8_t> data(content_size);
            pop_data(data.data(), content_size);
            message_segmented = false;
            handle_message(content_size, message_id, data);
          } else {
            LOG(debug) << "message content not complete, segmented again " << peer_->to_string() << " " << read_ring_.size() << "/" << message_size;
            break;
          }
        } else if (handshake_completed_) {
          if (has_data(sizeof(uint32_t))) {
            auto message_size = read_size();
            if (has_data(message_size + sizeof(uint8_t))) {
              uint8_t message_type;
              pop_data(&message_type, 1);
              std::vector<uint8_t> data(message_size-1);
              pop_data(data.data(), message_size-1);
              handle_message(message_size-1, message_type, data);
            } else {
              last_message_size_ = message_size;
              message_segmented = true;
              LOG(debug) << "message content not complete, segmented " << peer_->to_string() << " " << read_ring_.size() << "/" << message_size;
              break;
            }
          } else {
            LOG(debug) << "message size not complete, segmented " << peer_->to_string();
            break;
          }
        } else {
          // Not segmented && no handshake
          if (has_data(sizeof(Handshake))) {
            // pop front sizeof(Handshake)
            pop_data(&received_handshake_, sizeof(Handshake));
            std::stringstream ss(
            std::string((char *) &received_handshake_.sender_id,
                        sizeof(krpc::NodeID)));
            peer_id_ = krpc::NodeID::decode(ss);
            handshake_completed_ = true;
          } else {
            LOG(debug) << "handshake not complete, segmented " << peer_->to_string();
            break;
          }
        }
      }
    } catch (const bencoding::InvalidBencoding &e) {
      LOG(error) << "parse BT handshake: Invalid bencoding: " << e.what();
      close();
    } catch (const InvalidPeerMessage &e){
      LOG(error) << "Invalid peer message " << e.what();
      close();
    }
    socket_.async_receive(
        boost::asio::buffer(read_buffer_.data(), read_buffer_.size()),
        0,
        boost::bind(
            &PeerConnection::handle_receive,
            shared_from_this(),
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));

  }
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
      socket_.async_send(
          boost::asio::buffer(make_extended(node, extended_id)),
          0,
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
  socket_.close();
  connection_status_ = ConnectionStatus::Disconnected;
}
void PeerConnection::set_piece_data_handler(
    std::function<void(int, const std::vector<uint8_t> &)> handler) {
  this->piece_data_handler_ = std::move(handler);
}
void PeerConnection::set_metadata_handshake_handler(std::function<void(int, size_t)> handler) { metadata_handshake_handler_ = handler; }
uint8_t PeerConnection::has_peer_extended_message(const std::string &message_name) const {
  return extended_handshake_->dict().find(message_name) == extended_handshake_->dict().end();
}

}
