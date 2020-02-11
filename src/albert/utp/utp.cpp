#include <albert/utp/utp.hpp>

#include <random>
#include <iostream>

#include <boost/bind/bind.hpp>
#include <boost/asio/placeholders.hpp>

#include <albert/log/log.hpp>
#include <albert/utils/utils.hpp>


namespace albert::utp {

std::ostream &Packet::encode(std::ostream &os) const {
  os.put(static_cast<char>((type << 4u) | version));
  if (!extensions.empty()) {
    os.put(std::get<0>(extensions[0]));
  } else {
    os.put(0);
  }

  auto cid = utils::host_to_network(connection_id);
  os.write((char*)&cid, sizeof(cid));

  auto t = utils::host_to_network(timestamp_microseconds);
  os.write((char*)&t, sizeof(t));

  auto td = utils::host_to_network(timestamp_difference_microseconds);
  os.write((char*)&td, sizeof(td));

  auto wnd = utils::host_to_network(wnd_size);
  os.write((char*)&wnd, sizeof(wnd));

  auto seq = utils::host_to_network(seq_nr);
  os.write((char*)&seq, sizeof(seq));

  auto ack = utils::host_to_network(ack_nr);
  os.write((char*)&ack, sizeof(ack));

  for (size_t i = 0; i < extensions.size(); i++) {
    if (i == extensions.size() - 1) {
      os.put(0);
    } else {
      os.put(std::get<0>(extensions[i+1]));
    }
    auto ext = std::get<1>(extensions[i]);
    os.write(ext.data(), ext.size());
  }

  os.write((char*)data.data(), data.size());
  return os;
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

  os << utils::hexdump(data.data(), data.size(), true);
  return os;
}

Packet Packet::decode(std::istream &is) {
  Packet pkt;

  uint8_t version_type = is.get();
  pkt.version = version_type & 0xf;
  if (pkt.version != UTPVersion) {
    throw InvalidHeader("Unknown uTP version: " + std::to_string(pkt.version));
  }
  pkt.type = (version_type >> 4) & 0xf;
  uint8_t ext = is.get();

  is.read((char*)&pkt.connection_id, sizeof(pkt.connection_id));
  pkt.connection_id = utils::network_to_host(pkt.connection_id);

  is.read((char*)&pkt.timestamp_microseconds, sizeof(pkt.timestamp_microseconds));
  pkt.timestamp_microseconds = utils::network_to_host(pkt.timestamp_microseconds);

  is.read((char*)&pkt.timestamp_difference_microseconds, sizeof(pkt.timestamp_difference_microseconds));
  pkt.timestamp_difference_microseconds = utils::network_to_host(pkt.timestamp_difference_microseconds);

  is.read((char*)&pkt.wnd_size, sizeof(pkt.wnd_size));
  pkt.wnd_size = utils::network_to_host(pkt.wnd_size);

  is.read((char*)&pkt.seq_nr, sizeof(pkt.seq_nr));
  pkt.seq_nr = utils::network_to_host(pkt.seq_nr);

  is.read((char*)&pkt.ack_nr, sizeof(pkt.ack_nr));
  pkt.ack_nr = utils::network_to_host(pkt.ack_nr);

  while (ext != 0 && is) {
    auto ext_type = ext;
    ext = static_cast<uint8_t>(is.get());
    auto size = static_cast<uint8_t>(is.get());
    std::string ext_data(size, 0);
    is.read(ext_data.data(), ext_data.size());
    pkt.extensions.emplace_back(ext_type, ext_data);
  }

  if (!is) {
    throw InvalidHeader("Unexpected EOF while reading header");
  }

  while (is.peek() != EOF) {
    pkt.data.push_back(is.get());
  }
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

  setup();

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
    auto &c = connections_.at(ep);
    c->status_ = Status::Closed;

    if (c->connect_handler) {
      c->connect_handler(boost::asio::error::eof);
    }
    if (c->receive_handler) {
      c->receive_handler(boost::asio::error::eof, 0);
    }
    send_fin(*connections_.at(ep));
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

  std::stringstream ss(std::string((char*)receive_buffer_.data(), size));
  Packet packet;
  try {
    packet = Packet::decode(ss);
    LOG(debug) << "received packet from " << receive_ep_ << std::endl
               << packet.pretty() << std::endl;
  } catch (const InvalidHeader &e) {
    LOG(error) << "invalid header received from " << receive_ep_;
    reset(receive_ep_);
    return;
  }

  if (connections_.find(receive_ep_) == connections_.end()) {
    if (packet.type == UTPTypeFin) {
      // ignore
    } else {
      LOG(error) << "New connection, not implemented";
    }
  } else {
    auto &connection = connections_[this->receive_ep_];

    if (connection->status_ == Status::Closed) {
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
        auto diff = static_cast<int32_t>(packet.seq_nr - connection->ack_nr);
        if (diff == 1) {
          connection->ack_nr = packet.seq_nr;
          connection->buffered_received_packets.emplace_back(std::move(packet.data));
          poll_receive(connection);
          send_state(*connection);
        } else if (diff < 1) {
          // duplicate packet
          send_state(*connection);
        } else if (diff > 1) {
          // TODO use extension selective ack
          // NAK [ack_nr + 1, packet.seq_nr - 1]
          send_state(*connection);
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
    socket_.async_receive_from(
        boost::asio::buffer(receive_buffer_.data(), receive_buffer_.size()),
        receive_ep_,
        boost::bind(&Socket::handle_receive_from, shared_from_this(),
                    boost::asio::placeholders::error(),
                    boost::asio::placeholders::bytes_transferred));
  }
}

void Socket::handle_send_to(boost::asio::ip::udp::endpoint ep, const boost::system::error_code &error) {
  if (error) {
    LOG(error) << "utp::Socket failed to async_send_to " << ep << ", error: " << error.message();
  }
  if (connections_.find(ep) == connections_.end()) {
    LOG(debug) << "unknown handle_send_to ep " << ep << " success";
  }
}

void Socket::handle_send_to_fin(boost::asio::ip::udp::endpoint ep, const boost::system::error_code &error) {
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
  // TODO:
//  send_data(c, {});
  LOG(debug) << "Connection timeout, closing connection to " << c.ep;
  poll_receive(connections_[c.ep]);
  close(c.ep);
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
      static_cast<uint32_t>(receive_buffer_.size() - receive_buffer_offset_),
      connection.seq_nr, connection.ack_nr};

  connection.seq_nr++;

  std::stringstream ss;
  packet.encode(ss);
  LOG(debug) << "utp::Socket SYNC send to " << connection.ep << std::endl << packet.pretty();

  socket_.async_send_to(
      boost::asio::buffer(ss.str()),
      connection.ep,
      boost::bind(&Socket::handle_send_to, this, connection.ep, boost::asio::placeholders::error()));
}

void Socket::send_data(Connection &c, boost::asio::const_buffer data) {
  auto usec = get_usec();
  // TODO: time diff
  Packet pkg{
      UTPTypeData, UTPVersion, c.conn_id_send,
      usec, 0,
      static_cast<uint32_t>(receive_buffer_.size() - receive_buffer_offset_),
      c.seq_nr, c.ack_nr};

  pkg.data.resize(data.size());
  memcpy(pkg.data.data(), data.data(), data.size());

  c.seq_nr++;

  std::stringstream ss;
  pkg.encode(ss);

  LOG(debug) << "utp::Socket send data to " << c.ep << std::endl << pkg.pretty();

  socket_.async_send_to(
      boost::asio::buffer(ss.str()),
      c.ep,
      boost::bind(&Socket::handle_send_to, this, c.ep, boost::asio::placeholders::error()));
}

void Socket::send_state(Connection &c) {
  auto usec = get_usec();
  // TODO: time diff
  Packet packet{
      UTPTypeState, UTPVersion, c.conn_id_send,
      usec, 0,
      static_cast<uint32_t>(receive_buffer_.size() - receive_buffer_offset_),
      c.seq_nr, c.ack_nr};

  std::stringstream ss;
  packet.encode(ss);
  LOG(debug) << "utp::Socket send STATE to " << c.ep << std::endl << packet.pretty();

  socket_.async_send_to(
      boost::asio::buffer(ss.str()),
      c.ep,
      boost::bind(&Socket::handle_send_to, this, c.ep, boost::asio::placeholders::error()));
}

void Socket::send_fin(Connection &c) {
  auto usec = get_usec();
  Packet pkg{
      UTPTypeFin, UTPVersion, c.conn_id_send,
      usec, 0,
      static_cast<uint32_t>(receive_buffer_.size() - receive_buffer_offset_),
      c.seq_nr, c.ack_nr};

  std::stringstream ss;
  pkg.encode(ss);

  LOG(debug) << "utp::Socket send FIN to " << c.ep << std::endl << pkg.pretty();
  socket_.async_send_to(
      boost::asio::buffer(ss.str()),
      c.ep,
      boost::bind(&Socket::handle_send_to_fin, this, c.ep, boost::asio::placeholders::error()));
}

void Socket::setup() {
//  LOG(debug) << "Socket::setup handle_receive_from handler";
  socket_.async_receive_from(
      boost::asio::buffer(receive_buffer_.data(), receive_buffer_.size()),
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
  bool handler_called = false;
  bool data_exhausted = false;

  while (c->receive_handler && !data_exhausted) {
    bool user_buffer_full = false;
    // fill the user buffer as much as possible
    while (true) {
      if (c->buffered_received_packets.empty()) {
        data_exhausted = true;
        break;
      } else {
        auto &data = c->buffered_received_packets.front();
        size_t user_buffer_remaining = c->user_buffer.size() - c->user_buffer_data_size;
        if (user_buffer_remaining <= data.size()) {
          // receive buffer full
          std::copy(
              data.begin(),
              std::next(data.begin(), user_buffer_remaining),
              (uint8_t *) c->user_buffer.data() + user_buffer_remaining);
          c->user_buffer_data_size = c->user_buffer.size();
          user_buffer_full = true;
          data = std::vector<uint8_t>(std::next(data.begin(), user_buffer_remaining), data.end());
          break;
        } else {
          // pop one packet
          std::copy(data.begin(), data.end(), (uint8_t *) c->user_buffer.data()+c->user_buffer_data_size);
          c->user_buffer_data_size += data.size();
          c->buffered_received_packets.pop_front();
        }
      }
    }
    // if user_buffer.size() == 0 && data.size() == 0, we call the user handle once
    if (c->user_buffer_data_size > 0 || user_buffer_full) {
      auto handler = std::move(c->receive_handler);
      c->receive_handler = nullptr;
      auto bytes_transfered = c->user_buffer_data_size;
      c->user_buffer_data_size = 0;
      handler(boost::system::error_code(), bytes_transfered);
      handler_called = true;
    }
  }
  return handler_called;
}

}
