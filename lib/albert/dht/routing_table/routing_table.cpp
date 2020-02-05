#include <albert/dht/routing_table/routing_table.hpp>

#include <random>
#include <memory>
#include <fstream>

#include <boost/asio/ip/address_v4.hpp>

#include <albert/log/log.hpp>
#include <albert/u160/u160.hpp>

namespace albert::dht::routing_table {

void Bucket::split_if_required() {
  if (!(is_leaf() &&
      (fat_mode_ || self_in_bucket()) &&
      known_node_count() > BucketMaxGoodItems)){
    return;
  }
  // ref:
  //  In that case,
  //  the bucket is replaced by two new buckets,
  //  each with half the range of the old bucket,
  //  and the nodes from the old bucket are distributed among the two new ones

  left_ = std::make_unique<Bucket>(this, owner_);
  left_->prefix_ = prefix_;
  left_->prefix_length_ = prefix_length_ + 1;

  right_ = std::make_unique<Bucket>(this, owner_);
  right_->prefix_ = prefix_ | u160::U160::pow2(u160::U160Bits - prefix_length_ - 1);
  right_->prefix_length_ = prefix_length_ + 1;

  for (auto &item : known_nodes_) {
    if (left_->in_bucket(item.first)) {
      left_->known_nodes_.emplace(item.first, std::move(item.second));
    } else {
      assert(right_->in_bucket(item.first));
      right_->known_nodes_.emplace(item.first, std::move(item.second));
    }
  }
  known_nodes_.clear();

  left_->split_if_required();
  right_->split_if_required();
}

void Bucket::merge() {
  assert(!is_leaf());

  for (auto &item : left_->known_nodes_) {
    known_nodes_.emplace(item.first, std::move(item.second));
  }
  for (auto &item : right_->known_nodes_) {
    known_nodes_.emplace(item.first, std::move(item.second));
  }

  left_ = nullptr;
  right_ = nullptr;
}



bool Bucket::add_node(Entry entry) {
  assert(in_bucket(entry.id()));
  if (is_leaf()) {
    bool added = false;
//    if (self_in_bucket() || good_node_count() < BucketMaxGoodItems) {
      known_nodes_.insert(std::make_pair(entry.id(), std::move(entry)));
      added = true;
//    }
    split_if_required();
    return added;
  } else {
    if (left_->in_bucket(entry.id())) {
      return left_->add_node(std::move(entry));
    } else {
      assert(right_->in_bucket(entry.id()));
      return right_->add_node(std::move(entry));
    }
  }
}
void Bucket::encode_(std::ostream &os, int i) {
  if (is_leaf()) {
    os << indent(i) << "{" << std::endl;
    os << indent(i+1) << R"("prefix_length": )" << prefix_length_ << "," << std::endl;
    os << indent(i+1) << R"("prefix": ")" << prefix_.to_string() << "\"," << std::endl;
    os << indent(i+1) << R"("entry_count": )" << known_nodes_.size() << std::endl;
    os << indent(i) << "}" << std::endl;
  } else {
    left_->encode_(os, i);
    right_->encode_(os, i);
  }
}
size_t Bucket::leaf_count() const {
  if (is_leaf()) {
    return 1;
  } else {
    return left_->leaf_count() + right_->leaf_count();
  }
}
void Bucket::encode(std::ostream &os) {
  os << "[" << std::endl;
  encode_(os, 1);
  os << "]" << std::endl;

}
std::list<Entry> Bucket::k_nearest_good_nodes(const u160::U160 &id, size_t k) const {
  assert(k > 0);
  if (is_leaf()) {
    std::list<Entry> results;
    size_t i = 0;
    for (auto &item : known_nodes_) {
      results.push_back(item.second);
      i++;
      if (i >= k) {
        break;
      }
    }
    return results;
  } else {
    if (!id.bit(u160::U160Bits - prefix_length_ - 1)) {
      return left_->k_nearest_good_nodes(id, k);
    } else {
      return right_->k_nearest_good_nodes(id, k);
    }
  }
}
bool Bucket::is_full() const {
  if (is_leaf()) {
    if (self_in_bucket()) {
      return prefix_length_ >= (u160::U160Bits - 1);
    } else {
      return good_node_count() >= owner_->max_bucket_size();
    }
  } else {
    return left_->is_full() && right_->is_full();
  }
}

