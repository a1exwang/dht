#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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
  virtual std::optional<std::string> read(const std::string &key) const = 0;
  virtual std::vector<std::string> get_empty_keys() const = 0;
  virtual std::vector<std::string> get_empty_keys(size_t offset, size_t limit) const = 0;
  virtual size_t memory_size() const = 0;
};

}