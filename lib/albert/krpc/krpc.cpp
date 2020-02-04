#include <albert/krpc/krpc.hpp>

#include <algorithm>
#include <sstream>

#include <albert/log/log.hpp>
#include <albert/utils/utils.hpp>
#include <albert/u160/u160.hpp>

namespace albert::krpc {

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
    auto node_id = u160::U160::from_string(
        get_string_or_throw(a_dict.dict(), "id", "Query"));
    return std::make_shared<PingQuery>(t, v, node_id);
  } else if (q == MethodNameFindNode) {
    auto sender_id = get_string_or_throw(a_dict.dict(), "id", "FindNodeQuery");
    auto target_id = get_string_or_throw(a_dict.dict(), "target", "FindNodeQuery");
    return std::make_shared<FindNodeQuery>(t, v, u160::U160::from_string(sender_id), u160::U160::from_string(target_id));
  } else if (q == MethodNameGetPeers) {
    auto sender_id = get_string_or_throw(a_dict.dict(), "id", "GetPeersQuery");
    auto info_hash = get_string_or_throw(a_dict.dict(), "info_hash", "GetPeersQuery");
    return std::make_shared<GetPeersQuery>(t, v, u160::U160::from_string(sender_id), u160::U160::from_string(info_hash));
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
        u160::U160::from_string(sender_id),
        implied_port,
        u160::U160::from_string(info_hash),
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
    return std::make_shared<PingResponse>(t, v, u160::U160::from_string(id));
  } else if (method_name == MethodNameFindNode) {
    auto id_str = get_string_or_throw(r_dict, "id", "FindNode");
    auto nodes_str = get_string_or_throw(r_dict, "nodes", "FindNode");
    std::stringstream ss(nodes_str);
    // the stream has at least 1 character
    std::vector<NodeInfo> nodes;
    while (ss.peek() != EOF) {
      nodes.push_back(NodeInfo::decode(ss));
    }
    return std::make_shared<FindNodeResponse>(t, v, u160::U160::from_string(id_str), nodes);
  } else if (method_name == MethodNameGetPeers) {

    auto id_str = get_string_or_throw(r_dict, "id", "GetPeersRespone");
    auto token = get_string_or_throw(r_dict, "token", "GetPeersResponse");
    auto sender_id = u160::U160::from_string(id_str);
    if (r_dict.find("nodes") != r_dict.end()) {
      auto nodes_str = get_string_or_throw(r_dict, "nodes", "GetPeersResponse");
      std::stringstream ss_nodes(nodes_str);
      // the stream has at least 1 character
      std::vector<NodeInfo> nodes;
      while (ss_nodes.peek() != EOF) {
        nodes.push_back(NodeInfo::decode(ss_nodes));
      }
      return std::make_shared<krpc::GetPeersResponse>(t, v, sender_id, token, nodes);
    } else {
      if (r_dict.find("values") == r_dict.end()) {
        throw InvalidMessage("Invalid GetPeers response, neither 'nodes' nor 'values' is found");
      } else {
        auto values_list = std::dynamic_pointer_cast<bencoding::ListNode>(r_dict.at("values"));
        if (values_list) {
          std::vector<std::tuple<uint32_t, uint16_t>> values;
          for (int i = 0; i < values_list->size(); i++) {
            auto peer_info = (*values_list)[i];
            if (auto s = std::dynamic_pointer_cast<bencoding::StringNode>(peer_info); s) {
              std::string peer = *s;
              uint32_t ip;
              uint16_t port;
              memcpy(&ip, peer.data(), sizeof(ip));
              ip = dht::utils::network_to_host(ip);
              memcpy(&port, peer.data() + sizeof(ip), sizeof(port));
              port = dht::utils::network_to_host(port);
              values.emplace_back(ip, port);
            } else {
              LOG(warning) << "Invalid GetPeers response, response.values[" << i << "] is not a string";
            }
          }
          return std::make_shared<krpc::GetPeersResponse>(t, v, sender_id, token, values);
        } else {
          throw InvalidMessage("Invalid GetPeers response, values is not list");
        }
      }
    }

  } else if (method_name == MethodNameAnnouncePeer) {
    // TODO
    LOG(warning) << "AnnouncePeer response received, ignored";
  } else if (method_name == MethodNameSampleInfohashes) {
    auto id_str = get_string_or_throw(r_dict, "id", "SampleInfohashes");
    auto samples_str = get_string_or_throw(r_dict, "samples", "SampleInfohashes");
    int64_t interval = 0;
    size_t num = 0;
    if (r_dict.find("interval") != r_dict.end()) {
      interval = get_int64_or_throw(r_dict, "interval", "SampleInfohashes");
    }
    if (r_dict.find("num") != r_dict.end()) {
      num = get_int64_or_throw(r_dict, "num", "SampleInfohashes");
    }
    std::stringstream ss(samples_str);
    // the stream has at least 1 character
    std::vector<u160::U160> samples;
    while (ss.peek() != EOF) {
      samples.push_back(u160::U160::decode(ss));
    }
    return std::make_shared<SampleInfohashesResponse>(
        t, v, u160::U160::from_string(id_str), interval, num, samples
    );
  } else {
    throw InvalidMessage("Unknown response type: '" + method_name + "'");
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

  try {
    if (y == "q") {
      return krpc::Query::decode(dict, t, v);
    } else if (y == "r") {
      return krpc::Response::decode(dict, t, v, get_method_name(t));
    } else if (y == "e") {
      return krpc::Error::decode(dict, t, v, get_method_name(t));
    } else {
      throw InvalidMessage("Root node, 'y' is not one of  {'q', 'r', 'e'}");
    }
  } catch (const u160::InvalidFormat &e) {
    throw InvalidMessage(std::string("Invalid u160 parsing: ") + e.what());
  }
}
std::shared_ptr<bencoding::Node> krpc::PingQuery::get_arguments_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
  std::stringstream ss;
  sender_id_.encode(ss);
  arguments_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());
  return std::make_shared<bencoding::DictNode>(arguments_dict);
}

