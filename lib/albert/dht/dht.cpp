#include <albert/dht/dht.hpp>

#include <albert/dht/config.hpp>
#include <albert/dht/transaction.hpp>
#include <albert/krpc/krpc.hpp>
#include <albert/log/log.hpp>

/**
 * class dht::DHT
 */

namespace albert::dht {
std::string DHT::create_query(std::shared_ptr<krpc::Query> query, RoutingTable *routing_table) {
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
krpc::NodeID DHT::parse_node_id(const std::string &s) {
  return krpc::NodeID::from_hex(s);
}
std::string DHT::create_response(const krpc::Response &query) {
  std::stringstream ss;
  query.encode(ss, bencoding::EncodeMode::Bencoding);
  return ss.str();
}
void DHT::add_routing_table(std::unique_ptr<RoutingTable> routing_table) {
  routing_tables_.push_front(std::move(routing_table));
}

}
