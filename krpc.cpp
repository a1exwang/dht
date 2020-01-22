#include "log.hpp"
#include "krpc.hpp"

#include <algorithm>
#include <utility>
#include <sstream>
#include <random>

namespace krpc {

NodeID NodeID::random() {
  std::random_device device;
  std::mt19937 rng{std::random_device()()};
  NodeID ret{};
  std::generate(ret.data_.begin(), ret.data_.end(), rng);
  return ret;
}
std::string NodeID::to_string() const {
  std::stringstream ss;
  for (auto c : data_) {
    ss << std::hex << std::setfill('0') << std::setw(2) << (uint32_t)(uint8_t)c;
  }
  return ss.str();
}
NodeID NodeID::pow2m1(size_t r) {
  assert(r > 0 && r <= NodeIDBits);
  // NodeIDLength - ceil(r/8)
  size_t index = NodeIDLength - ((r-1) / 8 + 1);
  size_t bit = (r % 8 == 0) ? 8 : (r % 8);
  NodeID ret{};
  ret.data_[index] = (1u << bit) - 1;
  for (size_t i = index+1; i < NodeIDLength; i++) {
    ret.data_[i] = 0xffu;
  }
  return ret;
}
NodeID NodeID::pow2(size_t r) {
  assert(r >= 0 && r < NodeIDBits);
  size_t index = NodeIDLength - 1 - r / 8;
  size_t bit = r % 8;
  NodeID ret{};
  ret.data_[index] = 1u << bit;
  return ret;
}
NodeID NodeID::from_string(std::string s) {
  NodeID ret{};
  if (s.size() != NodeIDLength) {
    throw InvalidMessage("NodeID is not NodeIDLength long");
  }
  std::copy(s.begin(), s.end(), ret.data_.begin());
  return ret;
}
void NodeID::encode(std::ostream &os) const {
  os.write((const char*)data_.data(), data_.size());
}
bool NodeID::operator<=(const NodeID &rhs) const {
  return *this < rhs || *this == rhs;
}
bool NodeID::operator==(const NodeID &rhs) const {
  return !(*this < rhs) && !(rhs < *this);
}
bool NodeID::operator<(const NodeID &rhs) const {
  // data_ is big endian so we can use lexicographical_compare
  return std::lexicographical_compare(data_.begin(), data_.end(), rhs.data_.begin(), rhs.data_.end());
}
NodeID NodeID::operator&(const NodeID &rhs) const {
  NodeID ret{};
  for (int i = 0; i < data_.size(); i++) {
    ret.data_[i] = data_[i] & rhs.data_[i];
  }
  return ret;
}
NodeID NodeID::operator|(const NodeID &rhs) const {
  NodeID ret{};
  for (int i = 0; i < data_.size(); i++) {
    ret.data_[i] = data_[i] | rhs.data_[i];
  }
  return ret;
}
NodeID NodeID::operator^(const NodeID &rhs) const {
  NodeID ret{};
  for (int i = 0; i < data_.size(); i++) {
    ret.data_[i] = data_[i] ^ rhs.data_[i];
  }
  return ret;
}
uint8_t NodeID::bit(size_t r) const {
  assert(r >= 0 && r < NodeIDBits);
  // NodeIDLength - ceil(r/8)
  size_t index = NodeIDLength - 1 - r / 8;
  size_t bit = r % 8;
  return (data_[index] >> bit) & 1u;
}

void krpc::Message::build_bencoding_node(std::map<std::string, std::shared_ptr<bencoding::Node>> &dict) const {
  dict["t"] = std::make_shared<bencoding::StringNode>(transaction_id_);
  dict["y"] = std::make_shared<bencoding::StringNode>(type_);
  dict["v"] = std::make_shared<bencoding::StringNode>(client_version_);
}
void krpc::Query::encode(std::ostream &os, bencoding::EncodeMode mode) {
  std::map<std::string, std::shared_ptr<bencoding::Node>> dict;
  dict["q"] = std::make_shared<bencoding::StringNode>(method_name_);
  dict["a"] = get_arguments_node();

  build_bencoding_node(dict);

  auto root = bencoding::DictNode(dict);
  root.encode(os, mode);
}

static int64_t get_int64_or_throw(
    const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict,
    const std::string &key,
    const std::string &context) {
  if (dict.find(key) == dict.end()) {
    throw InvalidMessage(context + ", '" + key + "' not found");
  }
  auto node = std::dynamic_pointer_cast<bencoding::IntNode>(dict.at(key));
  if (!node) {
    throw InvalidMessage(context + ", '" + key + "' is not a string");
  }
  return *node;

}
static std::string get_string_or_throw(const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict, const std::string &key, const std::string &context) {
  if (dict.find(key) == dict.end()) {
    throw InvalidMessage(context + ", '" + key + "' not found");
  }
  auto node = std::dynamic_pointer_cast<bencoding::StringNode>(dict.at(key));
  if (!node) {
    throw InvalidMessage(context + ", '" + key + "' is not a string");
  }
  return *node;
}
static const bencoding::DictNode &get_dict_or_throw(const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict, const std::string &key, const std::string &context) {
  if (dict.find(key) == dict.end()) {
    throw InvalidMessage(context + ", '" + key + "' not found");
  }
  auto node = std::dynamic_pointer_cast<bencoding::DictNode>(dict.at(key));
  if (!node) {
    throw InvalidMessage(context + ", '" + key + "' is not a dict");
  }
  return *node;
}

static std::string get_string_or_empty(const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict, const std::string &key, const std::string &context) {
  if (dict.find(key) == dict.end()) {
    return "";
  }
  auto node = std::dynamic_pointer_cast<bencoding::StringNode>(dict.at(key));
  if (!node) {
    return "";
  }
  return *node;
}

std::shared_ptr<Message> krpc::Query::decode(
    const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict,
    const std::string& t,
    const std::string& v
    ) {
  std::string q = get_string_or_throw(dict, "q", "Query");
  auto a_dict = get_dict_or_throw(dict, "a", "Query");
  if (q == MethodNamePing) {
    auto node_id = NodeID::from_string(
        get_string_or_throw(a_dict.dict(), "id", "Query"));
    return std::make_shared<PingQuery>(t, v, node_id);
  } else if (q == MethodNameFindNode) {
    auto sender_id = get_string_or_throw(a_dict.dict(), "id", "FindNodeQuery");
    auto target_id = get_string_or_throw(a_dict.dict(), "target", "FindNodeQuery");
    return std::make_shared<FindNodeQuery>(t, v, NodeID::from_string(sender_id), NodeID::from_string(target_id));
  } else if (q == MethodNameGetPeers) {
    auto sender_id = get_string_or_throw(a_dict.dict(), "id", "GetPeersQuery");
    auto info_hash = get_string_or_throw(a_dict.dict(), "info_hash", "GetPeersQuery");
    return std::make_shared<GetPeersQuery>(t, v, NodeID::from_string(sender_id), NodeID::from_string(info_hash));
  } else if (q == MethodNameAnnouncePeer) {
    auto sender_id = get_string_or_throw(a_dict.dict(), "id", "AnnouncePeerQuery");
    bool implied_port = false;
    if (auto map = a_dict.dict(); map.find("implied_port") != map.end()) {
      implied_port = get_int64_or_throw(map, "implied_port", "AnnouncePeerQuery");
    }
    auto info_hash = get_string_or_throw(a_dict.dict(), "info_hash", "AnnouncePeerQuery");
    auto port = get_int64_or_throw(a_dict.dict(), "port", "AnnouncePeerQuery");
    auto token = get_string_or_throw(a_dict.dict(), "token", "AnnouncePeerQuery");
    return std::make_shared<AnnouncePeerQuery>(
        t,
        v,
        NodeID::from_string(sender_id),
        implied_port,
        NodeID::from_string(info_hash),
        port,
        token);
  } else {
    throw InvalidMessage("Query, Unknown method name '" + q + "'");
  }
}

std::shared_ptr<bencoding::Node> krpc::PingResponse::get_response_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
  std::stringstream ss;
  node_id_.encode(ss);
  arguments_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());
  return std::make_shared<bencoding::DictNode>(arguments_dict);
}
void krpc::Response::encode(std::ostream &os, bencoding::EncodeMode mode) const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> dict;
  dict["r"] = get_response_node();

  build_bencoding_node(dict);

  auto root = bencoding::DictNode(dict);
  root.encode(os, mode);
}
std::shared_ptr<Message> Response::decode(
    const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict,
    const std::string &t,
    const std::string &v,
    const std::string &method_name) {
  if (dict.find("r") == dict.end()) {
    throw InvalidMessage("Response, 'r' not found");
  }
  auto r_node = std::dynamic_pointer_cast<bencoding::DictNode>(dict.at("r"));
  if (!r_node) {
    throw InvalidMessage("Response, 'r' is not a dict");
  }
  auto r_dict = r_node->dict();
  if (method_name == MethodNamePing) {
    auto id = get_string_or_throw(r_dict, "id", "PingResponse");
    return std::make_shared<PingResponse>(t, v, NodeID::from_string(id));
  } else if (method_name == MethodNameFindNode) {
    auto id_str = get_string_or_throw(r_dict, "id", "FindNode");
    auto nodes_str = get_string_or_throw(r_dict, "nodes", "FindNode");
    std::stringstream ss(nodes_str);
    // the stream has at least 1 character
    std::vector<NodeInfo> nodes;
    while (ss.peek() != EOF) {
      nodes.push_back(NodeInfo::decode(ss));
    }
    return std::make_shared<FindNodeResponse>(t, v, NodeID::from_string(id_str), nodes);
  } else if (method_name == MethodNameGetPeers) {
    // TODO
    LOG(warning) << "GetPeers query received, ignored";
  } else if (method_name == MethodNameAnnouncePeer) {
    // TODO
    LOG(warning) << "AnnouncePeer query received, ignored";
  } else {
    throw InvalidMessage("Unknown response type: " + method_name);
  }
  throw InvalidMessage("Response not implemented");
}
std::shared_ptr<krpc::Message> krpc::Message::decode(
    const bencoding::Node &node,
    const std::function<std::string (std::string)>& get_method_name) {

  auto root = dynamic_cast<const bencoding::DictNode*>(&node);
  if (!root) {
    throw InvalidMessage("Root node type must be Dict");
  }
  auto &dict = root->dict();

  auto t = get_string_or_throw(dict, "t", "Root node");
  auto y = get_string_or_throw(dict, "y", "Root node");
  auto v = get_string_or_empty(dict, "v", "Root node");

  if (y == "q") {
    return krpc::Query::decode(dict, t, v);
  } else if (y == "r") {
    return krpc::Response::decode(dict, t, v, get_method_name(t));
  } else if (y == "e") {
    return krpc::Error::decode(dict, t, v, get_method_name(t));
  } else {
    throw InvalidMessage("Root node, 'y' is not one of  {'q', 'r', 'e'}");
  }
}
std::shared_ptr<bencoding::Node> krpc::PingQuery::get_arguments_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
  std::stringstream ss;
  sender_id_.encode(ss);
  arguments_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());
  return std::make_shared<bencoding::DictNode>(arguments_dict);
}