NodeInfo NodeInfo::decode(std::istream &is) {
  std::vector<char> s(u160::U160Length);
  is.read(s.data(), u160::U160Length);
  if (!is.good()) {
    throw InvalidMessage("NodeInfo invalid");
  }

  auto node_id = u160::U160::from_string(std::string(s.data(), s.size()));

  uint32_t ip;
  uint16_t port;
  is.read(reinterpret_cast<char*>(&ip), sizeof(ip));
  ip = dht::utils::network_to_host(ip);
  is.read(reinterpret_cast<char*>(&port), sizeof(port));
  port = dht::utils::network_to_host(port);

  NodeInfo info{node_id, ip, port};
  return info;
}

void NodeInfo::encode(std::ostream &os) const {
  node_id_.encode(os);
  auto ip = dht::utils::host_to_network(ip_);
  os.write((const char*)&ip, sizeof(ip));

  auto port = dht::utils::host_to_network(port_);
  os.write((const char*)&port, sizeof(port));
}
std::string NodeInfo::to_string() const {
  return "NodeID: '" + node_id_.to_string() + "' endpoint: '" + format_ep(ip_, port_) + "'";
}
std::tuple<uint32_t, uint16_t> NodeInfo::tuple() const { return std::make_tuple(ip_, port_); }
bool NodeInfo::operator<(const NodeInfo &rhs) const { return this->node_id_ < rhs.node_id_; }

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
  sender_id_.encode(ss);
  response_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());

  response_dict["token"] = std::make_shared<bencoding::StringNode>(token_);
  if (has_peers_) {
    uint32_t ip{};
    uint16_t port{};
    std::vector<std::shared_ptr<bencoding::Node>> peers_list;
    for (auto item : peers_) {
      std::tie(ip, port) = item;
      ip = dht::utils::host_to_network(ip);
      port = dht::utils::host_to_network(port);
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
std::shared_ptr<bencoding::Node> SampleInfohashesQuery::get_arguments_node() const {
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
std::string format_ep(uint32_t ip, uint16_t port) {
  std::string ip_s, port_s;
  {
    uint32_t t1, t2, t3, t4;
    std::stringstream ss;
    t1 = (ip >> 24u) & 0xffu;
    t2 = (ip >> 16u) & 0xffu;
    t3 = (ip >> 8u) & 0xffu;
    t4 = (ip >> 0u) & 0xffu;
    ss << t1 << "." << t2 << "." << t3 << "." << t4;
    ip_s = ss.str();
  }

  {
    std::stringstream ss;
    ss << port;
    ss >> port_s;
  }
  return ip_s + ":" + port_s;
}
std::shared_ptr<bencoding::Node> AnnouncePeerResponse::get_response_node() const {
  std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
  std::stringstream ss;
  node_id_.encode(ss);
  arguments_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());
  return std::make_shared<bencoding::DictNode>(arguments_dict);
}

}

