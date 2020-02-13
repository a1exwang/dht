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

#include <albert/bencode/bencoding.hpp>
#include <albert/u160/u160.hpp>

namespace albert::krpc {
constexpr const char *ClientVersion = "WTF0.0";

class InvalidMessage : public std::runtime_error {
 public:
  explicit InvalidMessage(const std::string &msg) :runtime_error(msg) { }
};

constexpr auto KRPCTimeout = std::chrono::seconds(30);

class NodeInfo {
 public:
  NodeInfo() { }
  NodeInfo(u160::U160 node_id, uint32_t ip, uint32_t port)
      :node_id_(node_id), ip_(ip), port_(port) { }
  static NodeInfo decode(std::istream &is);
  void encode(std::ostream &os) const;
  std::string to_string() const;
  u160::U160 id() const { return node_id_; }
  uint32_t ip() const { return ip_; }
  uint16_t port() const { return port_; }
  std::tuple<uint32_t, uint16_t> tuple() const;
  void ip(uint32_t ip) { ip_ = ip; }
  void port(uint16_t port) { port_ = port; }
  bool operator<(const NodeInfo &rhs) const;
  bool valid() const { return port_ != 0; }
 private:
  u160::U160 node_id_;
  uint32_t ip_{};
  uint16_t port_{};
};

std::string format_ep(uint32_t ip, uint16_t port);

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

  std::string version() const { return client_version_; }

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
  PingQuery(std::string transaction_id, std::string client_version, u160::U160 sender_id)
      : Query(std::move(transaction_id), std::move(client_version), MethodNamePing), sender_id_(sender_id) { }
  explicit PingQuery(u160::U160 sender_id)
      : Query(MethodNamePing), sender_id_(sender_id) { }
  u160::U160 sender_id() const { return sender_id_; }

 protected:
  [[nodiscard]]
  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 private:
  u160::U160 sender_id_;
};

class FindNodeQuery :public Query {
 public:
  FindNodeQuery(std::string transaction_id, std::string version, u160::U160 sender_id, u160::U160 target_id)
      : Query(std::move(transaction_id), std::move(version), MethodNameFindNode),
      sender_id_(sender_id), target_id_(target_id) {}
  FindNodeQuery(u160::U160 sender_id, u160::U160 target_id)
      : Query(MethodNameFindNode), sender_id_(sender_id), target_id_(target_id) {}

  const u160::U160 &sender_id() const { return sender_id_; }
  const u160::U160 &target_id() const { return target_id_; }

  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 protected:
 private:
  u160::U160 sender_id_;
  u160::U160 target_id_;
};

class SampleInfohashesQuery :public Query {
 public:
  SampleInfohashesQuery(
      u160::U160 sender_id,
      u160::U160 target_id)
      : Query(MethodNameSampleInfohashes),
        sender_id_(sender_id), target_id_(target_id) {}

  const u160::U160 &sender_id() const { return sender_id_; }
  const u160::U160 &target_id() const { return target_id_; }

  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 protected:
 private:
  u160::U160 sender_id_;
  u160::U160 target_id_;
};

class GetPeersQuery :public Query {
 public:
  GetPeersQuery(std::string transaction_id, std::string version, u160::U160 sender_id, u160::U160 info_hash)
      : Query(std::move(transaction_id), std::move(version), MethodNameGetPeers),
        sender_id_(sender_id), info_hash_(info_hash) {}
  GetPeersQuery(u160::U160 sender_id, u160::U160 info_hash)
      : Query(MethodNameGetPeers), sender_id_(sender_id), info_hash_(info_hash) {}

  const u160::U160 &sender_id() const { return sender_id_; }
  const u160::U160 &info_hash() const { return info_hash_; }

 protected:
  std::shared_ptr<bencoding::Node> get_arguments_node() const override;
 private:
  u160::U160 sender_id_;
  u160::U160 info_hash_;
};

class AnnouncePeerQuery :public Query {
 public:
  AnnouncePeerQuery(
      std::string transaction_id,
      std::string version,
      u160::U160 sender_id,
      bool implied_port,
      u160::U160 info_hash,
      uint16_t port,
      std::string token)
      :Query(std::move(transaction_id), std::move(version), MethodNameAnnouncePeer),
       sender_id_(sender_id),
       implied_port_(implied_port),
       info_hash_(info_hash),
       port_(port),
       token_(std::move(token)) {}

