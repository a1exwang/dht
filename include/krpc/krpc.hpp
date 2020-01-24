#pragma once
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

#include <bencode/bencoding.hpp>

namespace krpc {
constexpr const char *ClientVersion = "WTF0.0";

class InvalidMessage : public std::runtime_error {
 public:
  explicit InvalidMessage(const std::string &msg) :runtime_error(msg) { }
};


constexpr size_t NodeIDLength = 20;
constexpr size_t NodeIDBits = NodeIDLength * 8;
constexpr auto KRPCTimeout = std::chrono::seconds(30);
class NodeID {
 public:
  NodeID() {
    data_.fill(0);
  }

  static NodeID pow2(size_t r);
  static NodeID pow2m1(size_t r);
  static NodeID from_string(std::string s);
  static NodeID from_hex(const std::string &s);
  void encode(std::ostream &os) const;
  static NodeID decode(std::istream &is);
  static NodeID random();
  static NodeID random_from_prefix(const NodeID &prefix, size_t prefix_length);

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
  NodeInfo() { }
  NodeInfo(NodeID node_id, uint32_t ip, uint32_t port)
      :node_id_(node_id), ip_(ip), port_(port) { }
  static NodeInfo decode(std::istream &is);
  void encode(std::ostream &os) const;
  std::string to_string() const;
  NodeID id() const { return node_id_; }
  uint32_t ip() const { return ip_; }
  uint16_t port() const { return port_; }
  void ip(uint32_t ip) { ip_ = ip; }
  void port(uint16_t port) { port_ = port; }
  bool operator<(const NodeInfo &rhs) const { return this->node_id_ < rhs.node_id_; }
 private:
  NodeID node_id_;
  uint32_t ip_{};
  uint16_t port_{};
};

constexpr const char *MessageTypeQuery = "q";
constexpr const char *MessageTypeResponse = "r";
constexpr const char *MessageTypeError = "e";

constexpr const char *MethodNamePing = "ping";
constexpr const char *MethodNameFindNode = "find_node";
constexpr const char *MethodNameGetPeers = "get_peers";
constexpr const char *MethodNameAnnouncePeer = "announce_peer";
constexpr const char *MethodNameSampleInfohashes = "sample_infohashes";

constexpr int ErrorCodeGenericError = 201;
constexpr int ErrorServerError = 202;
constexpr int ErrorProtocolError = 203;
constexpr int ErrorMethodUnknown = 204;

class Message {
 public:
  Message(std::string type)
      :type_(std::move(type)), client_version_(ClientVersion) { }
  Message(std::string type, std::string client_version)
      :type_(std::move(type)), client_version_(std::move(client_version)) { }
  Message(std::string transaction_id, std::string type, std::string client_version)
      :transaction_id_(std::move(transaction_id)), type_(std::move(type)), client_version_(std::move(client_version)) { }

  virtual ~Message() = default;

  static std::shared_ptr<Message> decode(
      const bencoding::Node &node,
      const std::function<std::string (std::string)>& get_method_name
      );
  void build_bencoding_node(std::map<std::string, std::shared_ptr<bencoding::Node>> &dict) const;

  void set_transaction_id(std::string transaction_id) { this->transaction_id_ = std::move(transaction_id); }
  std::string transaction_id() const { return this->transaction_id_; }

 private:

  std::string transaction_id_;
  std::string type_;
  std::string client_version_;
};

class Error :public Message {
 public:
  Error(int error_code, std::string message)
      :Message(MessageTypeError), error_code_(error_code), message_(message) { }

  std::string message() const {
    return message_;
  }

  static std::shared_ptr<Message> decode(
      const std::map<std::string, std::shared_ptr<bencoding::Node>> &dict,
      const std::string &t,
      const std::string &v,
      const std::string &method_name);
 private:
  int error_code_;
  std::string message_;
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
      : Query(std::move(transaction_id), std::move(client_version), MethodNamePing), sender_id_(sender_id) { }
  explicit PingQuery(NodeID sender_id)
      : Query(MethodNamePing), sender_id_(sender_id) { }
  NodeID sender_id() const { return sender_id_; }

 protected:
  [[nodiscard]]
  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 private:
  NodeID sender_id_;
};

class FindNodeQuery :public Query {
 public:
  FindNodeQuery(std::string transaction_id, std::string version, NodeID sender_id, NodeID target_id)
      : Query(std::move(transaction_id), std::move(version), MethodNameFindNode),
      sender_id_(sender_id), target_id_(target_id) {}
  FindNodeQuery(NodeID sender_id, NodeID target_id)
      : Query(MethodNameFindNode), sender_id_(sender_id), target_id_(target_id) {}

  const NodeID &sender_id() const { return sender_id_; }
  const NodeID &target_id() const { return target_id_; }

  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 protected:
 private:
  NodeID sender_id_;
  NodeID target_id_;
};

class SampleInfohashesQuery :public Query {
 public:
  SampleInfohashesQuery(
      NodeID sender_id,
      NodeID target_id)
      : Query(MethodNameSampleInfohashes),
        sender_id_(sender_id), target_id_(target_id) {}

