#include <albert/u160/u160.hpp>
#include <boost/algorithm/string.hpp>

using namespace albert;

std::string ih_to_magnet(const u160::U160 &info_hash) {
  return "magnet:?xt=urn:btih:" + info_hash.to_string();
}

void process(std::string hex) {
  boost::algorithm::trim(hex);
  std::cout << ih_to_magnet(u160::U160::from_hex(hex)) << std::endl;
}

void usage() {
  std::cout << "Usage ./ih2magnet ih0 ih1 ih2 ..." << std::endl
            << "or use interactive mode ./ih2magnet and enters info hashes line by line" << std::endl;

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