std::list<std::tuple<Entry, u160::U160>> Bucket::find_some_node_for_filling_bucket(size_t k) const {
  std::list<Entry> selected_nodes;
  std::list<Entry> good_nodes;
  std::list<Entry> questionable_nodes;
  for (auto &item : known_nodes_) {
    if (item.second.is_good()) {
      good_nodes.push_back(item.second);
    } else if (!item.second.is_bad()) {
      questionable_nodes.push_back(item.second);
    }
  }

  // TODO: work around a possibilly glibc issue
  if (!good_nodes.empty()) {
    std::sample(
        good_nodes.begin(),
        good_nodes.end(),
        std::back_inserter(selected_nodes),
        k,
        std::mt19937{std::random_device{}()});
  }

  if (selected_nodes.size() < k && !questionable_nodes.empty()) {
    std::sample(
        questionable_nodes.begin(),
        questionable_nodes.end(),
        std::back_inserter(selected_nodes),
        k - selected_nodes.size(),
        std::mt19937{std::random_device{}()});
  }

  auto virtual_target_id =
      u160::U160::random_from_prefix(prefix_, prefix_length_);
  std::list<std::tuple<Entry, u160::U160>> results;
  for (auto &item : selected_nodes) {
    results.emplace_back(item, virtual_target_id);
  }
  return results;
}
size_t Bucket::total_good_node_count() const {
  if (is_leaf()) {
    return good_node_count();
  } else {
    return left_->total_good_node_count() + right_->total_good_node_count();
  }
}
void Bucket::dfs(const std::function<void(const Bucket &)> &cb) const {
  cb(*this);
  if (!is_leaf()) {
    left_->dfs(cb);
    right_->dfs(cb);
  }
}
bool Bucket::dfs_w(const std::function<bool (Bucket &)> &cb) {
  if (cb(*this)) {
    if (!is_leaf()) {
      if (left_->dfs_w(cb)) {
        return right_->dfs_w(cb);
      }
    } else {
      return true;
    }
  }
  return false;
}
void Bucket::bfs(const std::function<void(const Bucket &)> &cb) const {
  std::list<const Bucket*> queue;
  queue.push_back(this);

  while (!queue.empty()) {
    auto current_node = queue.front();
    queue.pop_front();
    cb(*current_node);

    if (!current_node->is_leaf()) {
      queue.push_back(current_node->left_.get());
      queue.push_back(current_node->right_.get());
    }
  }
}
bool Bucket::self_in_bucket() const {
  return min() <= owner_->self() && owner_->self() <= max();
}
bool Bucket::is_leaf() const {
  return left_ == nullptr && right_ == nullptr;
}
bool Bucket::in_bucket(u160::U160 id) const {
  return min() <= id && id <= max();
}
size_t Bucket::prefix_length() const { return prefix_length_; }

