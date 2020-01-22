#pragma once
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <exception>
#include <sstream>

namespace bencoding {

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
  virtual void encode(std::ostream &os, EncodeMode = EncodeMode::Bencoding) const = 0;
  virtual ~Node() = default;
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
  void encode(std::ostream &os, EncodeMode mode) const override;
 private:
  std::string s_;
};
class IntNode :public Node {
 public:
  explicit IntNode(int64_t i) :Node(Type::Int), i_(i) { }
  operator int64_t() { return i_; }
  void encode(std::ostream &os, EncodeMode mode) const override;
 private:
  int64_t i_;
};
class ListNode :public Node {
 public:
  explicit ListNode(std::vector<std::shared_ptr<Node>> nodes) :Node(Type::List), list_(std::move(nodes)) { }

  void encode(std::ostream &os, EncodeMode mode) const override;
  size_t size() const { return list_.size(); }
  std::shared_ptr<Node> operator[](size_t i) {
    return list_[i];
  }
 private:
  std::vector<std::shared_ptr<Node>> list_;
};
class DictNode :public Node {
 public:
  explicit DictNode(std::map<std::string, std::shared_ptr<Node>> dict) :Node(Type::Dict), dict_(std::move(dict)) { }


  const std::map<std::string, std::shared_ptr<Node>> &dict() const {
    return dict_;
  }
  void encode(std::ostream &os, EncodeMode mode) const override;
 private:
  std::map<std::string, std::shared_ptr<Node>> dict_;
};

class InvalidBencoding :public std::runtime_error {
 public:
  explicit InvalidBencoding(std::string s) :runtime_error(std::move(s)) { }
};
}