  const u160::U160 &sender_id() const { return sender_id_; }
  const u160::U160 &info_hash() const { return info_hash_; }

 protected:
  std::shared_ptr<bencoding::Node> get_arguments_node() const override;

 private:
  u160::U160 sender_id_;
  bool implied_port_;
  u160::U160 info_hash_;
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
  PingResponse(std::string transaction_id, std::string client_version, u160::U160 node_id)
      :Response(std::move(transaction_id), std::move(client_version)), node_id_(node_id) { }
  PingResponse(std::string transaction_id, u160::U160 node_id)
      :Response(std::move(transaction_id)), node_id_(node_id) { }
  u160::U160 node_id() const { return node_id_; }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  u160::U160 node_id_;
};

class FindNodeResponse :public Response {
 public:
  FindNodeResponse(
      std::string transaction_id,
      u160::U160 sender_id,
      std::vector<NodeInfo> nodes)
      : Response(std::move(transaction_id)), node_id_(sender_id), nodes_(std::move(nodes)) { }
  FindNodeResponse(
      std::string transaction_id,
      std::string client_version,
      u160::U160 sender_id,
      std::vector<NodeInfo> nodes)
      : Response(std::move(transaction_id), std::move(client_version)), node_id_(sender_id), nodes_(std::move(nodes)) { }
  void print_nodes();
  const std::vector<NodeInfo> &nodes() const { return nodes_; }
  const u160::U160 &sender_id() const { return node_id_; }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  u160::U160 node_id_;
  std::vector<NodeInfo> nodes_;
};

class GetPeersResponse :public Response {
 public:
  GetPeersResponse(
      std::string transaction_id,
      std::string client_version,
      u160::U160 sender_id,
      std::string token,
      std::vector<NodeInfo> nodes)
      : Response(std::move(transaction_id), std::move(client_version)),
        sender_id_(sender_id),
        token_(std::move(std::move(token))),
        nodes_(std::move(nodes)) { }

  GetPeersResponse(
      std::string transaction_id,
      std::string client_version,
      u160::U160 sender_id,
      std::string token,
      std::vector<std::tuple<uint32_t, uint16_t>> peers,
      std::vector<NodeInfo> nodes)
      : Response(std::move(transaction_id), std::move(client_version)),
        sender_id_(sender_id),
        token_(std::move(token)),
        nodes_(std::move(nodes)),
        peers_(std::move(peers))
  { }
  const u160::U160 &sender_id() const { return sender_id_; }
  bool has_peers() const { return peers_.size() > 0; }
  bool has_nodes() const { return nodes_.size() > 0; }
  std::vector<NodeInfo> nodes() const { return nodes_; };
  std::vector<std::tuple<uint32_t, uint16_t>> peers() const { return peers_; };
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  u160::U160 sender_id_;
  std::string token_;
  std::vector<NodeInfo> nodes_;
  std::vector<std::tuple<uint32_t, uint16_t>> peers_;
};

class AnnouncePeerResponse :public Response {
 public:
  AnnouncePeerResponse(
      std::string transaction_id,
      u160::U160 sender_id)
      : Response(std::move(transaction_id)), node_id_(sender_id) { }
  const u160::U160 &sender_id() const { return node_id_; }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override;
 private:
  u160::U160 node_id_;
};

class SampleInfohashesResponse :public Response {
 public:
  SampleInfohashesResponse(
      std::string transaction_id,
      std::string client_version,
      u160::U160 sender_id,
      int64_t interval,
      size_t num,
      std::vector<u160::U160> samples)
      : Response(std::move(transaction_id), std::move(client_version)),
      node_id_(sender_id),
      interval_(interval),
      num_(num),
      samples_(std::move(samples)) { }
  const std::vector<u160::U160> &samples() const { return samples_; }
  const u160::U160 sender_id() const { return node_id_; }
 protected:
  std::shared_ptr<bencoding::Node> get_response_node() const override { return nullptr; }
 private:
  u160::U160 node_id_;
  int64_t interval_{};
  size_t num_{};
  std::vector<u160::U160> samples_;
};

}