u160::U160 Bucket::min() const {
  return prefix_;
}
bool Bucket::make_good_now(const u160::U160 &id) {
  bool found = false;
  Bucket *bucket = nullptr;
  // TODO: optimize dfs to searching by id prefix
  dfs_w([&found, id, &bucket](Bucket &b) -> bool {
    if (!found && b.is_leaf()) {
      for (auto &item : b.known_nodes_) {
        if (item.first == id) {
          item.second.make_good_now();
          found = true;
          bucket = &b;
          return false;
        }
      }
    }
    return true;
  });
  if (bucket) {
    bucket->split_if_required();
  }
  return found;
}
bool Bucket::make_good_now(uint32_t ip, uint16_t port) {
  bool found = false;
  Bucket *bucket = nullptr;
  dfs_w([&found, &bucket, ip, port](Bucket &b) -> bool {
    if (!found && b.is_leaf()) {
      for (auto &item : b.known_nodes_) {
        if (item.second.ip() == ip && item.second.port() == port) {
          item.second.make_good_now();
          found = true;
          bucket = &b;
          return false;
        }
      }
    }
    return true;
  });
  if (bucket) {
    bucket->split_if_required();
  }
  return found;
}
std::string Bucket::indent(int n) {
  return std::string(n*2, ' ');
}
size_t Bucket::good_node_count() const {
  size_t ret = 0;
  for (auto &item : known_nodes_) {
    if (item.second.is_good()) {
      ret++;
    }
  }
  return ret;
}
size_t Bucket::total_known_node_count() const {
  if (is_leaf()) {
    return known_node_count();
  } else {
    return left_->total_known_node_count() + right_->total_known_node_count();
  }
}
size_t Bucket::known_node_count() const {
  return this->known_nodes_.size();
}
void Bucket::iterate_entries(const std::function<void(const Entry &)> &cb) const {
  for (auto &item : known_nodes_) {
    cb(item.second);
  }
}
std::optional<Entry> Bucket::remove(const u160::U160 &id) {
  std::optional<Entry> ret;
  dfs_w([id, &ret](Bucket &bucket) -> bool {
    if (bucket.known_nodes_.find(id) != bucket.known_nodes_.end()) {
      ret.emplace(std::move(bucket.known_nodes_[id]));
      bucket.known_nodes_.erase(id);
      return false;
    }
    return true;
  });
  return std::move(ret);
}
bool Bucket::require_response_now(const u160::U160 &target) {
  auto entry = search(target);
  if (entry) {
    entry->require_response_now();
    return true;
  } else {
    return false;
  }
}
Entry *Bucket::search(const u160::U160 &id) {
  Entry *ret = nullptr;
  bool found = false;
  dfs_w([&ret, &found, &id](Bucket &bucket) -> bool {
    if (!found && bucket.is_leaf()) {
      for (auto &item : bucket.known_nodes_) {
        if (item.first == id) {
          ret = &item.second;
          return false;
        }
      }
    }
    return true;
  });
  return ret;

}
std::tuple<size_t, size_t, size_t, std::list<krpc::NodeInfo>> Bucket::gc() {
  if (is_leaf()) {
    std::list<krpc::NodeInfo> nodes_to_delete;
    std::vector<krpc::NodeInfo> questionable_nodes, good_nodes;
    size_t n_good = 0, n_non_bad = 0, n_bad = 0;
    for (auto &node : known_nodes_) {
      if (node.second.is_bad()) {
        LOG(debug) << "Bucket::gc() prefix " << prefix_length_ << " delete bad node " << node.second.to_string();
        nodes_to_delete.push_back(node.second.node_info());
        owner_->black_list_node(node.second.ip(), node.second.port());
        n_bad++;
      } else if (node.second.is_good()) {
        n_good++;
        n_non_bad++;
        good_nodes.push_back(node.second.node_info());
      } else {
        n_non_bad++;
        questionable_nodes.push_back(node.second.node_info());
      }
    }
    size_t n_good_deleted = 0, n_questionable_deleted = 0;

    // If routing table is too big, we delete questionable nodes first, then good nodes.
    if (n_non_bad > owner_->max_bucket_size()) {
      LOG(debug) << "Bucket::gc() prefix " << prefix_length_ << " non_bad count " << n_non_bad << " delete extra nodes";
      for (size_t i = 0; i < std::min(n_non_bad - owner_->max_bucket_size(), questionable_nodes.size()); i++) {
        LOG(debug) << "Bucket::gc() prefix " << prefix_length_ << " delete questionable node " << questionable_nodes[i].to_string();
        nodes_to_delete.push_back(questionable_nodes[i]);
        n_questionable_deleted++;
      }
    }
    if (n_good > owner_->max_bucket_size()) {
      LOG(debug) << "Bucket::gc() prefix " << prefix_length_ << " good count " << n_good << " delete extra nodes";
      for (size_t i = 0; i < good_nodes.size() - owner_->max_bucket_size(); i++) {
        LOG(debug) << "Bucket::gc() prefix " << prefix_length_ << " delete good node " << good_nodes[i].to_string();
        nodes_to_delete.push_back(good_nodes[i]);
        n_good_deleted++;
      }
    }

    for (auto &node : nodes_to_delete) {
      known_nodes_.erase(node.id());
    }
    return {n_good_deleted, n_questionable_deleted, n_bad, nodes_to_delete};
  } else {
    size_t a1, a2, b1, b2, c1, c2;
    std::list<krpc::NodeInfo> l, r;
    std::tie(a1, b1, c1, l) = right_->gc();
    std::tie(a2, b2, c2, r) = left_->gc();
    l.merge(r);

    // merge buckets when all the sub-buckets are no bigger than max/2
    if (left_->is_leaf() && right_->is_leaf()) {
      if (left_->known_node_count() + right_->known_node_count() < owner_->max_bucket_size() / 2) {
        merge();
      }
    }

    return {a1 + a2, b1 + b2, c1 + c2, l};
  }
}

