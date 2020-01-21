#include "routing_table.hpp"

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

  for (auto &item : good_nodes_) {
    if (left_->in_bucket(item.first)) {
      left_->good_nodes_.insert(item);
    } else {
      assert(right_->in_bucket(item.first));
      right_->good_nodes_.insert(item);
    }
  }
}
void Bucket::add_node(Entry entry) {
  assert(in_bucket(entry.id()));
  if (is_leaf()) {
    if (self_in_bucket()) {
      good_nodes_.insert({entry.id(), entry});
      if (good_nodes_.size() > BucketMaxItems) {
        split();
      }
    } else {
      if (good_nodes_.size() < BucketMaxItems) {
        good_nodes_.insert({entry.id(), entry});
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
    os << indent(i+1) << R"("entry_count": )" << good_nodes_.size() << std::endl;
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
    for (auto item : good_nodes_) {
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
    return this->good_nodes_.size() >= BucketMaxItems;
  } else {
    return left_->is_full() && right_->is_full();
  }
}

std::list<Entry> Bucket::find_some_node_for_filling_bucket(size_t k) const {

  std::map<krpc::NodeID, Entry> results2;
  int seed = 1;
  // TODO: work around a possibilly glibc issue
  if (good_nodes_.begin() == good_nodes_.end()) {
    return {};
  }
  std::sample(
      good_nodes_.begin(),
      good_nodes_.end(),
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
    return this->good_nodes_.size();
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
  os << "  total entries: " << root_.total_good_node_count() << std::endl;
  root_.bfs([&os](const Bucket &bucket) {
    if (bucket.is_leaf()) {
      if (bucket.good_node_count() > 0) {
        os << "  p=" << bucket.prefix().to_string()
           << ", len(p)=" << bucket.prefix_length()
           << ", n=" << bucket.good_node_count() << std::endl;
      }
    }
  });
}
}
