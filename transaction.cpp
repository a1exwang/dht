#include "transaction.hpp"

void transaction::TransactionManager::start(const std::function<void(Transaction &transaction)> &callback) {
  std::unique_lock<std::mutex> _(lock_);
  auto transaction_id_int = transaction_counter_++;
  std::stringstream ss;
  std::array<char, sizeof(transaction_id_int)> transaction_id_buf{};
  std::copy(
      (const char*)&transaction_id_int,
      (const char*)&transaction_id_int + sizeof(transaction_id_int),
      transaction_id_buf.begin()
  );
  std::string transaction_id(transaction_id_buf.data(), transaction_id_buf.size());
  if (transactions_.find(transaction_id) != transactions_.end()) {
    throw TransactionError("Transaction ID overflow");
  }
  Transaction transaction{};
  transaction.id_ = transaction_id;

  callback(transaction);
//    if (!transaction.query_node_) {
//      throw TransactionError("Transaction invalid start callback, query_node not set");
//    }

  transactions_[transaction_id] = transaction;
}
void transaction::TransactionManager::end(const std::string &id,
                                          const std::function<void(const Transaction &)> &callback) {
  std::unique_lock<std::mutex> _(lock_);
  if (this->transactions_.find(id) == this->transactions_.end()) {
    throw TransactionError("Transaction not found");
  }
  callback(this->transactions_.at(id));
  this->transactions_.erase(id);
}
