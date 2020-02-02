#include <albert/store/sqlite3_store.hpp>

#include <sstream>
#include <cassert>

#include <sqlite3.h>

namespace albert::store {

std::string escape_sql(const std::string &s) {
  return "'" + s + "'";
}

Sqlite3Store::Sqlite3Store(const std::string &path) {
  auto rc = sqlite3_open(path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    throw Sqlite3FailedToOpen(sqlite3_errmsg(db_));
  }
}

void Sqlite3Store::create(const std::string &key, const std::string &value) {
  std::stringstream ss;
  char *z_err_msg = nullptr;
  ss << "INSERT INTO torrents (info_hash, data) VALUES (" << escape_sql(key) << ", " << escape_sql(value) << ");";

  auto sql = ss.str();
  auto rc = sqlite3_exec(db_, sql.c_str(), nullptr, 0, &z_err_msg);
  if (rc != SQLITE_OK) {
    std::string s("when running '" + ss.str() + "'" + sqlite3_errmsg(db_));
    sqlite3_free(z_err_msg);
    throw Sqlite3OperationError(s);
  }
}

namespace {
static int callback(void *user, int argc, char **argv, char **col_name) {
  assert(argc == 2);

  auto result = static_cast<std::optional<std::string> *>(user);
  (*result) = std::string(argv[1]);
  return 0;
}
}

std::optional<std::string> Sqlite3Store::read(const std::string &key) const {
  std::stringstream ss;
  char *z_err_msg;
  ss << "SELECT info_hash, data FROM torrents WHERE info_hash = " << escape_sql(key) << ";";
  auto sql = ss.str();
  std::optional<std::string> result;
  auto rc = sqlite3_exec(db_, sql.c_str(), callback, &result, &z_err_msg);
  if (rc != SQLITE_OK) {
    std::string s("when running '" + ss.str() + "'" + sqlite3_errmsg(db_));
    sqlite3_free(z_err_msg);
    throw Sqlite3OperationError(s);
  }
  return result;
};

}