void Bucket::make_bad(uint32_t ip, uint16_t port) {
  dfs_w([ip, port](Bucket &b) -> bool {
    if (b.is_leaf()) {
      for (auto &item : b.known_nodes_) {
        if (item.second.ip() == ip && item.second.port() == port) {
          item.second.make_bad();
          return false;
        }
      }
    }
    return true;
  });
}
size_t Bucket::memory_size() const {
  size_t ret = sizeof(*this);
  ret += known_node_count() * (sizeof(u160::U160) + sizeof(Entry));
  if (left_) {
    ret += left_->memory_size();
  }
  if (right_) {
    ret += right_->memory_size();
  }
  return ret;
}
void Bucket::remove(uint32_t ip, uint16_t port) {
  dfs_w([ip, port](Bucket &bucket) -> bool {
    for (auto &item : bucket.known_nodes_) {
      if (item.second.ip() == ip && item.second.port() == port) {
        bucket.known_nodes_.erase(item.first);
        return false;
      }
    }
    return true;
  });
}

std::list<std::tuple<Entry, u160::U160>> RoutingTable::select_expand_route_targets() {
  std::list<std::tuple<Entry, u160::U160>> entries;
  root_.bfs([&entries](const Bucket &bucket) {
    if (bucket.is_leaf()) {
      entries.splice(entries.end(), bucket.find_some_node_for_filling_bucket(1));
    }
  });
  return entries;
}
void RoutingTable::encode(std::ostream &os) {
  os << "{"  << std::endl;
  os << R"("type": "routing_table",)" << std::endl;
  os << R"("self_id": ")" << self_id_.to_string() << "\"," << std::endl;
  os << R"("data": )" << std::endl;
  root_.encode(os);
  os << "}" << std::endl;
}

struct TrieLevelStat {
  size_t good = 0;
  size_t known = 0;
  size_t buckets = 0;
};

