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
#include <optional>
#include <string>
#include <utility>

#include <boost/asio/ip/address_v4.hpp>

#include <albert/u160/u160.hpp>

namespace albert::dht::routing_table {

const int MaxGoodNodeAliveMinutes = 15;

class Entry {
 public:
  explicit Entry(krpc::NodeInfo info, const std::string &version) :info_(std::move(info)), version_(version) { }
  Entry(u160::U160 id, uint32_t ip, uint16_t port, const std::string &version)
      :info_(id, ip, port), version_(version) { }

  Entry(const Entry &rhs) = default;
  Entry(Entry &&rhs) = default;

  [[nodiscard]]
  krpc::NodeInfo node_info() const { return info_; }

  [[nodiscard]]
  u160::U160 id() const { return info_.id(); }

  [[nodiscard]]
  uint32_t ip() const { return info_.ip(); }

  [[nodiscard]]
  uint16_t port() const { return info_.port(); }

  [[nodiscard]]
  std::string version() const { return version_; }

  bool operator<(const Entry &rhs) const {
    return info_.id() < rhs.info_.id();
  }

  [[nodiscard]]
  bool is_good() const noexcept ;

  [[nodiscard]]
  bool is_bad() const;

  void make_good_now();
  void make_bad();
  bool require_response_now();

  [[nodiscard]]
  std::string to_string() const {
    return info_.to_string() + "@" + version();
  }

 private:
  const krpc::NodeInfo info_;
  const std::string version_;

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
  Bucket(const RoutingTable *owner, bool fat_mode)
      :parent_(nullptr), owner_(owner), fat_mode_(fat_mode) {}

  Bucket(Bucket *parent, const RoutingTable *owner)
      :parent_(parent),
       owner_(owner),
       fat_mode_(parent ? parent->fat_mode_ : false) {
    if (parent == nullptr) {
      throw std::invalid_argument("Bucket constructor, parent should not be nullptr");
    }
  }

  // Attribute accessor
  bool self_in_bucket() const;
  bool in_bucket(u160::U160 id) const;
  bool is_leaf() const;
  bool is_full() const;
  size_t prefix_length() const;
  u160::U160 prefix() const { return prefix_; }
  size_t leaf_count() const;
  size_t total_good_node_count() const;
  size_t good_node_count() const;
  size_t total_known_node_count() const;
  size_t known_node_count() const;
  size_t memory_size() const;
  std::list<Entry> k_nearest_good_nodes(const u160::U160 &id, size_t k) const;


  // Node manipulating functions
  bool add_node(Entry entry);
  std::optional<Entry> remove(const u160::U160 &id);
  void remove(uint32_t ip, uint16_t port);
  std::tuple<size_t, size_t, size_t, std::list<krpc::NodeInfo>> gc();

  // Iteration helpers
  void dfs(const std::function<void (const Bucket&)> &cb) const;
  void bfs(const std::function<void (const Bucket&)> &cb) const;
  void iterate_entries(const std::function<void (const Entry&)> &cb) const;

  // Node status management functions
  bool require_response_now(const u160::U160 &target);
  bool make_good_now(const u160::U160 &id);
  bool make_good_now(uint32_t ip, uint16_t port);
  void make_bad(uint32_t ip, uint16_t port);

  void encode(std::ostream &os);

  [[nodiscard]]
  std::list<std::tuple<Entry, u160::U160>> find_some_node_for_filling_bucket(size_t k) const;

 private:
  static std::string indent(int n);
  void encode_(std::ostream &os, int i);

  // Other helper functions
  void split_if_required();
  void merge();

 private:
  // NOTE:
  //  This function is quite dangerous.
  //  as changing `entry.id_` may cause the map key and value.id inconsistent
  bool dfs_w(const std::function<bool (Bucket&)> &cb);
  Entry *search(const u160::U160 &id);

  [[nodiscard]]
  u160::U160 min() const;

  [[nodiscard]]
  u160::U160 max() const {
    auto ret  = prefix_ | u160::U160::pow2m1(u160::U160Bits - prefix_length_);
    return ret;
  }

 private:
  // nodes are sorted in one buckets
  std::map<u160::U160, Entry> known_nodes_{};

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
  u160::U160 prefix_{};

  // 0 <= prefix_length < NodeIDBits
  int prefix_length_{};
  std::unique_ptr<Bucket> left_{}, right_{};
  Bucket *parent_;
  bool fat_mode_ = false;

  const RoutingTable *owner_;
};

class RoutingTable {
 public:
  explicit RoutingTable(
      u160::U160 self_id, std::string name, std::string save_path, size_t max_bucket_size, size_t max_known_nodes,
      bool delete_good, bool fat_mode, std::function<void(uint32_t, uint16_t)> black_list_node)
      :root_(this, fat_mode),
       self_id_(self_id),
       save_path_(std::move(save_path)),
       name_(std::move(name)),
       max_bucket_size_(max_bucket_size),
       delete_good_nodes_(delete_good),
       fat_mode_(fat_mode),
       black_list_node_(std::move(black_list_node)),
       max_known_nodes_(max_known_nodes) {}

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
      size_t max_known_nodes,
      bool delete_good_nodes,
      bool fat_mode,
      std::function<void(uint32_t, uint16_t)> black_list_node);

  std::list<std::tuple<Entry, u160::U160>> select_expand_route_targets();

  // This is the only function that insert a node into routing table
  bool add_node(Entry entry);
  std::optional<Entry> remove_node(const u160::U160 &target);
  void gc();

  bool make_good_now(const u160::U160 &id);
  bool make_good_now(uint32_t ip, uint16_t port);
  void make_bad(uint32_t ip, uint16_t port);
  bool require_response_now(const u160::U160 &target);

  void iterate_nodes(const std::function<void (const Entry &)> &callback) const;

  [[nodiscard]]
  std::list<Entry> k_nearest_good_nodes(const u160::U160 &id, size_t k) const;

  [[nodiscard]]
  const std::string &name() const { return name_; }
  void name(std::string value) { name_ = std::move(value); }

  [[nodiscard]]
  u160::U160 self() const { return self_id_; }

  [[nodiscard]]
  size_t max_bucket_size() const { return max_bucket_size_; }
  [[nodiscard]]
  bool delete_good_nodes() const { return delete_good_nodes_; }

  void black_list_node(uint32_t ip, uint16_t port) const;

  size_t memory_size() const;

 private:
  Bucket root_;
  u160::U160 self_id_;
  std::string save_path_;

  size_t total_node_added_{};
  size_t total_bad_node_deleted_{};
  size_t total_good_node_deleted_{};
  size_t total_questionable_node_deleted_{};

  std::string name_;
  size_t max_bucket_size_ = BucketMaxGoodItems;
  bool delete_good_nodes_ = true;
  bool fat_mode_;

  size_t max_known_nodes_;

  std::function<void(uint32_t, uint16_t)> black_list_node_;

  std::map<std::tuple<uint32_t, uint16_t>, u160::U160> reverse_map_;
};

}