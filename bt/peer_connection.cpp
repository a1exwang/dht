#include <bt/peer_connection.hpp>

#include <memory>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

#include <bt/bt.hpp>
#include <bencode/bencoding.hpp>
#include <utils/log.hpp>
#include <utils/utils.hpp>

using boost::asio::ip::tcp;

namespace {

using bdict = std::map<std::string, std::shared_ptr<bencoding::Node>>;
auto newdict(const bdict &dic) {
  return std::make_shared<bencoding::DictNode>(dic);
};
auto newint(int64_t i) {
  return std::make_shared<bencoding::IntNode>(i);
};
}

namespace bt::peer {
namespace {
std::string make_message(uint8_t type, const uint8_t *data, size_t data_size) {
  std::stringstream ss;
  ss.put((uint8_t)((data_size >> 24) & 0xff));
  ss.put((uint8_t)((data_size >> 16) & 0xff));
  ss.put((uint8_t)((data_size >> 8) & 0xff));
  ss.put((uint8_t)((data_size >> 0) & 0xff));
  ss.put(type);
  if (data) {
    ss.write((const char*)data, data_size);
  }
  return ss.str();
}
std::string make_extended(const bencoding::Node &payload) {
  std::stringstream ss;
  payload.encode(ss);
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
  socket_.async_connect(
      tcp::endpoint(boost::asio::ip::address_v4(peer_->ip()), peer_->port()),
      boost::bind(
          &PeerConnection::handle_connect,
          this, _1));

}

void PeerConnection::handle_connect(
    const boost::system::error_code &ec) {
  if (!socket_.is_open()) {
    LOG(error) << "Connect timed out " << this->peer_id_.to_string();
  } else if (ec) {
    LOG(error) << "Connect error: " << ec.message();
    socket_.close();
  } else {
    connected_ = true;
    LOG(info) << "connected to " << this->peer_id_.to_string();

    socket_.async_receive(
        boost::asio::buffer(read_buffer_.data(), read_buffer_.size()),
        0,
        boost::bind(
            &PeerConnection::handle_receive,
            this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    send_handshake();
    send_metadata_request(0);
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
        LOG(info) << "written " << bytes_transferred;
      });

  // extended handshake
  bencoding::DictNode node(
      bdict({
                {
                    "m",
                    newdict(bdict({{"ut_metadata", newint(3)}})),
                }
            }));
  socket_.async_send(
      boost::asio::buffer(make_extended(node)),
      0,
      [](const boost::system::error_code &err, size_t bytes_transferred) {
        if (err) {
          throw std::runtime_error("Failed to write to socket " + err.message());
        }
        LOG(info) << "written " << bytes_transferred;
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
  if (type == MessageTypeExtended) {
    if (size > 0) {
      uint8_t extended_id = data[0];
      std::stringstream ss(std::string((const char*)data.data() + 1, data.size()));
      auto node = bencoding::Node::decode(ss);
      auto s = ss.str();
      std::vector<uint8_t> appended_data(s.size());
      std::copy(s.begin(), s.end(), appended_data.begin());
      if (auto dict = std::dynamic_pointer_cast<bencoding::DictNode>(node); dict) {
        handle_extended_message(extended_id, *dict, appended_data);
      } else {
        LOG(error) << "invalid extended message, root node is not a dict";
      }
    } else {
      LOG(warning) << "empty extended message";
    }
  } else {
    LOG(warning) << "Unknown message type ignored " << type;
  }
}
void PeerConnection::handle_extended_message(
    uint8_t extended_id,
    const bencoding::DictNode &msg,
    const std::vector<uint8_t> &appended_data
    ) {
  std::stringstream ss;
  msg.encode(ss, bencoding::EncodeMode::JSON);
  LOG(info) << "extended id: " << (int)extended_id << " "
            << "appended size: " << appended_data.size() << " "
            << "data: " << ss.str();
}
void PeerConnection::handle_receive(const boost::system::error_code &err, size_t bytes_transferred) {

  if (err == boost::asio::error::eof) {
    LOG(error) << "eof " << bytes_transferred << " from " << peer_id_.to_string();
  } else if (err) {
    throw std::runtime_error("Failed to from socket " + err.message());
  } else {
    try {
      read_ring_.reserve(read_ring_.size() + bytes_transferred);
      for (int i = 0; i < bytes_transferred; i++) {
        read_ring_.push_back(read_buffer_[i]);
      }
      while (true) {
        if (!in_message_completed_) {
          auto message_size = last_message_size_;
          if (has_data(message_size + sizeof(uint8_t))) {
            uint8_t message_id;
            pop_data(&message_id, 1);
            std::vector<uint8_t> data(message_size);
            pop_data(data.data(), message_size);
            handle_message(message_size, message_id, data);
          } else {
            break;
          }
        } else if (handshake_completed_) {
          if (has_data(sizeof(uint32_t))) {
            auto message_size = read_size();
            if (has_data(message_size + sizeof(uint8_t))) {
              uint8_t message_type;
              pop_data(&message_type, 1);
              std::vector<uint8_t> data(message_size);
              pop_data(data.data(), message_size);
              handle_message(message_size, message_type, data);
            } else {
              last_message_size_ = message_size;
              in_message_completed_ = false;
              break;
            }
          } else {
            break;
          }
        } else {
          if (has_data(sizeof(Handshake))) {
            // pop front sizeof(Handshake)
            pop_data(&received_handshake_, sizeof(Handshake));
            std::stringstream ss(
            std::string((char *) &received_handshake_.sender_id,
                        sizeof(krpc::NodeID)));
            peer_id_ = krpc::NodeID::decode(ss);
            handshake_completed_ = true;
          } else {
            break;
          }
        }
      }
    } catch (const bencoding::InvalidBencoding &e) {
      LOG(error) << "parse BT handshake: Invalid bencoding: " << e.what();
      socket_.close();
      connected_ = false;
    }
    socket_.async_receive(
        boost::asio::buffer(read_buffer_.data(), read_buffer_.size()),
        0,
        boost::bind(
            &PeerConnection::handle_receive,
            this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
  }
}
void PeerConnection::send_metadata_request(int64_t piece) {
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
      boost::asio::buffer(make_extended(node)),
      0,
      [](const boost::system::error_code &err, size_t bytes_transferred) {
        if (err) {
          throw std::runtime_error("Failed to write to socket " + err.message());
        }
        LOG(info) << "written " << bytes_transferred;
      });

}

}