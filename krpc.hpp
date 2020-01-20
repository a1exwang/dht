#pragma once

#include "bencoding.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace krpc {
constexpr const char *ClientVersion = "WTF0.0";

class InvalidMessage : public std::runtime_error {
 public:
  InvalidMessage(const std::string &msg) :runtime_error(msg) { }
};


constexpr size_t NodeIDLength = 20;
class NodeID {
 public:
  static NodeID from_string(std::string s) {
    NodeID ret{};
    if (s.size() != NodeIDLength) {
      throw InvalidMessage("NodeID is not NodeIDLength long");
    }
    std::copy(s.begin(), s.end(), ret.data_.begin());
    return ret;
  }
  void encode(std::ostream &os) const {
    os.write(data_.data(), data_.size());
  }
  static NodeID random();

  std::string to_string() const;

 private:
  std::array<char, NodeIDLength> data_;
};

class NodeInfo {
 public:
  NodeInfo(NodeID node_id, uint32_t ip, uint32_t port)
      :node_id_(node_id), ip_(ip), port_(port) { }
  static NodeInfo decode(std::istream &is);
  void encode(std::ostream &os) const;
  std::string to_string() const;
 private:
  NodeID node_id_;
  uint32_t ip_;
  uint16_t port_;
};

constexpr const char *MessageTypeQuery = "q";
constexpr const char *MessageTypeResponse = "r";
constexpr const char *MethodNamePing = "ping";
constexpr const char *MethodNameFindNode = "find_node";
constexpr const char *MethodNameGetPeers = "get_peers";
constexpr const char *MethodNameAnnouncePeer = "announce_peer";

class Message {
 public:
  Message(std::string type, std::string client_version)
      :type_(std::move(type)), client_version_(std::move(client_version)) { }
  Message(std::string transaction_id, std::string type, std::string client_version)
      :transaction_id_(std::move(transaction_id)), type_(std::move(type)), client_version_(std::move(client_version)) { }

  virtual ~Message() = default;

  static std::shared_ptr<Message> decode(
      const bencoding::Node &node,
      const std::function<std::string (std::string)>& get_method_name
      );
  void build_bencoding_node(std::map<std::string, std::shared_ptr<bencoding::Node>> &dict);

  void set_transaction_id(std::string transaction_id) { this->transaction_id_ = std::move(transaction_id); }
 private:

  std::string transaction_id_;
  std::string type_;
  std::string client_version_;
};

class Query :public Message {
 public:
  Query(std::string method_name)
      :Message(MessageTypeQuery, ClientVersion), method_name_(std::move(method_name)) { }
  Query(std::string client_version, std::string method_name)
      :Message(MessageTypeQuery, std::move(client_version)), method_name_(std::move(method_name)) { }
  Query(std::string transaction_id, std::string client_version, std::string method_name)
      :Message(std::move(transaction_id), MessageTypeQuery, std::move(client_version)), method_name_(std::move(method_name)) { }

  void encode(std::ostream &os, bencoding::EncodeMode mode);
  static std::shared_ptr<Message> decode(const std::map<std::string, std::shared_ptr<bencoding::Node>> &, const std::string& t, const std::string& v);

  std::string method_name() const { return method_name_; }

 protected:
  virtual std::shared_ptr<bencoding::Node> get_arguments_node() const = 0;
 private:
  std::string method_name_;
};

class PingQuery :public Query {
 public:
  PingQuery(std::string transaction_id, std::string client_version, NodeID node_id)
      :Query(std::move(transaction_id), std::move(client_version), MethodNamePing), node_id_(node_id) { }
  PingQuery(NodeID node_id)
      :Query(MethodNamePing), node_id_(node_id) { }

 protected:
  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 private:
  NodeID node_id_;
};

class FindNodeQuery :public Query {
 public:
  FindNodeQuery(NodeID self_id, NodeID target_id)
      :Query(MethodNameFindNode), self_id_(self_id), target_id_(target_id) {}
 protected:
  std::shared_ptr<bencoding::Node> get_arguments_node() const override {
    std::map<std::string, std::shared_ptr<bencoding::Node>> arguments_dict;
    {
      std::stringstream ss;
      self_id_.encode(ss);
      arguments_dict["id"] = std::make_shared<bencoding::StringNode>(ss.str());
    }
    {
      std::stringstream ss;
      target_id_.encode(ss);
      arguments_dict["target"] = std::make_shared<bencoding::StringNode>(ss.str());
    }
    return std::make_shared<bencoding::DictNode>(arguments_dict);

  }

 private:
  NodeID self_id_;
  NodeID target_id_;
};

class Response :public Message {
 public:
  Response(std::string transaction_id, std::string client_version)
      :Message(std::move(transaction_id), MessageTypeResponse, std::move(client_version)) { }

  void encode(std::ostream &os, bencoding::EncodeMode mode);
  static std::shared_ptr<Message> decode(const std::map<std::string, std::shared_ptr<bencoding::Node>> &, const std::string &t, const std::string &v, const std::string &method_name);

 protected:
  virtual std::shared_ptr<bencoding::Node> get_response_node() const = 0;
};

class PingResponse :public Response {
 public:
  PingResponse(std::string transaction_id, std::string client_version, NodeID node_id)
      :Response(std::move(transaction_id), std::move(client_version)), node_id_(node_id) { }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  NodeID node_id_;
};

class FindNodeResponse :public Response {
 public:
  FindNodeResponse(
      std::string transaction_id,
      std::string client_version,
      NodeID node_id,
      std::vector<NodeInfo> nodes)
      :Response(std::move(transaction_id), std::move(client_version)), node_id_(node_id), nodes_(std::move(nodes)) { }
  void print_nodes() {
    std::cout << "FindNodeResponse " << std::endl;
    for (auto node : nodes_) {
      std::cout << node.to_string() << std::endl;
    }
  }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  NodeID node_id_;
  std::vector<NodeInfo> nodes_;
};

}

