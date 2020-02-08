#include <albert/bt/bt.hpp>

#include <string>

#include <boost/asio/ip/address_v4.hpp>

#include <albert/bt/peer_connection.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>


int main(int argc, char **argv) {
  boost::asio::io_context io_context;
  auto self = albert::u160::U160::random();
  albert::log::initialize_logger(false);

  if (argc != 4) {
    LOG(info) << "Invalid arguments";
    exit(1);
  }

  auto target = albert::u160::U160::from_hex(argv[1]);
  auto ip = boost::asio::ip::address_v4::from_string(argv[2]).to_uint();
  auto port = (uint16_t)atoi(argv[3]);

  albert::bt::peer::PeerConnection pc(
      io_context,
      self,
      target,
      0,
      0,
      ip,
      port,
      true);


  try {
    pc.connect();
    io_context.run();
    LOG(info) << "Successfully end";
  } catch (std::exception &e) {
    LOG(error) << "Exception: " << e.what();
    std::exit(EXIT_FAILURE);
  }
}
