#pragma once
#include <array>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <sstream>
#include <exception>

#include <albert/bencode/bencoding.hpp>

namespace albert::krpc {
class Query;
}

namespace albert::dht {
struct Transaction {
  std::string id_;
  std::string method_name_;
  std::shared_ptr<krpc::Query> query_node_;
};

class TransactionError : std::runtime_error {
 public:
  explicit TransactionError(const std::string& s) :runtime_error(s) { }
};

class TransactionManager {
 public:
  void start(const std::function<void (Transaction &transaction)> &callback);
  void end(const std::string& id, const std::function<void (const Transaction &transaction)> &callback);
  bool has_transaction(const std::string &id) const {
    return transactions_.find(id) != transactions_.end();
  }
 private:
  std::map<std::string, Transaction> transactions_;
  uint64_t transaction_counter_;
  std::mutex lock_;
};

}