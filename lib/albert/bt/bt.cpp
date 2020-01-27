#include <albert/bt/bt.hpp>

#include <string>
#include <sstream>

#include <boost/asio/ip/address_v4.hpp>

namespace albert::bt::peer {

std::string Peer::to_string() const {
  std::stringstream ss;
  ss << boost::asio::ip::address_v4(ip_) << ":" << port_;
  return ss.str();
}

}
