#pragma once

#include <string>
#include <stdexcept>

namespace albert::store {

class ItemExisted :public std::runtime_error {
 public:
  ItemExisted(const std::string &s) :runtime_error(s) { }
};

class Store {
 public:
  virtual ~Store() = default;

  virtual void create(const std::string &key, const std::string &value) = 0;
  virtual void update(const std::string &key, const std::string &value) = 0;
  virtual std::string read(const std::string &key) const = 0;
};

}