namespace {
template <typename T>
T host_to_network(T input);

template <typename T>
T network_to_host(T input) {
  return host_to_network(input);
}

template <>
uint32_t host_to_network<uint32_t>(uint32_t input) {
  uint64_t rval;
  auto *data = (uint8_t *)&rval;
  data[0] = input >> 24U;
  data[1] = input >> 16U;
  data[2] = input >> 8U;
  data[3] = input >> 0U;
  return rval;
}

template <>
uint16_t host_to_network<uint16_t>(uint16_t input) {
  uint64_t rval;
  auto *data = (uint8_t *)&rval;
  data[0] = input >> 8U;
  data[1] = input >> 0U;
  return rval;
}
}

NodeInfo NodeInfo::decode(std::istream &is) {
  std::vector<char> s(NodeIDLength);
  is.read(s.data(), NodeIDLength);
  if (!is.good()) {
    throw InvalidMessage("NodeInfo invalid");
  }

  auto node_id = NodeID::from_string(std::string(s.data(), s.size()));

  uint32_t ip;
  uint16_t port;
  is.read(reinterpret_cast<char*>(&ip), sizeof(ip));
  ip = network_to_host(ip);
  is.read(reinterpret_cast<char*>(&port), sizeof(port));
  port = network_to_host(port);

  NodeInfo info{node_id, ip, port};
  return info;
}

