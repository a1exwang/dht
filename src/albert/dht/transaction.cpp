#include <albert/dht/transaction.hpp>

#include <list>
#include <iomanip>

#include <albert/log/log.hpp>

namespace albert::dht {
void TransactionManager::start(const std::function<void(Transaction &transaction)> &callback) {
  std::unique_lock<std::mutex> _(lock_);
  auto transaction_id_int = transaction_counter_++;
  std::stringstream ss;
  std::array<char, sizeof(transaction_id_int)> transaction_id_buf{};
  std::copy(
      (const char *) &transaction_id_int,
      (const char *) &transaction_id_int + sizeof(transaction_id_int),
      transaction_id_buf.begin()
  );
  std::string transaction_id(transaction_id_buf.data(), transaction_id_buf.size());
  if (transactions_.find(transaction_id) != transactions_.end()) {
    throw TransactionError("Transaction ID overflow");
  }
  Transaction transaction{};
  transaction.id_ = transaction_id;
  transaction.start_time_ = std::chrono::high_resolution_clock::now();

  callback(transaction);
  if (transaction.method_name_.empty()) {
    throw TransactionError("Transaction invalid start callback, method_name not set");
  }

  if (!transaction.query_node_) {
    throw TransactionError("Transaction invalid start callback, query_node not set");
  }

  transactions_[transaction_id] = transaction;
}
void TransactionManager::end(
    const std::string &id,
    const std::function<void(const Transaction &)> &callback) {
  std::unique_lock<std::mutex> _(lock_);
  if (this->transactions_.find(id) == this->transactions_.end()) {
    throw TransactionError("Transaction not found");
  }
  callback(this->transactions_.at(id));
  this->transactions_.erase(id);
}

void TransactionManager::gc() {
  auto t0 = std::chrono::high_resolution_clock::now();
  std::vector<std::string> to_delete;
  to_delete.reserve(transactions_.size());
  auto a = t0 - expiration_time_;
  for (auto &item : transactions_) {
    if (a > item.second.start_time_) {
      to_delete.push_back(item.first);
    }
  }
  for (auto &item : to_delete) {
    transactions_.erase(item);
  }

  auto t1 = std::chrono::high_resolution_clock::now();
  LOG(debug) << "TransactionManager: delete " << to_delete.size() << " expiried transactions in "
            << std::fixed << std::setprecision(2) << std::chrono::duration<double,std::milli>(t1-t0).count() << "ms";
}
bool TransactionManager::has_transaction(const std::string &id) const {
  return transactions_.find(id) != transactions_.end();
}
size_t TransactionManager::memory_size() const {
  size_t size = sizeof(*this);
  for (auto &item : transactions_) {
    size += sizeof(item.first) + sizeof(item.second) + item.first.size();
  }
  return size;
}
}