void RoutingTable::stat() const {
  LOG(info) << "Routing Table: ";
  if (fat_mode_) {
    std::map<size_t, TrieLevelStat> level_stat;
    root_.bfs([&level_stat](const Bucket &bucket) {
      if (bucket.is_leaf()) {
        auto p = bucket.prefix_length();
        level_stat[p].good += bucket.good_node_count();
        level_stat[p].known += bucket.known_node_count();
        level_stat[p].buckets += 1;
      }
    });
    for (auto &item : level_stat) {
      LOG(debug) << "  depth=" << item.first << ", buckets=" << item.second.buckets << " " << item.second.good << "/" << item.second.known;
    }
  } else {
    root_.bfs([](const Bucket &bucket) {
      if (bucket.is_leaf()) {
        LOG(debug) << "  len(p)=" << bucket.prefix_length()
                   << ", " << bucket.good_node_count() << "/" << bucket.known_node_count();
      }
    });
  }
  LOG(info) << "  total entries: " << root_.total_known_node_count();
  LOG(info) << "  total good entries: " << root_.total_good_node_count();
  LOG(info) << "  total node added: " << total_node_added_;
  LOG(info) << "  total good deleted: " << total_good_node_deleted_;
  LOG(info) << "  total questionable deleted: " << total_questionable_node_deleted_;
  LOG(info) << "  total bad deleted: " << total_bad_node_deleted_;
  LOG(info) << "  total bucket count: " << bucket_count();
}
bool RoutingTable::make_good_now(const u160::U160 &id) {
  return root_.make_good_now(id);
}
bool RoutingTable::add_node(Entry entry) {
  if (reverse_map_.find({entry.ip(), entry.port()}) == reverse_map_.end()) {
    reverse_map_[std::make_tuple(entry.ip(), entry.port())] = entry.id();
    if (is_full()) {
      LOG(debug) << "failed to add node because routing table is full";
      return false;
    } else {
      if (root_.add_node(std::move(entry))) {
        total_node_added_++;
        return true;
      } else {
        LOG(debug) << "failed to add node because routing table bucket is full";
        return false;
      }
    }
  } else {
    if (entry.id() == reverse_map_[{entry.ip(), entry.port()}]) {
      if (!root_.in_bucket(entry.node_info().id())) {
        LOG(error) << "!!!!!!!!!!!!!!!!!!! error, routing table inconsistent";
      }
    } else {
      black_list_node(entry.ip(), entry.port());
      reverse_map_.erase({entry.ip(), entry.port()});
      make_bad(entry.ip(), entry.port());
      LOG(debug) << "banned node " << entry.to_string() << " because it has multiple node IDs";
    }
    return false;
  }

}
bool RoutingTable::make_good_now(uint32_t ip, uint16_t port) {
  return root_.make_good_now(ip, port);
}
void RoutingTable::iterate_nodes(const std::function<void(const Entry &)> &callback) const {
  root_.dfs([&callback](const Bucket &bucket) {
    if (bucket.is_leaf()) {
      bucket.iterate_entries(callback);
    }
  });
}
std::optional<Entry> RoutingTable::remove_node(const u160::U160 &target) {
  auto node = root_.remove(target);
  if (node.has_value()) {
    reverse_map_.erase({node->ip(), node->port()});
  }
  return node;
}

