#pragma once

#include <albert/store/store.hpp>

struct sqlite3;

namespace albert::store {

class Sqlite3FailedToOpen :public std::runtime_error {
 public:
  Sqlite3FailedToOpen(const std::string &s) :runtime_error(s) { }
};

class Sqlite3OperationError :public std::runtime_error {
 public:
  Sqlite3OperationError(const std::string &s) :runtime_error(s) { }
};

class Sqlite3Store :public Store {
 public:
  Sqlite3Store(const std::string &path);
  ~Sqlite3Store() override = default;

  void create(const std::string &key, const std::string &value) override;
  void update(const std::string &key, const std::string &value) override { throw Sqlite3OperationError("wtf, do not call me"); };
  std::optional<std::string> read(const std::string &key) const override;
 private:
  sqlite3 *db_;
};
}