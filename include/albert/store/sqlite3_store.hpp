#pragma once

#include <albert/store/store.hpp>

struct sqlite3;

namespace albert::store {

class Sqlite3Store :public Store {
 public:
  Sqlite3Store(const std::string &path);
  ~Sqlite3Store();

  virtual void create(const std::string &key, const std::string &value) = 0;
  virtual void update(const std::string &key, const std::string &value) = 0;
  virtual std::string read(const std::string &key) const = 0;
 private:
  sqlite3 *db;
};
}