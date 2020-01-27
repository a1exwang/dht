#include <bt/bt.hpp>

#include <string>

#include <boost/asio/ip/address_v4.hpp>

#include <bt/peer_connection.hpp>
#include <krpc/krpc.hpp>
#include <utils/log.hpp>

int main(int argc, char **argv) {
  boost::asio::io_context io_context;
  auto self = krpc::NodeID::random();
  dht::log::initialize_logger(false);

  if (argc != 4) {
    LOG(info) << "Invalid arguments";
    exit(1);
  }

  auto target = krpc::NodeID::from_hex(argv[1]);
  auto ip = boost::asio::ip::address_v4::from_string(argv[2]).to_uint();
  auto port = (uint16_t)atoi(argv[3]);

  bt::peer::PeerConnection pc(
      io_context,
      self,
      target,
      ip,
      port);


  try {
    pc.connect();
    io_context.run();
    LOG(info) << "Successfully end";
  } catch (std::exception &e) {
    LOG(error) << "Exception: " << e.what();
    std::exit(EXIT_FAILURE);
  }
}
