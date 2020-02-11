#include <albert/utp/utp.hpp>

#include <random>
#include <iostream>

#include <boost/bind/bind.hpp>
#include <boost/asio/placeholders.hpp>

#include <albert/log/log.hpp>
#include <albert/utils/utils.hpp>

namespace {

using namespace albert;
class PacketReader {
 public:
  PacketReader(const uint8_t *data, size_t size) : data_(data), size_(size), offset_(0) { }
  template <typename T>
  T read() {
    if (offset_ + sizeof(T) > size_) {
      throw std::overflow_error("T PacketReader::read(): overflow");
    }
    T t = *reinterpret_cast<const T*>(data_ + offset_);
    t = utils::network_to_host(t);
    offset_ += sizeof(T);
    return t;
  }

  template <typename T>
  void read(T &t) {
    if (offset_ + sizeof(T) > size_) {
      throw std::overflow_error("PacketReader::read(T): overflow");
    }
    t = *reinterpret_cast<const T*>(data_ + offset_);
    t = utils::network_to_host(t);
    offset_ += sizeof(T);
  }

  template <typename T>
  void read(T *output, size_t size) {
    if (offset_ + size > size_) {
      throw std::overflow_error("PacketReader::read(T*, size_t): overflow");
    }
    memcpy(output, data_ + offset_, size * sizeof(T));
    offset_ += size * sizeof(T);
  }

  size_t offset() const { return offset_; }
  void skip(size_t size) {
    if (offset_ + size > size_) {
      throw std::overflow_error("PacketReader::skip(" + std::to_string(size) + ") overflow");
    }
    offset_ += size;
  }

  operator bool() const {
    return offset_ < size_;
  }
 private:
  const uint8_t *data_;
  size_t size_;
  size_t offset_;
};

class PacketWriter {
 public:
  PacketWriter(uint8_t *data, size_t size) : data_(data), size_(size), offset_(0) { }
  template <typename T>
  void write(T t) {
    if (offset_ + sizeof(T) > size_) {
      throw std::overflow_error("T PacketWriter::write(): overflow");
    }
    *reinterpret_cast<T*>(data_ + offset_) = utils::host_to_network(t);
    offset_ += sizeof(T);
  }

  template <typename T>
  void write(const T *input, size_t size) {
    if (offset_ + size > size_) {
      throw std::overflow_error("PacketWrite::write(T*, size_t): overflow");
    }
    memcpy(data_ + offset_, input, size * sizeof(T));
    offset_ += size * sizeof(T);
  }

  size_t data_size() const { return offset_; }
//  void skip(size_t size) {
//    if (offset_ + size > size_) {
//      throw std::overflow_error("PacketWriter::skip(" + std::to_string(size) + ") overflow");
//    }
//    offset_ += size;
//  }

  operator bool() const {
    return offset_ < size_;
  }
 private:
  uint8_t *data_;
  size_t size_;
  size_t offset_;
};


}


