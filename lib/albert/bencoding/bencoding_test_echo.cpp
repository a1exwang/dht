#include "albert/bencode/bencoding.hpp"

#include <iostream>
#include <fstream>

int main() {
  auto root = albert::bencoding::Node::decode(std::cin);
  root->encode(std::cout);
  root->encode(std::cerr, albert::bencoding::EncodeMode::JSON);
}