#include "albert/bencode/bencoding.hpp"

#include <albert/log/log.hpp>
#include <iomanip>

namespace albert::bencoding {

static std::string read_string(std::istream &is) {
  // TODO: check overflow, bencoding integer can be any digits
  int64_t n = -1;
  is >> n;
  if (!is.good()) {
    throw InvalidBencoding("Invalid string length, eof or not a number");
  }
  if (is.get() != ':') {
    throw InvalidBencoding("Invalid string, after integer, no colon presents.");
  }

  // TODO: check EOF
  std::vector<char> s(n);
  is.read(s.data(), n);
  if (!is.good()) {
    throw InvalidBencoding("Invalid string value");
  }
  return std::string(s.data(), s.size());
}

std::shared_ptr<Node> Node::decode(std::istream &is) {
  int ch = is.peek();
  if (ch == EOF) {
    throw InvalidBencoding("EOF");
  }

  switch (ch) {
    case 'i':{
      is.seekg(1, std::ios_base::cur);
      int64_t i;
      is >> i;
      if (is.fail()) {
        throw InvalidBencoding("Invalid integer, overflow, eof, or not a number, etc.");
      }
      if (is.get() == 'e') {
        return std::make_shared<IntNode>(i);
      } else {
        throw InvalidBencoding("Invalid integer, no 'e' found after the number");
      }
    }
    case 'l': {
      is.seekg(1, std::ios_base::cur);
      std::vector<std::shared_ptr<Node>> l;
      while (true) {
        if (is.peek() == 'e') {
          is.seekg(1, std::ios_base::cur);
          break;
        }
        auto node = decode(is);
        assert(node != nullptr);
        l.push_back(node);
      }
      return std::make_shared<ListNode>(std::move(l));
    }
    case 'd': {
      is.seekg(1, std::ios_base::cur);

      std::map<std::string, std::shared_ptr<Node>> dict;
      while (true) {
        if (is.peek() == 'e') {
          is.seekg(1, std::ios_base::cur);
          break;
        }

        auto key = read_string(is);
        auto value = decode(is);
        assert(value);
        dict[key] = value;
      }
      return std::make_shared<DictNode>(std::move(dict));
    }
    default: {
      // string
      auto s = read_string(is);
      return std::make_shared<StringNode>(s);
    }

  }
}
std::ostream &Node::make_indent(std::ostream &os, size_t depth) {
  for (size_t i = 0; i < depth; i++) {
    os << "  ";
  }
  return os;
}

static std::string json_string(const std::string& s) {
  std::stringstream ss;
  ss << '"';
  for (char c : s) {
    if (c == '"' || c == '\\') {
      // this works for utf-8 string
      ss << "\\" << c;
    } else {
      if (std::isprint(c)) {
        ss.put(c);
      } else {
        std::stringstream ss2;
        ss2 << std::hex << std::setfill('0') << std::setw(4) << (uint32_t)(uint8_t)c;
        ss << "\\u" << ss2.str();
      }
    }
  }
  ss << '"';
  return ss.str();
}

void StringNode::encode(std::ostream &os, EncodeMode mode, size_t depth) const {
  if (mode == EncodeMode::Bencoding) {
    os << s_.size() << ":" << s_;
  } else if (mode == EncodeMode::JSON) {
    os << json_string(s_);
  }
}

void ListNode::encode(std::ostream &os, EncodeMode mode, size_t depth) const {
  if (mode == EncodeMode::Bencoding) {
    os << 'l';
    for (auto &node : list_) {
      node->encode(os, mode, depth+1);
    }
    os << 'e';
  } else if (mode == EncodeMode::JSON) {
    os << '[';
    os << std::endl;
    for (size_t i = 0; i < list_.size(); i++) {
      make_indent(os, depth+1);
      list_[i]->encode(os, mode, depth+1);
      if (i != list_.size() - 1) {
        os << ", ";
      }
      os << std::endl;
    }
    make_indent(os, depth) << ']';
  }
}

ListNode::operator std::vector<std::shared_ptr<DictNode>>() const {
  std::vector<std::shared_ptr<DictNode>> ret;
  for (auto item : list_) {
    auto dict_node = std::dynamic_pointer_cast<DictNode>(item);
    if (!dict_node) {
      throw std::runtime_error("not all items in the list are not DictNode");
    }
    ret.push_back(dict_node);
  }
  return ret;
}

void DictNode::encode(std::ostream &os, EncodeMode mode, size_t depth) const {
  if (mode == EncodeMode::Bencoding) {
    os << 'd';
    for (const auto& item : dict_) {
      os << item.first.size() << ":" << item.first;
      item.second->encode(os, mode, depth+1);
    }
    os << 'e';
  } else if (mode == EncodeMode::JSON) {
    os << '{' << std::endl;
    for (auto it = dict_.begin(); it != dict_.end(); it++) {
      if (it != dict_.begin())
        os << ", " << std::endl;
      make_indent(os, depth+1) << json_string(it->first) << ": ";
      it->second->encode(os, mode, depth+1);
      os << std::endl;
    }
    make_indent(os, depth) << '}';
  }
}
void IntNode::encode(std::ostream &os, EncodeMode mode, size_t depth) const {
  if (mode == EncodeMode::Bencoding) {
    os << 'i' << i_ << 'e';
  } else if (mode == EncodeMode::JSON) {
    os << i_;
  }
}
}