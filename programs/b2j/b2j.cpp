#include <albert/bencode/bencoding.hpp>

int main() {
  albert::bencoding::Node::decode(std::cin)->encode(std::cout, albert::bencoding::EncodeMode::JSON, 0);
  return 0;
}