#pragma once
#include <cassert>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace albert::bencoding {

enum class Type {
  String,
  Int,
  List,
  Dict,
};

enum class EncodeMode {
  Bencoding,
  JSON
};

class Node {
 public:
  Node(Type type) :type_(type) { }
  static std::shared_ptr<Node> decode(std::istream &);
  Type type() const { return type_; }
  virtual void encode(std::ostream &os, EncodeMode = EncodeMode::Bencoding, size_t depth = 0) const = 0;
  virtual ~Node() = default;
 protected:
  static std::ostream &make_indent(std::ostream &os, size_t depth);
 private:
  Type type_;
};

class StringNode :public Node {
 public:
  explicit StringNode(std::string s)
      :Node(Type::String), s_(std::move(s)) { }
  operator std::string() const {
    return s_;
  }
  void encode(std::ostream &os, EncodeMode mode, size_t depth = 0) const override;
 private:
  std::string s_;
};
class IntNode :public Node {
 public:
  explicit IntNode(int64_t i) :Node(Type::Int), i_(i) { }
  operator int64_t() const { return i_; }
  void encode(std::ostream &os, EncodeMode mode, size_t depth = 0) const override;
 private:
  int64_t i_;
};
class ListNode :public Node {
 public:
  explicit ListNode(std::vector<std::shared_ptr<Node>> nodes = {}) :Node(Type::List), list_(std::move(nodes)) { }

  void encode(std::ostream &os, EncodeMode mode, size_t depth = 0) const override;
  size_t size() const { return list_.size(); }
  std::shared_ptr<const Node> operator[](size_t i) const {
    return list_[i];
  }
  std::shared_ptr<Node> operator[](size_t i) {
    return list_[i];
  }
  void append(std::shared_ptr<Node> item) { return list_.push_back(item); }
 private:
  std::vector<std::shared_ptr<Node>> list_;
};
class DictNode :public Node {
 public:
  explicit DictNode(std::map<std::string, std::shared_ptr<Node>> dict = {}) :Node(Type::Dict), dict_(std::move(dict)) { }

  const std::map<std::string, std::shared_ptr<Node>> &dict() const {
    return dict_;
  }
  void encode(std::ostream &os, EncodeMode mode, size_t depth = 0) const override;
 private:
  std::map<std::string, std::shared_ptr<Node>> dict_;
};

class InvalidBencoding :public std::runtime_error {
 public:
  explicit InvalidBencoding(std::string s) :runtime_error(std::move(s)) { }
};

template <typename T, typename = void>
struct GetNodeType { };

template <typename T>
struct GetNodeType<T, typename std::enable_if<std::is_same<std::string, std::remove_reference_t<T>>::value>::type> {
  typedef StringNode type;
};

template <typename T>
struct GetNodeType<T, typename std::enable_if<std::is_integral<std::remove_reference_t<T>>::value>::type> {
  typedef IntNode type;
};

template <typename T>
struct GetNodeType<T, typename std::enable_if<std::is_same<std::vector<std::shared_ptr<Node>>, std::remove_reference_t<T>>::value>::type> {
  typedef ListNode type;
};

template <typename T>
struct GetNodeType<T, typename std::enable_if<std::is_same<ListNode, std::remove_reference_t<T>>::value>::type> {
  typedef ListNode type;
};

//
//template <typename T>
//struct GetNodeType<T, typename std::enable_if<std::is_integral<T>::value>::type> {
//  typedef StringNode type;
//};

template <typename T, typename U = typename GetNodeType<T>::type>
std::shared_ptr<U> make_node(T &&t) {
  return std::make_shared<U>(t);
}

namespace {
void _make_list(std::vector<std::shared_ptr<Node>> &result) {}
template <typename T, typename ... Args>
void _make_list(std::vector<std::shared_ptr<Node>> &result, T &&t, Args&&... args) {
  result.emplace_back(make_node(t));
  if (sizeof...(Args) > 0) {
    _make_list(result, std::forward<Args>(args)...);
  }
}
}

template <typename ... Args>
std::shared_ptr<ListNode> make_list(Args&&... args) {
  std::vector<std::shared_ptr<Node>> result;
  _make_list(result, std::forward<Args>(args)...);
  return std::make_shared<ListNode>(std::move(result));
}


template <typename T, typename U = typename GetNodeType<T>::type>
auto get(const DictNode &dict, const std::string &key) {
  auto node = std::dynamic_pointer_cast<const U>(dict.dict().at(key));
  if (!node) {
    throw std::invalid_argument("bencoding::get(DictNode, " + key + "), item is not " + typeid(T).name());
  }
  return *node;
}

template <typename T, typename U = typename GetNodeType<T>::type>
auto get(const ListNode &list, size_t index) {
  if (index >= list.size()) {
    throw std::invalid_argument("bencoding::get(ListNode) index out of range: i=" + std::to_string(index) + ", size=" + std::to_string(list.size()));
  }
  auto node = std::dynamic_pointer_cast<const U>(list[index]);
  if (!node) {
    throw std::invalid_argument("bencoding::get(ListNode, " + std::to_string(index) + "), item is not " + typeid(T).name());
  }
  return *node;
}

}

