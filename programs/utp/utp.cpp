
#include <vector>

#include <albert/log/log.hpp>
#include <albert/utp/utp.hpp>
#include <albert/utils/utils.hpp>
#include <albert/u160/u160.hpp>
#include <albert/bt/peer_connection.hpp>
using namespace albert;


int main() {

  boost::asio::io_service io;
  auto socket = std::make_shared<albert::utp::Socket>(
      io,
      boost::asio::ip::udp::endpoint()
  );

  auto send_handshake = [&socket]() {
    albert::bt::peer::Handshake handshake;
    albert::u160::U160 self = albert::u160::U160::random();
    albert::u160::U160 target = albert::u160::U160::from_hex("207674362039a82f6c1abd25e75c687dfc5f41bd");
    {
      std::stringstream ss;
      self.encode(ss);
      auto s = ss.str();
      if (s.size() != albert::u160::U160Length) {
        throw std::runtime_error("self_ Invalid node id length, s.size() != NodeIDLength");
      }
      memcpy(handshake.sender_id, s.data(), albert::u160::U160Length);
    }
    {
      std::stringstream ss;
      target.encode(ss);
      auto s = ss.str();
      if (s.size() != albert::u160::U160Length) {
        throw std::runtime_error("target_ Invalid node id length, s.size() != NodeIDLength");
      }
      memcpy(handshake.info_hash, s.data(), u160::U160Length);
    }

    size_t write_size = sizeof(handshake);
    std::vector<uint8_t> write_buffer(write_size);
    std::copy(
        (char*)&handshake,
        (char*)&handshake + sizeof(handshake),
        write_buffer.begin());
    socket->async_send(
        boost::asio::buffer(write_buffer.data(), write_size),
        [](const boost::system::error_code &err, size_t bytes_transferred) {
          if (err) {
            throw std::runtime_error("Failed to write to socket " + err.message());
          }
        });


//  auto m = bdict();
//  for (auto &item : extended_message_id_) {
//    m[item.second] = std::make_shared<bencoding::IntNode>(item.first);
//  }
//  // extended handshake
//  bencoding::DictNode node(
//      bdict({
//                {
//                    "m", newdict(m)
//                },
//                {"p", newint(6881)},
//                {"reqq", newint(500)},
//                {"v", std::make_shared<bencoding::StringNode>("wtf/0.0")}
//            }));
//  socket_.async_send(
//      boost::asio::buffer(make_extended(node, 0)),
//      0,
//      [](const boost::system::error_code &err, size_t bytes_transferred) {
//        if (err) {
//          throw std::runtime_error("Failed to write to socket " + err.message());
//        }
//      });
  };

  std::vector<uint8_t> receive_buffer(1048576);
  std::function<void(const boost::system::error_code &error, size_t bytes_transfered)> handle_receive =
      [&receive_buffer, &handle_receive, &socket](const boost::system::error_code &error, size_t bytes_transfered){
        if (error) {
          LOG(error) << "error when handling receive " << error.message();
          return;
        }
        LOG(info) << "received data: " << std::endl << albert::utils::hexdump(receive_buffer.data(), bytes_transfered, true);

        socket->async_receive(boost::asio::buffer(receive_buffer.data(), receive_buffer.size()), handle_receive);
      };

  socket->async_connect(
      boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::from_string("127.0.0.1"), 7001),
      [&](const boost::system::error_code &code) {
        socket->async_receive(boost::asio::buffer(receive_buffer.data(), receive_buffer.size()), handle_receive);
        LOG(info) << "connected";
        send_handshake();
      });

  io.run();
  return 0;
}