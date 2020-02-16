#include <albert/store/sqlite3_store.hpp>

#include <cassert>

#include <sstream>
#include <thread>
#include <random>

#include <sqlite3.h>
#include <chrono>
#include <functional>

#include <albert/log/log.hpp>

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

static int my_callback(void *user, int argc, char **argv, char **col_name) {
  auto real_callback = static_cast<const std::function<void(const std::string &key, const std::string &data)>*>(user);
  if (*real_callback) {
    std::string info_hash;
    std::string data;
    for (int i = 0; i < argc; i++) {
      if (col_name[i] == std::string("info_hash")) {
        info_hash = argv[i];
      } else if (col_name[i] == std::string("data")) {
        data = argv[i];
      }
    }
    (*real_callback)(info_hash, data);
  }
  return 0;
}

static void sqlite_retry(
    sqlite3* db,
    const std::string &sql,
    std::function<void(const std::string &key, const std::string &data)> callback,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
) {
  char *z_err_msg;
  auto t0 = std::chrono::high_resolution_clock::now();
  while (true) {
    auto rc = sqlite3_exec(db, sql.c_str(), my_callback, &callback, &z_err_msg);
    if (rc == SQLITE_LOCKED || rc == SQLITE_BUSY) {
      // continue
      if (std::chrono::high_resolution_clock::now() > t0 + timeout) {
        throw Sqlite3TimeoutError("Database locked, retried but time out: sql: " + sql);
      }
      std::random_device rng;
      // sleep for 2ms ~ 64ms
      auto us = rng() % 62 + 2;
      std::this_thread::sleep_for(std::chrono::milliseconds(us));
    }
    else if (rc != SQLITE_OK) {
      sqlite3_errcode(db);
      std::string s("get_empty_keys: when running '" + sql + "', code=" + std::to_string(rc) + ", " + sqlite3_errmsg(db));
      sqlite3_free(z_err_msg);
      throw Sqlite3OperationError(s);
    } else {
      return;
    }
  }
}

void Sqlite3Store::create(const std::string &key, const std::string &value) {
  std::stringstream ss;
  ss << "INSERT INTO torrents (info_hash, data) VALUES (" << escape_sql(key) << ", " << escape_sql(value) << ");";
  sqlite_retry(db_, ss.str(), nullptr);
}

std::optional<std::string> Sqlite3Store::read(const std::string &key) const {
  std::stringstream ss;
  ss << "SELECT info_hash, data FROM torrents WHERE info_hash = " << escape_sql(key) << ";";
  std::optional<std::string> result;
  sqlite_retry(db_, ss.str(), [&result](const std::string &key, const std::string &value) {
    result.emplace(value);
  });
  return result;
}
void Sqlite3Store::update(const std::string &key, const std::string &value) {
  std::stringstream ss;
  ss << "UPDATE torrents SET data = " + escape_sql(value) + " WHERE info_hash = " << escape_sql(key) << ";";
  auto sql = ss.str();

  sqlite_retry(db_, sql, nullptr);
}

std::vector<std::string> Sqlite3Store::get_empty_keys() const {
  std::vector<std::string> result;
  std::string sql = "SELECT info_hash FROM torrents WHERE data is null or data = '';";

  sqlite_retry(db_, sql, [&result](const std::string &key, const std::string &value) {
    result.push_back(key);
  });
  return result;
}

std::vector<std::string> Sqlite3Store::get_empty_keys(size_t offset, size_t limit) const {
  std::vector<std::string> result;
  std::string sql = "SELECT info_hash FROM torrents WHERE data is null or data = '' LIMIT " + std::to_string(offset) + "," + std::to_string(limit) + ";";

  sqlite_retry(db_, sql, [&result](const std::string &key, const std::string &value) {
    result.push_back(key);
  });
  return result;
}
size_t Sqlite3Store::memory_size() const {
  sqlite3_int64 value = 0, ignored = 0;
  auto ret = sqlite3_status64(SQLITE_STATUS_MEMORY_USED, &value, &ignored, 0);
  if (ret == SQLITE_OK) {
    return value;
  } else {
    LOG(error) << "Failed to get SQLITE memory used size: " << sqlite3_errstr(ret);
    return 0;
  }

};

}
