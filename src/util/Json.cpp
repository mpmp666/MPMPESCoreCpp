#include "mpmpes/util/Json.hpp"

#include <cctype>
#include <charconv>

namespace mpmpes::util {

namespace {
struct P {
  std::string_view s;
  std::size_t o = 0;
  void ws() {
    while (o < s.size() && (s[o] == ' ' || s[o] == '\t' || s[o] == '\n' || s[o] == '\r')) ++o;
  }
  bool eof() const { return o >= s.size(); }
  char peek() { return o < s.size() ? s[o] : '\0'; }
  char get() { return o < s.size() ? s[o++] : '\0'; }
  bool eat(char c) {
    ws();
    if (peek() == c) { ++o; return true; }
    return false;
  }
  JsonPtr value(int depth = 0) {
    if (depth > 200) return nullptr;
    ws();
    char c = peek();
    if (c == '{') {
      ++o;
      JsonObject obj;
      ws();
      if (eat('}')) return std::make_shared<JsonValue>(JsonValue{std::move(obj)});
      while (true) {
        ws();
        if (peek() != '"') return nullptr;
        auto key = string();
        if (!key) return nullptr;
        ws();
        if (!eat(':')) return nullptr;
        auto v = value(depth + 1);
        if (!v) return nullptr;
        obj.emplace_back(std::move(*key), std::move(v));
        ws();
        if (eat('}')) break;
        if (!eat(',')) return nullptr;
      }
      return std::make_shared<JsonValue>(JsonValue{std::move(obj)});
    }
    if (c == '[') {
      ++o;
      JsonArray arr;
      ws();
      if (eat(']')) return std::make_shared<JsonValue>(JsonValue{std::move(arr)});
      while (true) {
        auto v = value(depth + 1);
        if (!v) return nullptr;
        arr.push_back(std::move(v));
        ws();
        if (eat(']')) break;
        if (!eat(',')) return nullptr;
      }
      return std::make_shared<JsonValue>(JsonValue{std::move(arr)});
    }
    if (c == '"') {
      auto str = string();
      if (!str) return nullptr;
      return std::make_shared<JsonValue>(JsonValue{std::move(*str)});
    }
    if (c == 't' || c == 'f') {
      if (s.substr(o, 4) == "true") { o += 4; return std::make_shared<JsonValue>(JsonValue{true}); }
      if (s.substr(o, 5) == "false") { o += 5; return std::make_shared<JsonValue>(JsonValue{false}); }
      return nullptr;
    }
    if (c == 'n') {
      if (s.substr(o, 4) == "null") { o += 4; return std::make_shared<JsonValue>(JsonValue{nullptr}); }
      return nullptr;
    }
    // number
    std::size_t start = o;
    if (peek() == '-' || peek() == '+') ++o;
    while (o < s.size() && (std::isdigit(static_cast<unsigned char>(s[o])) || s[o] == '.' || s[o] == 'e' || s[o] == 'E' || s[o] == '-' || s[o] == '+')) ++o;
    if (o == start) return nullptr;
    double d = 0;
    auto [ptr, ec] = std::from_chars(s.data() + start, s.data() + o, d);
    if (ec != std::errc()) return nullptr;
    return std::make_shared<JsonValue>(JsonValue{d});
  }
  std::optional<std::string> string() {
    if (get() != '"') return std::nullopt;
    std::string out;
    while (!eof() && peek() != '"') {
      char c = get();
      if (c == '\\' && !eof()) {
        char e = get();
        switch (e) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'u': {
            if (o + 4 > s.size()) return std::nullopt;
            unsigned cp = 0;
            std::from_chars(s.data() + o, s.data() + o + 4, cp, 16);
            o += 4;
            if (cp < 0x80) out.push_back((char)cp);
            else if (cp < 0x800) { out.push_back((char)(0xc0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3f))); }
            else { out.push_back((char)(0xe0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3f))); out.push_back((char)(0x80 | (cp & 0x3f))); }
            break;
          }
          default: out.push_back(e);
        }
      } else out.push_back(c);
    }
    if (eof()) return std::nullopt;
    get(); // closing quote
    return out;
  }
};
} // namespace

JsonPtr jsonParse(std::string_view text) {
  P p{text};
  auto v = p.value();
  if (!v) return nullptr;
  p.ws();
  return v;
}

} // namespace mpmpes::util
