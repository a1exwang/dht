#include "routing_table.hpp"
#include "log.hpp"

#include <random>
#include <memory>

namespace dht {

void dht::Bucket::split() {
  assert(is_leaf());
  // ref:
  //  In that case,
  //  the bucket is replaced by two new buckets,
  //  each with half the range of the old bucket,
  //  and the nodes from the old bucket are distributed among the two new ones

  left_ = std::make_unique<Bucket>(self_, this);
  left_->prefix_ = prefix_;
  left_->prefix_length_ = prefix_length_ + 1;

  right_ = std::make_unique<Bucket>(self_, this);
  right_->prefix_ = prefix_ | krpc::NodeID::pow2(krpc::NodeIDBits - prefix_length_ - 1);
  right_->prefix_length_ = prefix_length_ + 1;

  for (auto &item : known_nodes_) {
    if (left_->in_bucket(item.first)) {
      left_->known_nodes_.insert(item);
    } else {
      assert(right_->in_bucket(item.first));
      right_->known_nodes_.insert(item);
    }
  }

  if (left_->good_node_count() > BucketMaxGoodItems) {
    left_->split();
  }
  if (right_->good_node_count() > BucketMaxGoodItems) {
    right_->split();
  }
}


void Bucket::add_node(Entry entry) {
  assert(in_bucket(entry.id()));
  if (is_leaf()) {
    if (self_in_bucket()) {
      known_nodes_.insert({entry.id(), entry});
      if (good_node_count() > BucketMaxGoodItems) {
        split();
      }
    } else {
      if (good_node_count() < BucketMaxGoodItems) {
        known_nodes_.insert({entry.id(), entry});
      }
    }
  } else {
    if (left_->in_bucket(entry.id())) {
      left_->add_node(entry);
    } else {
      assert(right_->in_bucket(entry.id()));
      right_->add_node(entry);
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
std::list<Entry> Bucket::k_nearest_good_nodes(const krpc::NodeID &id, size_t k) const {
  assert(k > 0);
  if (is_leaf()) {
    std::list<Entry> results;
    size_t i = 0;
    for (auto item : known_nodes_) {
      results.push_back(item.second);
      i++;
      if (i >= k) {
        break;
      }
    }
//    auto itl = good_nodes_.lower_bound(id);
//    // We return the nearest using a merge-sort like algorithm
//    if (itl == good_nodes_.end()) {
//      if (itl == good_nodes_.begin()) {
//        // good_nodes_ is empty
//      } else {
//        itl--;
//        results.push_back(itl->second);
//        for (int i = 0; i < k && itl != good_nodes_.begin(); i++, itl--) {
//          results.push_back(itl->second);
//        }
//      }
//    } else /* itl != goode_nodes.end() */ {
//      results.push_back(itl->second);
//      auto itr = itl;
//      itr++;
//      // From now one.
//      // `itl` always points to the left-most element that we've used,
//      //    and `it` cannot point to a non-existing position.
//      // `itl` always moves left.
//
//      // `itr` always points to the left-most element that we've not used.
//      //    and `it2` may point to a non-existing position(e.g. good_nodes.end()).
//      // `itr` always moves right.
//      while (results.size() < k) {
//        if (itl == good_nodes_.begin() && itr == good_nodes_.end()) {
//          break;
//        } else if (itl == good_nodes_.begin()) {
//          results.push_back((itr++)->second);
//        } else if (itr == good_nodes_.end()) {
//          results.push_front((--itl)->second);
//        } else {
//          auto itl_bk = itl;
//          itl_bk--;
//          // find the nearer one
//          if (id.is_first_nearer(itl_bk->first, itr->first)) {
//            results.push_front((--itl)->second);
//          } else {
//            results.push_back((itr++)->second);
//          }
//        }
//      }
//    }
    return results;
  } else {
    if (!id.bit(krpc::NodeIDBits - prefix_length_ - 1)) {
      return left_->k_nearest_good_nodes(id, k);
    } else {
      return right_->k_nearest_good_nodes(id, k);
    }
  }
}
bool Bucket::is_full() const {
  if (is_leaf()) {
    if (self_in_bucket()) {
      return prefix_length_ >= (krpc::NodeIDBits - 1);
    } else {
      return this->good_node_count() >= BucketMaxGoodItems;
    }
  } else {
    return left_->is_full() && right_->is_full();
  }
}

std::list<Entry> Bucket::find_some_node_for_filling_bucket(size_t k) const {

  std::map<krpc::NodeID, Entry> results2;
  int seed = 1;
  // TODO: work around a possibilly glibc issue
  if (known_nodes_.begin() == known_nodes_.end()) {
    return {};
  }
  // TODO: priorize good nodes
  std::sample(
      known_nodes_.begin(),
      known_nodes_.end(),
      std::inserter(results2, results2.begin()),
      k,
      std::mt19937{std::random_device{}()});
//  if (good_nodes_.size() >= k) {
//    std::copy(good_nodes_.begin(), std::next(good_nodes_.begin(), k), std::back_inserter(results2));
//  } else {
//    std::copy(good_nodes_.begin(), good_nodes_.end(), std::back_inserter(results2));
//  }

//  std::sample(
//      good_nodes_.begin(),
//      good_nodes_.end(), std::back_inserter(results2),
//      k,
//      std::mt19937(seed));
//

  std::list<Entry> results;

  for (auto item : results2) {
    results.push_back(item.second);
  }
  if (results.size() < k) {
    if (parent_) {
      results.splice(results.end(), parent_->find_some_node_for_filling_bucket(k - results.size()));
    }
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
void Bucket::dfs_w(const std::function<void(Bucket &)> &cb) {
  cb(*this);
  if (!is_leaf()) {
    left_->dfs_w(cb);
    right_->dfs_w(cb);
  }
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
  return min() <= self_ && self_ <= max();
}
bool Bucket::is_leaf() const {
  return left_ == nullptr && right_ == nullptr;
}
bool Bucket::in_bucket(krpc::NodeID id) const {
  return min() <= id && id <= max();
}
size_t Bucket::prefix_length() const { return prefix_length_; }

const Entry *Bucket::search(uint32_t ip, uint16_t port) const {
  const Entry *ret = nullptr;
  bool found = false;
  dfs([&ret, &found, ip, port](const Bucket &bucket) {
    if (!found && bucket.is_leaf()) {
      for (auto &item : bucket.known_nodes_) {
        auto &node = item.second;
        if (node.ip() == ip && node.port() == port) {
          ret = &node;
          return;
        }
      }
    }
  });
  return ret;
}
krpc::NodeID Bucket::min() const {
  return prefix_;
}
bool Bucket::make_good_now(const krpc::NodeID &id) {
  bool found = false;
  dfs_w([&found, id](Bucket &bucket) {
    if (!found && bucket.is_leaf()) {
      for (auto &item : bucket.known_nodes_) {
        if (item.first == id) {
          item.second.make_good_now();
          found = true;
          return;
        }
      }
    }
  });
  return found;
}
bool Bucket::make_good_now(uint32_t ip, uint16_t port) {
  bool found = false;
  dfs_w([&found, ip, port](Bucket &bucket) {
    if (!found && bucket.is_leaf()) {
      for (auto &item : bucket.known_nodes_) {
        if (item.second.ip() == ip && item.second.port() == port) {
          item.second.make_good_now();
          found = true;
          return;
        }
      }
    }
  });
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
  for (auto item : known_nodes_) {
    cb(item.second);
  }
}
void Bucket::remove(const krpc::NodeID &id) {
  dfs_w([id](Bucket &bucket) {
    if (bucket.known_nodes_.find(id) != bucket.known_nodes_.end()) {
      bucket.known_nodes_.erase(id);
    }
  });
}
bool Bucket::require_response_now(const krpc::NodeID &target) {
  auto entry = search(target);
  if (entry) {
    entry->require_response_now();
    return true;
  } else {
    return false;
  }
}
Entry *Bucket::search(const krpc::NodeID &id) {
  Entry *ret = nullptr;
  bool found = false;
  dfs_w([&ret, &found, &id](Bucket &bucket) {
    if (!found && bucket.is_leaf()) {
      for (auto &item : bucket.known_nodes_) {
        if (item.first == id) {
          ret = &item.second;
          return;
        }
      }
    }
  });
  return ret;

}
void Bucket::gc() {
  if (is_leaf()) {
    std::list<krpc::NodeID> nodes_to_delete;
    std::vector<krpc::NodeID> questionable_nodes;
    size_t n_good = 0, n_non_bad = 0;
    for (auto &node : known_nodes_) {
      if (node.second.is_bad()) {
        LOG(debug) << "Bucket::gc() prefix " << prefix_length_ << " delete bad node " << node.second.to_string();
        nodes_to_delete.push_back(node.first);
      } else if (node.second.is_good()) {
        n_good++;
        n_non_bad++;
      } else {
        n_non_bad++;
        questionable_nodes.push_back(node.first);
      }
    }
    if (n_non_bad > BucketMaxItems) {
      LOG(debug) << "Bucket::gc() prefix " << prefix_length_ << " non_bad count " << n_non_bad << " delete extra nodes";
      for (size_t i = 0; i < n_non_bad - BucketMaxGoodItems && i < questionable_nodes.size(); i++) {
        LOG(debug) << "Bucket::gc() prefix " << prefix_length_ << " delete questionable node " << questionable_nodes[i].to_string();
        nodes_to_delete.push_back(questionable_nodes[i]);
      }
    }
    for (auto &id : nodes_to_delete) {
      known_nodes_.erase(id);
    }
  } else {
    left_->gc();
    right_->gc();
  }
}

std::list<Entry> RoutingTable::select_expand_route_targets() {
  std::list<Entry> entries;
  root_.dfs([&entries](const Bucket &bucket) {
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
void RoutingTable::stat(std::ostream &os) const {
  os << "Routing Table: self: " << self_id_.to_string() << std::endl;
  os << "  total entries: " << root_.total_known_node_count() << std::endl;
  os << "  total good entries: " << root_.total_good_node_count() << std::endl;
  root_.bfs([&os](const Bucket &bucket) {
    if (bucket.is_leaf()) {
      if (bucket.known_node_count() > 0) {
        os << "  p=" << bucket.prefix().to_string()
           << ", len(p)=" << bucket.prefix_length()
           << ", good " << bucket.good_node_count() << ", n " << bucket.known_node_count() << std::endl;
      }
    }
  });
}
bool RoutingTable::make_good_now(const krpc::NodeID &id) {
  return root_.make_good_now(id);
}
void RoutingTable::add_node(Entry entry) {
  root_.add_node(entry);
}
bool RoutingTable::make_good_now(uint32_t ip, uint16_t port) {
  return root_.make_good_now(ip, port);
}
void RoutingTable::iterate_nodes(const std::function<void(const Entry &)> &callback) {
  root_.dfs([&callback](const Bucket &bucket) {
    if (bucket.is_leaf()) {
      bucket.iterate_entries(callback);
    }
  });
}
void RoutingTable::remove_node(const krpc::NodeID &target) {
  root_.remove(target);
}

void Entry::make_good_now() {
  this->last_seen_ = std::chrono::high_resolution_clock::now();
  this->response_required = false;
}
bool Entry::is_good() const {
  // A good node is a node has responded to one of our queries within the last 15 minutes,
  // A node is also good if it has ever responded to one of our queries and has sent us a query within the last 15 minutes

  return !is_bad() &&
      (std::chrono::high_resolution_clock::now() - last_seen_) < std::chrono::minutes(MaxGoodNodeAliveMinutes);
}
void Entry::require_response_now() {
  response_required = true;
  this->last_require_response_ = std::chrono::high_resolution_clock::now();
}
bool Entry::is_bad() const {
  if (!response_required)
    return false;

  return ((std::chrono::high_resolution_clock::now() - last_require_response_) > krpc::KRPCTimeout);
}
}
