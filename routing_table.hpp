#pragma once
#include "krpc.hpp"

#include <cstdint>
#include <iostream>
#include <list>
#include <functional>
#include <memory>
#include <map>
#include <string>

namespace dht {

class Entry {
 public:
  explicit Entry(krpc::NodeID id) :id_(id) { }
  Entry(krpc::NodeID id, uint32_t ip, uint16_t port)
      :id_(id), ip_(ip), port_(port) { }

  [[nodiscard]]
  krpc::NodeID id() const { return id_; }

  [[nodiscard]]
  uint32_t ip() const { return ip_; }

  [[nodiscard]]
  uint16_t port() const { return port_; }

  bool operator<(const Entry &rhs) const {
    return id_ < rhs.id_;
  }
 private:
  krpc::NodeID id_{};
  uint32_t ip_{};
  uint16_t port_{};
};

// ref:
//  Each bucket can only hold K nodes, currently eight, before becoming "full."
const size_t BucketMaxItems = 8;
class Bucket {
 public:
  Bucket(krpc::NodeID self_id, Bucket *parent) :self_(self_id), parent_(parent) {}
  void add_node(Entry entry);

  bool self_in_bucket() const;
  bool in_bucket(krpc::NodeID id) const;
  bool is_leaf() const;
  bool is_full() const;
  size_t prefix_length() const;
  krpc::NodeID prefix() const { return prefix_; }
  size_t leaf_count() const;
  size_t total_good_node_count() const;
  size_t good_node_count() const { return this->good_nodes_.size(); }

  void dfs(const std::function<void (const Bucket&)> &cb) const;
  void bfs(const std::function<void (const Bucket&)> &cb) const;

  void split();

  void encode(std::ostream &os);

  [[nodiscard]]
  std::list<Entry> k_nearest_good_nodes(const krpc::NodeID &id, size_t k) const;

  [[nodiscard]]
  std::list<Entry> find_some_node_for_filling_bucket(size_t k) const;
 private:
  static std::string indent(int n) {
    return std::string(n*2, ' ');
  }
  void encode_(std::ostream &os, int i);

 private:
  [[nodiscard]]
  krpc::NodeID min() const {
    return prefix_;
  }

  [[nodiscard]]
  krpc::NodeID max() const {
    auto ret  = prefix_ | krpc::NodeID::pow2m1(krpc::NodeIDBits - prefix_length_);
    return ret;
  }

 private:
  // nodes are sorted in one buckets
  std::map<krpc::NodeID, Entry> good_nodes_{};

  /**
   * How `prefix`, `min` and `max` Are Related.
   *
   * The definition of min and max are the similar to BEP0005,
   *    with one slight difference:
   *        min <= id <= max.
   * While in BEP0005, min <= id < max.
   * We define it this way to prevent `max` from overflow.
   *
   * However we do not store min and max directly.
   * Instead, we store it as a Common Prefix,
   *   because we know that the difference min and max is minimal, only after the common prefix.
   *
   * We can calculate the binary representation of min and max as follows:
   *    min = prefix | "0" * (NodeIDBits - prefix_length)
   *    max = prefix | "1" * (NodeIDBits - prefix_length)
   */

  // the longest common prefix length of min and max
  krpc::NodeID prefix_{};

  // 0 <= prefix_length < NodeIDBits
  int prefix_length_{};
  std::unique_ptr<Bucket> left_{}, right_{};
  Bucket *parent_;
  krpc::NodeID self_{};

};

class RoutingTable {
 public:
  explicit RoutingTable(krpc::NodeID self_id) :root_(self_id, nullptr), self_id_(self_id) {}
  void add_node(Entry entry) {
    root_.add_node(entry);
  }

  [[nodiscard]]
  bool is_full() const {
    return root_.is_full();
  }

  [[nodiscard]]
  size_t size() const {
    return root_.total_good_node_count();
  }

  void stat(std::ostream &os) const;
  // encode to json
  void encode(std::ostream &os);

  std::list<Entry> select_expand_route_targets();

 private:
  Bucket root_;
  krpc::NodeID self_id_;
};

}