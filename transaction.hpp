#pragma once

#include "bencoding.hpp"

#include <array>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <sstream>
#include <exception>

namespace transaction {


struct Transaction {
  std::string id_;
  std::string method_name_;
//  std::shared_ptr<bencoding::Node> query_node_;
};

class TransactionError : std::runtime_error {
 public:
  explicit TransactionError(const std::string& s) :runtime_error(s) { }
};

class TransactionManager {
 public:
  void start(const std::function<void (Transaction &transaction)> &callback);

  void end(const std::string& id, const std::function<void (const Transaction &transaction)> &callback);
 private:
  std::map<std::string, Transaction> transactions_;
  int64_t transaction_counter_;
  std::mutex lock_;
};

}