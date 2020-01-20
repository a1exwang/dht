#include "krpc.hpp"

#include <algorithm>
#include <utility>
#include <sstream>
#include <random>

namespace krpc {

NodeID NodeID::random() {
  std::random_device device;
  int seed = 123;
  std::mt19937 rng(seed);
  NodeID ret{};
  std::generate(ret.data_.begin(), ret.data_.end(), rng);
//  memcpy(ret.data_.data(), "abcdefghijklmnoprqstuvwxyz:::::::", ret.data_.size());
  return ret;
}
std::string NodeID::to_string() const {
  std::stringstream ss;
  for (auto c : data_) {
    ss << std::hex << std::setfill('0') << std::setw(2) << (uint32_t)(uint8_t)c;
  }
  return ss.str();
}
void krpc::Message::build_bencoding_node(std::map<std::string, std::shared_ptr<bencoding::Node>> &dict) {
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
  if (q == MethodNamePing) {
    if (dict.find("a") == dict.end()) {
      throw InvalidMessage("Query, 'a' not found");
    }
    auto a_node = std::dynamic_pointer_cast<bencoding::DictNode>(dict.at("a"));
    if (!a_node) {
      throw InvalidMessage("Query, 'a' is not a dict");
    }
    auto a_dict = a_node->dict();
    auto node_id_string = get_string_or_throw(a_dict, "id", "Query");
    auto node_id = NodeID::from_string(node_id_string);
    return std::make_shared<PingQuery>(t, v, node_id);
  } else if (q == MethodNameFindNode) {
    throw InvalidMessage("Query, method not implemented");
  } else if (q == MethodNameGetPeers) {
    throw InvalidMessage("Query, method not implemented");
  } else if (q == MethodNameAnnouncePeer) {
    throw InvalidMessage("Query, method not implemented");
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
void krpc::Response::encode(std::ostream &os, bencoding::EncodeMode mode) {
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
    auto id_str = get_string_or_throw(r_dict, "id", "PingResponse");
    auto nodes_str = get_string_or_throw(r_dict, "nodes", "PingResponse");
    std::stringstream ss(nodes_str);
    // the stream has at least 1 character
    std::vector<NodeInfo> nodes;
    while (ss.peek() != EOF) {
      nodes.push_back(NodeInfo::decode(ss));
    }
    return std::make_shared<FindNodeResponse>(t, v, NodeID::from_string(id_str), nodes);
  } else if (method_name == MethodNameGetPeers) {
    // TODO
  } else if (method_name == MethodNameAnnouncePeer) {
    // TODO
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
    // FIXME
    return krpc::Response::decode(dict, t, v, get_method_name(t));
  } else {
    throw InvalidMessage("Root node, 'y' is either 'q' nor 'r'");
  }
}
std::shared_ptr<bencoding::Node> krpc::PingQuery::get_arguments_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
  std::stringstream ss;
  node_id_.encode(ss);
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
}
