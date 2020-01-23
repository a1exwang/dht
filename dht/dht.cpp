#include <dht/dht.hpp>

#include <krpc/krpc.hpp>
#include <utils/log.hpp>
#include <dht/transaction.hpp>

/**
 * class dht::DHT
 */

namespace dht {
std::string DHT::create_query(std::shared_ptr<krpc::Query> query) {
  transaction_manager.start([&query, this](Transaction &transaction) {
    transaction.method_name_ = query->method_name();
    transaction.query_node_ = query;
    query->set_transaction_id(transaction.id_);
  });
  std::stringstream ss;
  query->encode(ss, bencoding::EncodeMode::Bencoding);
  return ss.str();
}
krpc::NodeID DHT::parse_node_id(const std::string &s) {
  if (s.empty()) {
    return krpc::NodeID::random();
  } else {
    return krpc::NodeID::from_string(s);
  }
}
std::string DHT::create_response(const krpc::Response &query) {
  std::stringstream ss;
  query.encode(ss, bencoding::EncodeMode::Bencoding);
  return ss.str();
}
void DHT::got_info_hash(const krpc::NodeID &info_hash) {
  info_hash_list_stream_ << info_hash.to_string() << std::endl;
  info_hash_list_stream_.flush();
  if (!info_hash_list_stream_) {
    LOG(error) << "Failed to write to info hash list file '" << config_.info_hash_save_path << "'";
  }
}

}
