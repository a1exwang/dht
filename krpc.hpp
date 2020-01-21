#pragma once

#include "bencoding.hpp"

#include <algorithm>
#include <chrono>
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
  explicit InvalidMessage(const std::string &msg) :runtime_error(msg) { }
};


constexpr size_t NodeIDLength = 20;
constexpr size_t NodeIDBits = NodeIDLength * 8;
constexpr auto KRPCTimeout = std::chrono::seconds(10);
class NodeID {
 public:
  NodeID() {
    data_.fill(0);
  }

  static NodeID pow2(size_t r);
  static NodeID pow2m1(size_t r);
  static NodeID from_string(std::string s);
  void encode(std::ostream &os) const;
  static NodeID random();

  std::string to_string() const;

  bool operator<(const NodeID &rhs) const;
  bool operator==(const NodeID &rhs) const;
  bool operator<=(const NodeID &rhs) const;
  NodeID operator&(const NodeID &rhs) const;
  NodeID operator|(const NodeID &rhs) const;
  NodeID operator^(const NodeID &rhs) const;
  uint8_t bit(size_t r) const;
  NodeID distance(const NodeID &rhs) const { return *this ^ rhs; }

 private:
  // network byte order
  std::array<uint8_t, NodeIDLength> data_{};
};

class NodeInfo {
 public:
  NodeInfo(NodeID node_id, uint32_t ip, uint32_t port)
      :node_id_(node_id), ip_(ip), port_(port) { }
  static NodeInfo decode(std::istream &is);
  void encode(std::ostream &os) const;
  std::string to_string() const;
  NodeID id() const { return node_id_; }
  uint32_t ip() const { return ip_; }
  uint16_t port() const { return port_; }
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
  std::string transaction_id() const { return this->transaction_id_; }

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
  PingQuery(std::string transaction_id, std::string client_version, NodeID sender_id)
      :Query(std::move(transaction_id), std::move(client_version), MethodNamePing), node_id_(sender_id) { }
  PingQuery(NodeID sender_id)
      :Query(MethodNamePing), node_id_(sender_id) { }
  NodeID node_id() const { return node_id_; }
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
  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 private:
  NodeID self_id_;
  NodeID target_id_;
};

class Response :public Message {
 public:
  explicit Response(std::string transaction_id)
      :Message(std::move(transaction_id), MessageTypeResponse, ClientVersion) { }
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
  PingResponse(std::string transaction_id, NodeID node_id)
      :Response(std::move(transaction_id)), node_id_(node_id) { }
  NodeID node_id() const { return node_id_; }
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
  void print_nodes();
  const std::vector<NodeInfo> &nodes() const { return nodes_; }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  NodeID node_id_;
  std::vector<NodeInfo> nodes_;
};

}

