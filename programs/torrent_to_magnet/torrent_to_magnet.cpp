#include <fstream>

#include <boost/algorithm/string.hpp>

#include <albert/u160/u160.hpp>
#include <albert/bencode/bencoding.hpp>

using namespace albert;

std::string torrent_to_magnet(const bencoding::DictNode &torrent) {
  auto info = bencoding::get<bencoding::DictNode>(torrent, "info");
  std::stringstream ss;
  info.encode(ss, bencoding::EncodeMode::Bencoding);
  auto s = ss.str();
  auto info_hash = u160::U160::hash((const uint8_t*)s.data(), s.size());
  return "magnet:?xt=urn:btih:" + info_hash.to_string();
}

void process(std::string file_path) {
  boost::algorithm::trim(file_path);
  std::ifstream ifs(file_path, std::ios::binary);
  if (!ifs) {
    throw std::invalid_argument("Invalid file path '" + file_path + "'");
  }
  auto node = bencoding::Node::decode(ifs);
  if (auto torrent = std::dynamic_pointer_cast<bencoding::DictNode>(node); !torrent) {
    throw std::runtime_error("Invalid torrent file '" + file_path + "', root node not a dict node");
  } else {
    std::cout << torrent_to_magnet(*torrent) << std::endl;
  }
}

void usage() {
  std::cout << "Usage ./torrent_to_magnet torrent0 torrent1 torrent2 ..." << std::endl;
}

int main(int argc, char **argv) {
  try {
    if (argc >= 2) {
      // args mode
      for (int i = 1; i < argc; i++) {
        std::string hex = argv[i];
        process(hex);
      }
    } else {
      // stdin mode
      while (std::cin) {
        std::string hex;
        std::getline(std::cin, hex);
        process(hex);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to parse info hash: " << e.what() << std::endl;
    usage();
  }
}
