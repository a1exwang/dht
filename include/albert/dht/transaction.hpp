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
namespace routing_table {
class RoutingTable;
}

struct Transaction {
  std::string id_;
  std::string method_name_;
  std::shared_ptr<krpc::Query> query_node_;
  routing_table::RoutingTable *routing_table_;
  std::chrono::high_resolution_clock::time_point start_time_;
};

class TransactionError : std::runtime_error {
 public:
  explicit TransactionError(const std::string& s) :runtime_error(s) { }
};

class TransactionManager {
 public:
  explicit TransactionManager(std::chrono::milliseconds expiration_time) :expiration_time_(expiration_time) { }
  void start(const std::function<void (Transaction &transaction)> &callback);
  void end(const std::string& id, const std::function<void (const Transaction &transaction)> &callback);
  bool has_transaction(const std::string &id) const;

  void gc();
  size_t memory_size() const;
  size_t size() const { return transactions_.size(); }
 private:
  std::map<std::string, Transaction> transactions_;
  uint64_t transaction_counter_ = 0;
  std::mutex lock_;

  std::chrono::milliseconds expiration_time_;
};

}