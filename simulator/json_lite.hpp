#pragma once

#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace msaflow::json_lite {

struct Value {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Value> array;
  std::map<std::string, Value> object;

  bool is_object() const { return type == Type::Object; }
  bool has(const std::string& key) const {
    return type == Type::Object && object.find(key) != object.end();
  }
  const Value& at(const std::string& key) const {
    const auto it = object.find(key);
    if (it == object.end()) {
      throw std::runtime_error("json_lite: missing key '" + key + "'");
    }
    return it->second;
  }
  double num() const { return number; }
  const std::string& str() const { return string; }
  bool flag() const { return boolean; }
  const std::vector<Value>& items() const { return array; }
};

namespace detail {

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  Value parse_value() {
    skip_ws();
    if (pos_ >= text_.size()) {
      throw std::runtime_error("json_lite: unexpected end of input");
    }
    switch (text_[pos_]) {
      case '{':
        return parse_object();
      case '[':
        return parse_array();
      case '"': {
        Value value;
        value.type = Value::Type::String;
        value.string = parse_string();
        return value;
      }
      case 't': {
        expect("true");
        Value value;
        value.type = Value::Type::Bool;
        value.boolean = true;
        return value;
      }
      case 'f': {
        expect("false");
        Value value;
        value.type = Value::Type::Bool;
        return value;
      }
      case 'n': {
        expect("null");
        return Value{};
      }
      default:
        return parse_number();
    }
  }

  void finish() {
    skip_ws();
    if (pos_ != text_.size()) {
      throw std::runtime_error("json_lite: trailing characters");
    }
  }

 private:
  void skip_ws() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
            text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  void expect(std::string_view token) {
    if (text_.substr(pos_, token.size()) != token) {
      throw std::runtime_error("json_lite: expected literal");
    }
    pos_ += token.size();
  }

  Value parse_object() {
    Value value;
    value.type = Value::Type::Object;
    ++pos_;
    skip_ws();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return value;
    }
    while (true) {
      skip_ws();
      const std::string key = parse_string();
      skip_ws();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        throw std::runtime_error("json_lite: expected ':'");
      }
      ++pos_;
      value.object.emplace(key, parse_value());
      skip_ws();
      if (pos_ < text_.size() && text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (pos_ < text_.size() && text_[pos_] == '}') {
        ++pos_;
        return value;
      }
      throw std::runtime_error("json_lite: expected ',' or '}'");
    }
  }

  Value parse_array() {
    Value value;
    value.type = Value::Type::Array;
    ++pos_;
    skip_ws();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return value;
    }
    while (true) {
      value.array.push_back(parse_value());
      skip_ws();
      if (pos_ < text_.size() && text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (pos_ < text_.size() && text_[pos_] == ']') {
        ++pos_;
        return value;
      }
      throw std::runtime_error("json_lite: expected ',' or ']'");
    }
  }

  std::string parse_string() {
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      throw std::runtime_error("json_lite: expected string");
    }
    ++pos_;
    std::string out;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') {
        return out;
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) {
        break;
      }
      const char esc = text_[pos_++];
      switch (esc) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          if (pos_ + 4 > text_.size()) {
            throw std::runtime_error("json_lite: bad unicode escape");
          }
          const std::string hex(text_.substr(pos_, 4));
          pos_ += 4;
          out.push_back(static_cast<char>(std::strtoul(hex.c_str(), nullptr, 16)));
          break;
        }
        default:
          throw std::runtime_error("json_lite: bad escape");
      }
    }
    throw std::runtime_error("json_lite: unterminated string");
  }

  Value parse_number() {
    const char* begin = text_.data() + pos_;
    char* end = nullptr;
    const double number = std::strtod(begin, &end);
    if (end == begin) {
      throw std::runtime_error("json_lite: bad number");
    }
    pos_ += static_cast<std::size_t>(end - begin);
    Value value;
    value.type = Value::Type::Number;
    value.number = number;
    return value;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

}  // namespace detail

inline Value parse(std::string_view text) {
  detail::Parser parser(text);
  Value value = parser.parse_value();
  parser.finish();
  return value;
}

}  // namespace msaflow::json_lite
