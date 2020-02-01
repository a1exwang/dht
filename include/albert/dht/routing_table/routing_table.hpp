#pragma once
#include "albert/krpc/krpc.hpp"

#include <cstdint>

#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <string>

#include <boost/asio/ip/address_v4.hpp>
#include <utility>

namespace albert::dht::routing_table {

const int MaxGoodNodeAliveMinutes = 15;

class Entry {
 public:
  explicit Entry(krpc::NodeInfo info) :info_(std::move(info)) { }
  explicit Entry(krpc::NodeID id) :info_(id, 0, 0) { }
  Entry(krpc::NodeID id, uint32_t ip, uint16_t port)
      :info_(id, ip, port) { }
  Entry() = default;

  [[nodiscard]]
  krpc::NodeInfo node_info() const { return info_; }

  [[nodiscard]]
  krpc::NodeID id() const { return info_.id(); }

  [[nodiscard]]
  uint32_t ip() const { return info_.ip(); }

  [[nodiscard]]
  uint16_t port() const { return info_.port(); }

  bool operator<(const Entry &rhs) const {
    return info_.id() < rhs.info_.id();
  }

  [[nodiscard]]
  bool is_good() const;

  [[nodiscard]]
  bool is_bad() const;

  void make_good_now();
  void make_bad();
  void require_response_now();

  [[nodiscard]]
  std::string to_string() const { return info_.to_string(); }

 private:
  krpc::NodeInfo info_;

  std::chrono::high_resolution_clock::time_point last_seen_{};
  bool response_required = false;
  std::chrono::high_resolution_clock::time_point last_require_response_{};

  bool bad_ = false;
};

// ref:
//  Each bucket can only hold K nodes, currently eight, before becoming "full."
const size_t BucketMaxGoodItems = 8;
const size_t BucketMaxItems = 32;
class RoutingTable;
class Bucket {
 public:
  Bucket(krpc::NodeID self_id, const RoutingTable *owner, bool fat_mode)
      :self_(self_id), parent_(nullptr), owner_(owner), fat_mode_(fat_mode) {}

  Bucket(Bucket *parent, const RoutingTable *owner)
      :self_(parent ? parent->self_ : /* this should not happen*/krpc::NodeID()),
       parent_(parent),
       owner_(owner),
       fat_mode_(parent ? parent->fat_mode_ : false) {
    if (parent == nullptr) {
      throw std::invalid_argument("Bucket construct, parent should not be nullptr");
    }
  }
  bool add_node(const Entry &entry);

  bool self_in_bucket() const;
  bool in_bucket(krpc::NodeID id) const;
  bool is_leaf() const;
  bool is_full() const;
  size_t prefix_length() const;
  krpc::NodeID prefix() const { return prefix_; }
  size_t leaf_count() const;
  size_t total_good_node_count() const;
  size_t good_node_count() const;
  size_t total_known_node_count() const;
  size_t known_node_count() const;
  std::tuple<size_t, size_t, size_t> gc();

  void remove(const krpc::NodeID &id);

  void dfs(const std::function<void (const Bucket&)> &cb) const;
  void bfs(const std::function<void (const Bucket&)> &cb) const;
  void iterate_entries(const std::function<void (const Entry&)> &cb) const;
  bool require_response_now(const krpc::NodeID &target);

  bool make_good_now(const krpc::NodeID &id);
  bool make_good_now(uint32_t ip, uint16_t port);
  void make_bad(uint32_t ip, uint16_t port);

  void split_if_required();

  void encode(std::ostream &os);

  [[nodiscard]]
  std::list<Entry> k_nearest_good_nodes(const krpc::NodeID &id, size_t k) const;

  [[nodiscard]]
  std::list<std::tuple<Entry, krpc::NodeID>> find_some_node_for_filling_bucket(size_t k) const;
 private:
  static std::string indent(int n);
  void encode_(std::ostream &os, int i);

 private:
  // NOTE:
  //  This function is quite dangerous.
  //  as changing `entry.id_` may cause the map key and value.id inconsistent
  void dfs_w(const std::function<void (Bucket&)> &cb);
  Entry *search(const krpc::NodeID &id);

  [[nodiscard]]
  krpc::NodeID min() const;

  [[nodiscard]]
  krpc::NodeID max() const {
    auto ret  = prefix_ | krpc::NodeID::pow2m1(krpc::NodeIDBits - prefix_length_);
    return ret;
  }

 private:
  // nodes are sorted in one buckets
  std::map<krpc::NodeID, Entry> known_nodes_{};

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
  bool fat_mode_ = false;

  const RoutingTable *owner_;
};

class RoutingTable {
 public:
  explicit RoutingTable(
      krpc::NodeID self_id, std::string name, std::string save_path, size_t max_bucket_size,
      bool delete_good, bool fat_mode, std::function<void(uint32_t, uint16_t)> black_list_node)
      :root_(self_id, this, fat_mode),
       self_id_(self_id),
       save_path_(std::move(save_path)),
       name_(std::move(name)),
       max_bucket_size_(max_bucket_size),
       delete_good_nodes_(delete_good),
       fat_mode_(fat_mode),
       black_list_node_(std::move(black_list_node)) {}

  ~RoutingTable();

  [[nodiscard]]
  bool is_full() const;

  [[nodiscard]]
  size_t good_node_count() const;

  [[nodiscard]]
  size_t max_prefix_length() const;

  [[nodiscard]]
  size_t known_node_count() const;

  [[nodiscard]]
  size_t bucket_count() const;

  void stat() const;
  // encode to json
  void encode(std::ostream &os);

  void serialize(std::ostream &os) const;
  static std::unique_ptr<RoutingTable> deserialize(
      std::istream &is,
      std::string name,
      std::string save_path,
      size_t max_bucket_size,
      bool delete_good_nodes,
      bool fat_mode,
      std::function<void(uint32_t, uint16_t)> black_list_node);

  std::list<std::tuple<Entry, krpc::NodeID>> select_expand_route_targets();

  bool add_node(Entry entry);
  void remove_node(const krpc::NodeID &target);
  bool require_response_now(const krpc::NodeID &target);

  bool make_good_now(const krpc::NodeID &id);
  bool make_good_now(uint32_t ip, uint16_t port);

  void make_bad(uint32_t ip, uint16_t port);

  void iterate_nodes(const std::function<void (const Entry &)> &callback) const;
  void gc();

  [[nodiscard]]
  std::list<Entry> k_nearest_good_nodes(const krpc::NodeID &id, size_t k) const;

  [[nodiscard]]
  const std::string &name() const { return name_; }
  void name(std::string value) { name_ = std::move(value); }

  [[nodiscard]]
  krpc::NodeID self() const { return self_id_; }

  [[nodiscard]]
  size_t max_bucket_size() const { return max_bucket_size_; }
  [[nodiscard]]
  bool delete_good_nodes() const { return delete_good_nodes_; }

  void black_list_node(uint32_t ip, uint16_t port) const;

 private:
  Bucket root_;
  krpc::NodeID self_id_;
  std::string save_path_;

  size_t total_node_added_{};
  size_t total_bad_node_deleted_{};
  size_t total_good_node_deleted_{};
  size_t total_questionable_node_deleted_{};

  std::string name_;
  size_t max_bucket_size_ = BucketMaxGoodItems;
  bool delete_good_nodes_ = true;
  bool fat_mode_;

  std::function<void(uint32_t, uint16_t)> black_list_node_;
};

}