void RoutingTable::gc() {
  auto t0 = std::chrono::high_resolution_clock::now();

  size_t bad{}, good{}, quest;
  std::list<krpc::NodeInfo> info;
  std::tie(good, quest, bad, info) = root_.gc();
  total_good_node_deleted_ += good;
  total_questionable_node_deleted_ += quest;
  total_bad_node_deleted_ += bad;
  for (auto &node : info) {
    reverse_map_.erase({node.ip(), node.port()});
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  LOG(info) << "RoutingTable::gc() good/bad/questionable = " << good << "/" << bad << "/" << quest << " in "
            << std::fixed << std::setprecision(2) << std::chrono::duration<double, std::milli>(t1 - t0).count() << "ms";
}
size_t RoutingTable::max_prefix_length() const {
  size_t length = 0;
  root_.dfs([&length](const Bucket &bucket) {
    if (bucket.prefix_length() > length) {
      length = bucket.prefix_length();
    }
  });
  return length;
}
size_t RoutingTable::known_node_count() const {
  return root_.total_known_node_count();
}
size_t RoutingTable::good_node_count() const {
  return root_.total_good_node_count();
}

bool RoutingTable::is_full() const {
  return root_.total_known_node_count() >= max_known_nodes_ || root_.is_full();
}

bool RoutingTable::require_response_now(const u160::U160 &target) {
  return root_.require_response_now(target);
}

std::list<Entry> RoutingTable::k_nearest_good_nodes(const u160::U160 &id, size_t k) const {
  return root_.k_nearest_good_nodes(id, k);
}

void RoutingTable::serialize(std::ostream &os) const {
  iterate_nodes([&os](const Entry &entry) {
    if (entry.is_good()) {
      os << entry.id().to_string() << " "
         << boost::asio::ip::address_v4(entry.ip()) << " "
         << entry.port() << std::endl;
    }
  });
}

std::unique_ptr<RoutingTable> RoutingTable::deserialize(
    std::istream &is, std::string name, std::string save_path, size_t max_bucket_size, size_t max_known_nodes,
    bool delete_good_nodes, bool fat_mode,
    std::function<void(uint32_t, uint16_t)> black_list_node) {
  std::string node_id;
  std::string ip;
  uint16_t port;
  auto ret = std::make_unique<RoutingTable>(u160::U160(), std::move(name), std::move(save_path), max_bucket_size, max_known_nodes,
                                            delete_good_nodes, fat_mode, std::move(black_list_node));
  while (is) {
    is >> node_id >> ip >> port;
    if (!is.good() && !is.eof()) {
      throw std::invalid_argument("Invalid routing table format: column parsing failure");
    }
    auto node = u160::U160::from_hex(node_id);
    auto ip_address = boost::asio::ip::address_v4::from_string(ip);
    ret->add_node(Entry{krpc::NodeInfo{node, ip_address.to_uint(), port}});
  }
  return std::move(ret);
}

RoutingTable::~RoutingTable() {
  if (!save_path_.empty()) {
    LOG(info) << "Saving routing table to file '" << save_path_  << "'";
    std::ofstream save_file(save_path_);
    serialize(save_file);
  }
}
void RoutingTable::make_bad(uint32_t ip, uint16_t port) {
  root_.make_bad(ip, port);
}
size_t RoutingTable::bucket_count() const {
  size_t n = 0;
  root_.dfs([&n](const Bucket &b) {
    if (b.is_leaf() && b.good_node_count() > 0) {
      n++;
    }
  });
  return n;
}
void RoutingTable::black_list_node(uint32_t ip, uint16_t port) const {
  if (black_list_node_) {
    black_list_node_(ip, port);
  }
}
size_t RoutingTable::memory_size() const {
  size_t size = sizeof(*this);
  size += root_.memory_size() - sizeof(root_);
  size += name_.size();
  size += save_path_.size();
  size += (sizeof(reverse_map_.begin()->first) + sizeof(reverse_map_.begin()->second)) * reverse_map_.size();
  return size;
}

bool Entry::is_good() const {
  // A good node is a node has responded to one of our queries within the last 15 minutes,
  // A node is also good if it has ever responded to one of our queries and has sent us a query within the last 15 minutes

  return !is_bad() &&
      (std::chrono::high_resolution_clock::now() - last_seen_) < std::chrono::minutes(MaxGoodNodeAliveMinutes);
}

bool Entry::is_bad() const {
  if (!response_required)
    return false;

  return (std::chrono::high_resolution_clock::now() - last_require_response_) > krpc::KRPCTimeout || bad_;
}

bool Entry::require_response_now() {
  if (!response_required) {
    response_required = true;
    this->last_require_response_ = std::chrono::high_resolution_clock::now();
    LOG(trace) << "require response " << to_string();
    return true;
  } else {
    return false;
  }
}

void Entry::make_good_now() {
  this->last_seen_ = std::chrono::high_resolution_clock::now();
  this->response_required = false;
  this->bad_ = false;
}

void Entry::make_bad() {
  bad_ = true;
}
}
