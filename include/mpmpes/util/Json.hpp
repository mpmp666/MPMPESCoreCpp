#pragma once
// Minimal JSON parser (enough for structure manifest/pools/loot tables).

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mpmpes::util {

struct JsonValue;
using JsonPtr = std::shared_ptr<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonPtr>>;
using JsonArray = std::vector<JsonPtr>;

struct JsonValue {
  std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> v;
  bool isNull() const { return std::holds_alternative<std::nullptr_t>(v); }
  bool isBool() const { return std::holds_alternative<bool>(v); }
  bool isNum() const { return std::holds_alternative<double>(v); }
  bool isStr() const { return std::holds_alternative<std::string>(v); }
  bool isArr() const { return std::holds_alternative<JsonArray>(v); }
  bool isObj() const { return std::holds_alternative<JsonObject>(v); }
  bool asBool(bool def = false) const { return isBool() ? std::get<bool>(v) : def; }
  double asNum(double def = 0) const {
    if (isNum()) return std::get<double>(v);
    if (isStr()) {
      try { return std::stod(std::get<std::string>(v)); } catch (...) {}
    }
    return def;
  }
  int asInt(int def = 0) const { return static_cast<int>(asNum(def)); }
  const std::string& asStr() const {
    static const std::string empty;
    return isStr() ? std::get<std::string>(v) : empty;
  }
  const JsonArray& arr() const {
    static const JsonArray empty;
    return isArr() ? std::get<JsonArray>(v) : empty;
  }
  const JsonObject& obj() const {
    static const JsonObject empty;
    return isObj() ? std::get<JsonObject>(v) : empty;
  }
  const JsonValue* get(std::string_view key) const {
    if (!isObj()) return nullptr;
    for (const auto& [k, val] : std::get<JsonObject>(v))
      if (k == key) return val.get();
    return nullptr;
  }
  const JsonValue* at(std::size_t i) const {
    if (!isArr()) return nullptr;
    const auto& a = std::get<JsonArray>(v);
    return i < a.size() ? a[i].get() : nullptr;
  }
};

// Parse JSON text; returns nullptr on error. Non-destructive.
JsonPtr jsonParse(std::string_view text);

} // namespace mpmpes::util
