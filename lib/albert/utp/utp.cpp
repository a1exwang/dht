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
}

void Socket::async_connect(
    boost::asio::ip::udp::endpoint ep,
    std::function<void(const boost::system::error_code &error)> handler) {

  if (connections_.find(ep) != connections_.end()) {
    throw InvalidStatus("Cannot connect when connection exists " + ep.address().to_string() + ":" + std::to_string(ep.port()));
  }

  setup();

  Connection c;
  c.ep = ep;
  c.connect_handler = std::move(handler);
  connections_.emplace(ep, c);
  send_syn(connections_[ep]);
}

void Socket::handle_receive_from(const boost::system::error_code &error, size_t size) {
  if (error) {
    LOG(info) << "utp::Socket::handle_receive_from: " << error.message();
    if (error == boost::asio::error::operation_aborted) {
      return;
    } else {
      close();
    }
    return;
  }

  if (connections_.find(receive_ep_) == connections_.end()) {
    // new connection
    LOG(error) << "New connection, not implemented";
  } else {
    auto &connection = connections_[this->receive_ep_];

    if (connection.status_ == Status::Closed) {
      return;
    }

    std::stringstream ss(std::string((char*)receive_buffer_.data(), size));
    auto packet = Packet::decode(ss);
    LOG(info) <<"received packet from " << receive_ep_ << std::endl
              << packet.pretty() << std::endl;

    if (connection.status_ == Status::SynSent) {
      if (packet.connection_id != connection.conn_id_recv) {
        LOG(error) << "Multiple connections with one ep not implemented, ignored";
      } else {
        if (packet.type == UTPTypeState) {
          connection.status_ = Status::Connected;
          connection.ack_nr = packet.seq_nr - 1;
//          send_state(connection);
          LOG(info) << "Connected ack_nr: " << packet.seq_nr;

          if (connection.connect_handler) {
            connection.connect_handler(boost::system::error_code());
            connection.connect_handler = nullptr;
          }
        } else {
          LOG(error) << "Invalid status, closing connection";
          close();
          return;
        }
      }
    } else if (connection.status_ == Status::Connecting) {
      LOG(error) << "connection not implemented, ignored";
    } else if (connection.status_ == Status::Connected) {
      if (packet.type == UTPTypeData) {
        connection.ack_nr = packet.seq_nr;
        connection.buffered_received_packets.emplace_back(std::move(packet.data));
        poll_receive(connection);
        send_state(connection);
      } else if (packet.type == UTPTypeFin) {
        poll_receive(connection);
        if (connection.receive_handler) {
          connection.receive_handler(boost::asio::error::eof, 0);
        }
        connection.status_ = Status::Closed;
        close();
        return;
      } else {
        LOG(error) << "connected, not implemented type " << int(packet.type);
      }
    }

    connection.timeout_at = std::chrono::high_resolution_clock::now() + std::chrono::seconds(1);
  }


  socket_.async_receive_from(
      boost::asio::buffer(receive_buffer_.data(), receive_buffer_.size()),
      receive_ep_,
      boost::bind(&Socket::handle_receive_from, shared_from_this(),
                  boost::asio::placeholders::error(),
                  boost::asio::placeholders::bytes_transferred));
}

void Socket::close() {
  // TODO:
  socket_.cancel();
  socket_.close();
}
void Socket::handle_send_to(boost::asio::ip::udp::endpoint ep, const boost::system::error_code &error) {
  if (error) {
    LOG(error) << "utp::Socket failed to async_send_to " << ep << ", error: " << error.message();
  }
  if (connections_.find(ep) == connections_.end()) {
    LOG(error) << "unknown handle_send_to ep " << ep;
  }
}
void Socket::setup() {
  socket_.async_receive_from(
      boost::asio::buffer(receive_buffer_.data(), receive_buffer_.size()),
      receive_ep_,
      boost::bind(&Socket::handle_receive_from, shared_from_this(),
                  boost::asio::placeholders::error(),
                  boost::asio::placeholders::bytes_transferred));

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
  LOG(info) << "utp::Socket send to " << connection.ep << std::endl << packet.pretty();

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

  LOG(info) << "utp::Socket send to " << c.ep << std::endl << pkg.pretty();

  socket_.async_send_to(
      boost::asio::buffer(ss.str()),
      c.ep,
      boost::bind(&Socket::handle_send_to, this, c.ep, boost::asio::placeholders::error()));
}

void Socket::async_receive(boost::asio::mutable_buffer buffer,
                           std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) {

  for (auto &c : connections_) {
    c.second.receive_handler = handler;
    c.second.receive_buffer = buffer;
    poll_receive(c.second);
  }

}
bool Socket::poll_receive(Connection &c) {
  bool handler_called = false;
  while (!c.buffered_received_packets.empty() && c.receive_handler) {
    if (c.receive_buffer_data_size > 0) {
      // do nothing, poll success
    } else {
      auto &data = c.buffered_received_packets.front();
      if (c.receive_buffer.size() < data.size()) {
        throw SystemError("receive buffer not big enough, " + std::to_string(c.receive_buffer.size()) + " of "
                              + std::to_string(data.size()));
      }
      c.receive_buffer_data_size = data.size();
      std::copy(data.begin(), data.end(), (uint8_t *) c.receive_buffer.data());
      c.buffered_received_packets.pop_front();
    }
    c.receive_handler(boost::system::error_code(), c.receive_buffer_data_size);
    c.receive_handler = nullptr;
    c.receive_buffer_data_size = 0;
    handler_called = true;
  }
  return handler_called;
}
void Socket::handle_timer(const boost::system::error_code &error) {
  if (error) {
    LOG(error) << "Socket timer error " << error.message();
    return;
  }

  for (auto &c : connections_) {
    if (std::chrono::high_resolution_clock::now() > c.second.timeout_at) {
      timeout(c.second);
    }
  }

  timer_.expires_at(timer_.expiry() + boost::asio::chrono::seconds(1));
  timer_.async_wait(boost::bind(&Socket::handle_timer, this, boost::asio::placeholders::error));
}
void Socket::timeout(Connection &c) {
  // TODO:
  send_data(c, {});
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
  LOG(info) << "utp::Socket send to " << c.ep << std::endl << packet.pretty();

  socket_.async_send_to(
      boost::asio::buffer(ss.str()),
      c.ep,
      boost::bind(&Socket::handle_send_to, this, c.ep, boost::asio::placeholders::error()));
}
void Socket::async_send(boost::asio::const_buffer buffer,
                        std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handler) {

  for (auto &item : connections_) {
    auto &c = item.second;
    if (c.status_ != Status::Connected) {
      throw InvalidStatus("Invalid status, cannot send in status " + std::to_string(int(c.status_)));
    }

    send_data(c, buffer);
  }

}

}