namespace albert::utp {

size_t Packet::encode(const uint8_t *data, size_t size) {
  PacketWriter writer(udp_data->data(), udp_data->size());
  writer.write(static_cast<uint8_t>((type << 4u) | version));
  if (!extensions.empty()) {
    writer.write<uint8_t>(std::get<0>(extensions[0]));
  } else {
    writer.write<uint8_t>(0);
  }

  writer.write(connection_id);
  writer.write(timestamp_microseconds);
  writer.write(timestamp_difference_microseconds);
  writer.write(wnd_size);
  writer.write(seq_nr);
  writer.write(ack_nr);

  for (size_t i = 0; i < extensions.size(); i++) {
    if (i == extensions.size() - 1) {
      writer.write<uint8_t>(0);
    } else {
      writer.write<uint8_t>(std::get<0>(extensions[i+1]));
    }
    auto ext = std::get<1>(extensions[i]);
    writer.write(ext.data(), ext.size());
  }

  auto data_offset = writer.data_size();
  if (data && size) {
    writer.write(data, size);
  }
  this->utp_data = gsl::span<const uint8_t>(udp_data->data() + data_offset, size);
  return writer.data_size();
}
std::ostream &Packet::pretty(std::ostream &os) const {
  os << "type: " << int(type) << std::endl;
  os << "connection_id: " << connection_id << std::endl;
  os << "timestamp_us: " << timestamp_microseconds << std::endl;
  os << "timestamp_diff_us: " << timestamp_difference_microseconds << std::endl;
  os << "wnd_size: " << wnd_size << ", "
     << "seq: " << seq_nr << ", "
     << "ack: " << ack_nr << std::endl;

  for (size_t i = 0; i < extensions.size(); i++) {
    os << "extension: " << (int)std::get<0>(extensions[i]) << ", size: " << std::get<1>(extensions[i]).size() << std::endl;
  }

  os << utils::hexdump(utp_data.data(), utp_data.size(), true);
  return os;
}

Packet Packet::decode(Allocator::Buffer buffer, size_t size) {
  Packet pkt;
  pkt.udp_data = buffer;

  PacketReader reader(pkt.udp_data->data(), size);
  auto version_type = reader.read<uint8_t>();
  pkt.version = version_type & 0xf;
  if (pkt.version != UTPVersion) {
    throw InvalidHeader("Unknown uTP version: " + std::to_string(pkt.version));
  }
  pkt.type = (version_type >> 4u) & 0xf;
  auto ext = reader.read<uint8_t>();

  reader.read(pkt.connection_id);
  reader.read(pkt.timestamp_microseconds);
  reader.read(pkt.timestamp_difference_microseconds);
  reader.read(pkt.wnd_size);
  reader.read(pkt.seq_nr);
  reader.read(pkt.ack_nr);

  while (ext != 0 && reader) {
    auto ext_type = ext;
    ext = reader.read<uint8_t>();
    auto ext_size = reader.read<uint8_t>();
    std::string ext_data(ext_size, 0);
    reader.read(ext_data.data(), ext_data.size());
    pkt.extensions.emplace_back(ext_type, ext_data);
  }

  pkt.utp_data = gsl::span<const uint8_t>(pkt.udp_data->data()+reader.offset(), size - reader.offset());
  return std::move(pkt);
}
std::string Packet::pretty() const {
  std::stringstream ss;
  pretty(ss);
  return ss.str();
}

Socket::Socket(boost::asio::io_service &io,
               boost::asio::ip::udp::endpoint bind_ep)
    : io_(io), socket_(io, bind_ep), bind_ep_(bind_ep), timer_(io) {

  timer_.expires_at(timer_.expiry() + boost::asio::chrono::seconds(1));
  timer_.async_wait(boost::bind(&Socket::handle_timer, this, boost::asio::placeholders::error));
}

Socket::~Socket() {
  close();
}

void Socket::async_connect(
    boost::asio::ip::udp::endpoint ep,
    std::function<void(const boost::system::error_code &error)> handler) {

  if (connections_.find(ep) != connections_.end()) {
    throw InvalidStatus("Cannot connect when connection exists " + ep.address().to_string() + ":" + std::to_string(ep.port()));
  }

  continue_receive();

  auto c = std::make_shared<Connection>();
  c->ep = ep;
  c->connect_handler = std::move(handler);
  connections_.emplace(ep, c);
  send_syn(*c);
}


void Socket::async_receive(boost::asio::mutable_buffer buffer,
                           std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) {

  for (auto &c : connections_) {
    c.second->receive_handler = handler;
    c.second->user_buffer = buffer;
    poll_receive(c.second);
  }

}

void Socket::async_send(boost::asio::const_buffer buffer,
                        std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) {

  for (auto &item : connections_) {
    auto &c = item.second;
    if (c->status_ != Status::Connected) {
      throw InvalidStatus("Invalid status, cannot send in status " + std::to_string(int(c->status_)));
    }

    send_data(*c, buffer);
  }

}

void Socket::close(boost::asio::ip::udp::endpoint ep) {
  if (connections_.find(ep) != connections_.end()) {
    auto c = connections_.at(ep);
    c->status_ = Status::Closed;

    io_.post([socket = shared_from_this(), c]() {
      if (c->connect_handler) {
        c->connect_handler(boost::asio::error::eof);
      }
      if (c->receive_handler) {
        c->receive_handler(boost::asio::error::eof, 0);
      }
      socket->send_fin(*c);
    });
  }
}

void Socket::handle_receive_from(const boost::system::error_code &error, size_t size) {
  if (error) {
    if (error == boost::asio::error::operation_aborted) {
      return;
    } else {
      LOG(info) << "utp::Socket::handle_receive_from: " << error.message();
      close(receive_ep_);
    }
    return;
  }

  Packet packet;
  try {
    packet = Packet::decode(receive_buffer_, size);
    receive_buffer_ = nullptr;
//    LOG(debug) << "uTP: received packet from " << receive_ep_ << std::endl
//               << packet.pretty() << std::endl;
  } catch (const InvalidHeader &e) {
    LOG(error) << "uTP: invalid header received from " << receive_ep_;
    reset(receive_ep_);
    return;
  } catch (const std::overflow_error &e) {
    LOG(error) << "uTP: unexpected EOF while decoding packet: " << e.what();
    reset(receive_ep_);
    return;
  }

  auto connection_it = connections_.find(receive_ep_);
  if (connection_it == connections_.end()) {
    if (packet.type == UTPTypeSyn) {
      LOG(error) << "uTP: New connection, not implemented";
    } else {
      // ignore
      LOG(debug) << "uTP: Received packet from unknown connection: " << packet.type;
    }
  } else {
    auto &connection = connection_it->second;

    if (connection->status_ == Status::Closed ||
        connection->status_ == Status::Timeout ||
        connection->status_ == Status::Error) {

      LOG(debug) << "uTP: connection status: " << int(connection->status_) << " ignored packet";
      return;
    }

    if (connection->status_ == Status::SynSent) {
      if (packet.connection_id != connection->conn_id_recv) {
        LOG(error) << "Multiple connections with one ep not implemented, ignored";
      } else {
        if (packet.type == UTPTypeState) {
          connection->status_ = Status::Connected;
          // NOTE:
          // This "ack_nr = seq_nr - 1" is not written in BEP, but by capture packets from qbittorrent
          connection->ack_nr = packet.seq_nr - 1;

          if (connection->connect_handler) {
            connection->connect_handler(boost::system::error_code());
            connection->connect_handler = nullptr;
          }
          connection->reset_timeout_from_now();
        } else {
          LOG(error) << "Invalid status, closing connection";
          close(connection->ep);
          return;
        }
      }
    } else if (connection->status_ == Status::Connecting) {
      LOG(error) << "connection not implemented, ignored";
    } else if (connection->status_ == Status::Connected) {
      if (packet.type == UTPTypeData) {
        auto diff = static_cast<int16_t>(packet.seq_nr - connection->ack_nr);
        if (diff == 1) {
          connection->ack_nr = packet.seq_nr;
          // TODO: drop packets when we have too many buffered_received_packets
          connection->buffered_received_packets.emplace_back(packet.udp_data, packet.utp_data);
          poll_receive(connection);
//          send_state(*connection);
//          LOG(debug) << "uTP: received packet normal " << packet.seq_nr;
        } else if (diff < 1) {
          // duplicate packet
          send_state(*connection);
          LOG(warning) << "uTP: received packet duplicate " << packet.seq_nr;
        } else /*(diff > 1)*/ {
          // TODO use extension selective ack
          // NAK [ack_nr + 1, packet.seq_nr - 1]
          send_state(*connection);
          LOG(warning) << "uTP: received packet lost " << connection->ack_nr << " to " << packet.seq_nr - 1;
        }
      } else if (packet.type == UTPTypeFin) {
        poll_receive(connection);
        LOG(error) << "FIN received from " << receive_ep_ << " closing connection";
        close(connection->ep);
        return;
      } else if (packet.type == UTPTypeReset) {
        poll_receive(connection);
        LOG(error) << "RST received from " << receive_ep_ << " closing connection";
        reset(connection->ep);
        return;
      } else if (packet.type == UTPTypeState) {
        connection->acked = packet.ack_nr;
      } else {
        LOG(error) << "connected, not implemented type " << int(packet.type);
      }
      if (packet.ack_nr > connection->acked) {
        connection->acked = packet.ack_nr;
      }
    }

    connection->reset_timeout_from_now();
  }

  if (socket_.is_open()) {
//    LOG(info) << "Socket::handle_receive_from handle_receive_from handler";
    continue_receive();
  }
}

void Socket::handle_send_to(boost::asio::ip::udp::endpoint ep, Allocator::Buffer buf, const boost::system::error_code &error) {
  if (error) {
    LOG(error) << "utp::Socket failed to async_send_to " << ep << ", error: " << error.message();
  }
//  if (connections_.find(ep) == connections_.end()) {
//    LOG(debug) << "unknown handle_send_to ep " << ep << " success";
//  }
}

void Socket::handle_send_to_fin(boost::asio::ip::udp::endpoint ep, Allocator::Buffer buf, const boost::system::error_code &error) {
  if (error) {
    LOG(error) << "utp::Socket failed to async_send_to " << ep << ", error: " << error.message();
  }
  if (connections_.find(ep) != connections_.end()) {
    cleanup(ep);
  }
}

void Socket::handle_timer(const boost::system::error_code &error) {
  if (error) {
    LOG(error) << "Socket timer error " << error.message();
    return;
  }

  for (auto &c : connections_) {
    if (c.second->status_ == Status::Connected && std::chrono::high_resolution_clock::now() > c.second->timeout_at) {
      timeout(*c.second);
    }
  }

  timer_.expires_at(timer_.expiry() + boost::asio::chrono::seconds(1));
  timer_.async_wait(boost::bind(&Socket::handle_timer, this, boost::asio::placeholders::error));
}
void Socket::timeout(Connection &c) {
  LOG(debug) << "uTP: connection timeout, closing connection to " << c.ep;
  poll_receive(connections_[c.ep]);

  c.status_ = Status::Timeout;
  if (c.receive_handler) {
    c.receive_handler(boost::asio::error::timed_out, 0);
  }

  if (c.connect_handler) {
    c.connect_handler(boost::asio::error::timed_out);
  }
  if (c.receive_handler) {
    c.receive_handler(boost::asio::error::timed_out, 0);
  }
  cleanup(c.ep);
}

#include <sys/time.h>
namespace {
uint32_t get_usec() {
  struct timeval tv; struct timezone tz;
  if (gettimeofday(&tv, &tz) != 0) {
    throw SystemError(std::string("Failed to send_syn, error: ") + strerror(errno));
  }
  return static_cast<uint32_t>(tv.tv_sec*1000u*1000u + tv.tv_usec);
}
}

void Socket::send_syn(Connection &connection) {
  auto usec = get_usec();
  std::random_device rng;
  std::uniform_int_distribution<uint16_t> cid_rng;
  connection.conn_id_recv = cid_rng(rng);
  connection.conn_id_send = connection.conn_id_recv+1;
  connection.seq_nr = 1;
  connection.ack_nr = 0;

  Packet packet{
      UTPTypeSyn, UTPVersion, connection.conn_id_recv,
      usec, 0,
      (uint32_t)send_buffer_allocator_.buffer_size(),
      connection.seq_nr, connection.ack_nr};

  connection.seq_nr++;
  auto buf = send_buffer_allocator_.allocate();
  packet.udp_data = buf;
  auto size = packet.encode();

  LOG(debug) << "utp::Socket SYNC send to " << connection.ep << std::endl << packet.pretty();

  socket_.async_send_to(
      boost::asio::buffer(buf->data(), size),
      connection.ep,
      boost::bind(&Socket::handle_send_to, this, connection.ep, buf, boost::asio::placeholders::error()));
}

void Socket::send_data(Connection &c, boost::asio::const_buffer data_to_send) {
  auto usec = get_usec();
  // TODO: time diff
  Packet packet{
      UTPTypeData, UTPVersion, c.conn_id_send,
      usec, 0,
      (uint32_t)send_buffer_allocator_.buffer_size(),
      c.seq_nr, c.ack_nr};

  auto buf = send_buffer_allocator_.allocate();
  packet.udp_data = buf;
  auto size = packet.encode((const uint8_t*)data_to_send.data(), data_to_send.size());

  c.seq_nr++;

//  LOG(debug) << "utp::Socket send data to " << c.ep << std::endl << packet.pretty();

  socket_.async_send_to(
      boost::asio::buffer(buf->data(), size),
      c.ep,
      boost::bind(&Socket::handle_send_to, this, c.ep, buf, boost::asio::placeholders::error()));
}

void Socket::send_state(Connection &c) {
  auto usec = get_usec();
  // TODO: time diff
  Packet packet{
      UTPTypeState, UTPVersion, c.conn_id_send,
      usec, 0,
      (uint32_t)send_buffer_allocator_.buffer_size(),
      c.seq_nr, c.ack_nr};

  auto buf = send_buffer_allocator_.allocate();
  packet.udp_data = buf;
  auto size = packet.encode();
  // This is too slow
//  LOG(debug) << "utp::Socket send STATE to " << c.ep << std::endl << packet.pretty();

  socket_.async_send_to(
      boost::asio::buffer(buf->data(), size),
      c.ep,
      boost::bind(&Socket::handle_send_to, this, c.ep, buf, boost::asio::placeholders::error()));
}

void Socket::send_fin(Connection &c) {
  auto usec = get_usec();
  Packet packet{
      UTPTypeFin, UTPVersion, c.conn_id_send,
      usec, 0,
      (uint32_t)send_buffer_allocator_.buffer_size(),
      c.seq_nr, c.ack_nr};

  auto buf = send_buffer_allocator_.allocate();
  packet.udp_data = buf;
  auto size = packet.encode();

  LOG(debug) << "utp::Socket send FIN to " << c.ep << std::endl << packet.pretty();
  socket_.async_send_to(
      boost::asio::buffer(buf->data(), size),
      c.ep,
      boost::bind(&Socket::handle_send_to_fin, this, c.ep, buf, boost::asio::placeholders::error()));
}

void Socket::continue_receive() {
  auto buffer = receive_buffer_allocator_.allocate();
  receive_buffer_ = buffer;
  socket_.async_receive_from(
      boost::asio::buffer(receive_buffer_->data(), receive_buffer_->size()),
      receive_ep_,
      boost::bind(&Socket::handle_receive_from, shared_from_this(),
                  boost::asio::placeholders::error(),
                  boost::asio::placeholders::bytes_transferred));
}

void Socket::close() {
  std::list<boost::asio::ip::udp::endpoint> keys;
  for (auto &item : connections_) {
    keys.push_back(item.first);
  }
  for (const auto& key : keys) {
    close(key);
  }
}

void Socket::cleanup(boost::asio::ip::udp::endpoint ep) {
  connections_.erase(ep);
  if (connections_.empty()) {
    socket_.cancel();
    socket_.close();
  }
}

void Socket::reset(boost::asio::ip::udp::endpoint ep) {
  if (connections_.find(ep) != connections_.end()) {
    auto &c = connections_.at(ep);
    c->status_ = Status::Closed;

    if (c->connect_handler) {
      c->connect_handler(boost::asio::error::connection_reset);
    }
    if (c->receive_handler) {
      c->receive_handler(boost::asio::error::connection_reset, 0);
    }
    cleanup(ep);
  }
}

bool Socket::poll_receive(std::shared_ptr<Connection> c) {
  if (c->receive_handler && !c->buffered_received_packets.empty()) {
    bool user_buffer_full = false;
    // fill the user buffer as much as possible
    while (!c->buffered_received_packets.empty()) {
      auto &data = c->buffered_received_packets.front();
      size_t user_buffer_remaining = c->user_buffer.size() - c->user_buffer_data_size;
      if (user_buffer_remaining <= data.size()) {
        // user buffer full after copying
        std::copy(
            data.data(),
            data.data() + user_buffer_remaining,
            (uint8_t *) c->user_buffer.data() + c->user_buffer_data_size);
//        std::copy(
//            data.begin(),
//            std::next(data.begin(), user_buffer_remaining),
//            (uint8_t *) c->user_buffer.data() + user_buffer_remaining);
        c->user_buffer_data_size = c->user_buffer.size();
        data.skip_front(user_buffer_remaining);
        break;
      } else {
        // pop one packet
        std::copy(data.data(), data.data() + data.size(), (uint8_t *) c->user_buffer.data()+c->user_buffer_data_size);
        c->user_buffer_data_size += data.size();
        c->buffered_received_packets.pop_front();
      }
    }
    if (c->user_buffer_data_size > 0 || c->user_buffer.size() == 0) {
      io_.post([c, socket = shared_from_this()]() {
        if (c->user_buffer_data_size > 0 || c->user_buffer.size() == 0) {
          c->invoke_handler();
        }

        if (c->receive_handler && !c->buffered_received_packets.empty()) {
          socket->poll_receive(c);
        }
      });
    }
  }
  return true;
}

}
