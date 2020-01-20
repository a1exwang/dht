#include "bencoding.hpp"

#include <iostream>
#include <fstream>

int main() {
  auto root = bencoding::Node::decode(std::cin);
  root->encode(std::cout);
}