  const NodeID &sender_id() const { return sender_id_; }
  const NodeID &target_id() const { return target_id_; }

  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 protected:
 private:
  NodeID sender_id_;
  NodeID target_id_;
};

class GetPeersQuery :public Query {
 public:
  GetPeersQuery(std::string transaction_id, std::string version, NodeID sender_id, NodeID info_hash)
      : Query(std::move(transaction_id), std::move(version), MethodNameGetPeers),
        sender_id_(sender_id), info_hash_(info_hash) {}
  GetPeersQuery(NodeID sender_id, NodeID info_hash)
      : Query(MethodNameGetPeers), sender_id_(sender_id), info_hash_(info_hash) {}

  const NodeID &sender_id() const { return sender_id_; }
  const NodeID &info_hash() const { return info_hash_; }

 protected:
  std::shared_ptr<bencoding::Node> get_arguments_node() const override;
 private:
  NodeID sender_id_;
  NodeID info_hash_;
};

class AnnouncePeerQuery :public Query {
 public:
  AnnouncePeerQuery(
      std::string transaction_id,
      std::string version,
      krpc::NodeID sender_id,
      bool implied_port,
      krpc::NodeID info_hash,
      uint16_t port,
      std::string token)
      :Query(std::move(transaction_id), std::move(version), MethodNameAnnouncePeer),
        sender_id_(sender_id), info_hash_(info_hash), implied_port_(implied_port),
        port_(port), token_(token) {}

  const NodeID &sender_id() const { return sender_id_; }
  const NodeID &info_hash() const { return info_hash_; }

 protected:
  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 private:
  krpc::NodeID sender_id_;
  bool implied_port_;
  krpc::NodeID info_hash_;
  uint16_t port_;
  std::string token_;
};

class Response :public Message {
 public:
  explicit Response(std::string transaction_id)
      :Message(std::move(transaction_id), MessageTypeResponse, ClientVersion) { }
  Response(std::string transaction_id, std::string client_version)
      :Message(std::move(transaction_id), MessageTypeResponse, std::move(client_version)) { }

  void encode(std::ostream &os, bencoding::EncodeMode mode) const ;
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
      NodeID sender_id,
      std::vector<NodeInfo> nodes)
      : Response(std::move(transaction_id)), node_id_(sender_id), nodes_(std::move(nodes)) { }
  FindNodeResponse(
      std::string transaction_id,
      std::string client_version,
      NodeID sender_id,
      std::vector<NodeInfo> nodes)
      : Response(std::move(transaction_id), std::move(client_version)), node_id_(sender_id), nodes_(std::move(nodes)) { }
  void print_nodes();
  const std::vector<NodeInfo> &nodes() const { return nodes_; }
  const NodeID &sender_id() const { return node_id_; }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  NodeID node_id_;
  std::vector<NodeInfo> nodes_;
};

class GetPeersResponse :public Response {
 public:
  GetPeersResponse(
      std::string transaction_id,
      std::string client_version,
      NodeID sender_id,
      std::string token,
      std::vector<NodeInfo> nodes)
      : Response(std::move(transaction_id), std::move(client_version)),
        sender_id_(sender_id),
        token_(std::move(std::move(token))),
        has_peers_(false),
        nodes_(std::move(nodes)) { }
  GetPeersResponse(
      std::string transaction_id,
      std::string client_version,
      NodeID sender_id,
      std::string token,
      std::vector<std::tuple<uint32_t, uint16_t>> peers)
      : Response(std::move(transaction_id), std::move(client_version)),
        sender_id_(sender_id),
        token_(std::move(token)),
        has_peers_(true),
        peers_(std::move(peers)) { }
  const NodeID &sender_id() const { return sender_id_; }
  bool has_peers() const { return has_peers_; }
  std::vector<NodeInfo> nodes() const { return nodes_; };
  std::vector<std::tuple<uint32_t, uint16_t>> peers() const { return peers_; };
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  bool has_peers_;
  NodeID sender_id_;
  std::string token_;
  std::vector<NodeInfo> nodes_;
  std::vector<std::tuple<uint32_t, uint16_t>> peers_;
};

class SampleInfohashesResponse :public Response {
 public:
  SampleInfohashesResponse(
      std::string transaction_id,
      std::string client_version,
      NodeID sender_id,
      int64_t interval,
      size_t num,
      std::vector<NodeID> samples)
      : Response(std::move(transaction_id), std::move(client_version)),
      node_id_(sender_id),
      interval_(interval),
      num_(num),
      samples_(std::move(samples)) { }
  const std::vector<NodeID> &samples() const { return samples_; }
  const NodeID sender_id() const { return node_id_; }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override { return nullptr; }
 private:
  NodeID node_id_;
  int64_t interval_{};
  size_t num_{};
  std::vector<NodeID> samples_;
};

}