void NodeInfo::encode(std::ostream &os) const {
  node_id_.encode(os);
  auto ip = host_to_network(ip_);
  os.write((const char*)&ip, sizeof(ip));

  auto port = host_to_network(port_);
  os.write((const char*)&port, sizeof(port));
}
std::string NodeInfo::to_string() const {
  std::string ip, port;
  {
    uint32_t t1, t2, t3, t4;
    std::stringstream ss;
    t1 = (ip_ >> 24u) & 0xffu;
    t2 = (ip_ >> 16u) & 0xffu;
    t3 = (ip_ >> 8u) & 0xffu;
    t4 = (ip_ >> 0u) & 0xffu;
    ss << t1 << "." << t2 << "." << t3 << "." << t4;
    ip = ss.str();
  }

  {
    std::stringstream ss;
    ss << port_;
    ss >> port;
  }
  return "NodeID: '" + node_id_.to_string() + "' endpoint: '" + ip + ":" + port + "'";
}

std::shared_ptr<bencoding::Node> FindNodeResponse::get_response_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> response_dict;

  std::stringstream ss;
  node_id_.encode(ss);
  response_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());

  std::stringstream ss2;
  for (auto node : nodes_) {
    node.encode(ss2);
  }
  response_dict["nodes"] = std::make_shared<bencoding::StringNode>(ss2.str());

  return std::make_shared<bencoding::DictNode>(response_dict);
}
void FindNodeResponse::print_nodes() {
  LOG(debug) << "FindNodeResponse ";
  for (auto node : nodes_) {
    LOG(debug) << node.to_string();
  }
}
std::shared_ptr<bencoding::Node> FindNodeQuery::get_arguments_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
  {
    std::stringstream ss;
    sender_id_.encode(ss);
    arguments_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());
  }
  {
    std::stringstream ss;
    target_id_.encode(ss);
    arguments_dict["target"] = std::make_shared<bencoding::StringNode>(ss.str());
  }
  return std::make_shared<bencoding::DictNode>(arguments_dict);

}
std::shared_ptr<Message> Error::decode(const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict,
                                       const std::string &t,
                                       const std::string &v,
                                       const std::string &method_name) {
  if (dict.find("e") == dict.end()) {
    throw InvalidMessage("Invalid 'Error' message, 'e' not found");
  }
  auto e = std::dynamic_pointer_cast<bencoding::ListNode>(dict.at("e"));
  if (!e) {
    throw InvalidMessage("Invalid 'Error' message, 'e' is not a list");
  }
  if (e->size() != 2) {
    throw InvalidMessage("Invalid 'Error' message, size of 'e' is not 2");
  }
  int error_code = -1;
  if (auto err_code = std::dynamic_pointer_cast<bencoding::IntNode>((*e)[0]); err_code) {
    error_code = *err_code;
  } else {
    throw InvalidMessage("Invalid 'Error' message, the first element of 'e' is not a int");
  }

  std::string message;
  if (auto err_message = std::dynamic_pointer_cast<bencoding::StringNode>((*e)[1]); err_message) {
    message = *err_message;
  } else {
    throw InvalidMessage("Invalid 'Error' message, the second element of 'e' is not a string");
  }

  return std::make_shared<Error>(error_code, message);
}
std::shared_ptr<bencoding::Node> GetPeersQuery::get_arguments_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
  {
    std::stringstream ss;
    sender_id_.encode(ss);
    arguments_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());
  }
  {
    std::stringstream ss;
    info_hash_.encode(ss);
    arguments_dict["info_hash"] = std::make_shared<bencoding::StringNode>(ss.str());
  }
  return std::make_shared<bencoding::DictNode>(arguments_dict);

}
std::shared_ptr<bencoding::Node> AnnouncePeerQuery::get_arguments_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
  {
    std::stringstream ss;
    sender_id_.encode(ss);
    arguments_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());
  }
  arguments_dict["implied_port"] = std::make_shared<bencoding::IntNode>(implied_port_);
  {
    std::stringstream ss;
    info_hash_.encode(ss);
    arguments_dict["info_hash"] = std::make_shared<bencoding::StringNode>(ss.str());
  }
  arguments_dict["port"] = std::make_shared<bencoding::IntNode>(port_);
  arguments_dict["token"] = std::make_shared<bencoding::StringNode>(token_);
  return std::make_shared<bencoding::DictNode>(arguments_dict);
}
std::shared_ptr<bencoding::Node> GetPeersResponse::get_response_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> response_dict;

  std::stringstream ss;
  node_id_.encode(ss);
  response_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());

  response_dict["token"] = std::make_shared<bencoding::StringNode>(token_);
  if (has_peers_) {
    uint32_t ip{};
    uint16_t port{};
    std::vector<std::shared_ptr<bencoding::Node>> peers_list;
    for (auto item : peers_) {
      std::tie(ip, port) = item;
      ip = host_to_network(ip);
      port = host_to_network(port);
      char tmp[sizeof(ip) + sizeof(port)];
      memcpy(tmp, &ip, sizeof(ip));
      memcpy(tmp + sizeof(ip), &port, sizeof(port));
      peers_list.push_back(std::make_shared<bencoding::StringNode>(std::string(tmp, sizeof(tmp))));
    }
    response_dict["values"] = std::make_shared<bencoding::ListNode>(peers_list);
  } else {
    std::stringstream ss2;
    for (auto node : nodes_) {
      node.encode(ss2);
    }
    response_dict["nodes"] = std::make_shared<bencoding::StringNode>(ss2.str());
  }

  return std::make_shared<bencoding::DictNode>(response_dict);

}
}
