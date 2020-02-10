#include <albert/dht/dht.hpp>

#include <albert/dht/routing_table/routing_table.hpp>
#include <albert/dht/config.hpp>
#include <albert/dht/transaction.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>

/**
 * class dht::DHT
 */

namespace albert::dht {
std::string DHT::create_query(std::shared_ptr<krpc::Query> query, routing_table::RoutingTable *routing_table) {
  transaction_manager.start([query, routing_table, this](Transaction &transaction) {
    transaction.method_name_ = query->method_name();
    transaction.query_node_ = query;
    transaction.routing_table_ = routing_table;
    query->set_transaction_id(transaction.id_);
  });
  std::stringstream ss;
  query->encode(ss, bencoding::EncodeMode::Bencoding);
  return ss.str();
}
std::string DHT::create_response(const krpc::Response &query) {
  std::stringstream ss;
  query.encode(ss, bencoding::EncodeMode::Bencoding);
  return ss.str();
}
void DHT::add_routing_table(std::unique_ptr<routing_table::RoutingTable> routing_table) {
  routing_tables_.push_front(std::move(routing_table));
}
bool DHT::in_black_list(uint32_t ip, uint16_t port) const {
  return black_list_.find({ip, port}) != black_list_.end();
}
bool DHT::add_to_black_list(uint32_t ip, uint16_t port) {
  for (auto &rt : routing_tables_) {
    rt->make_bad(ip, port);
  }
  return this->black_list_.insert({ip, port}).second;
}

}
