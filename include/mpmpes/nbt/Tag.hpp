#pragma once
// Minimal NBT tree compatible with PocketMine nbt/NBT.php (BIG_ENDIAN for disk,
// LITTLE_ENDIAN for MCPE network tile data).

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mpmpes::nbt {

enum TagType : std::uint8_t {
  TAG_End = 0,
  TAG_Byte = 1,
  TAG_Short = 2,
  TAG_Int = 3,
  TAG_Long = 4,
  TAG_Float = 5,
  TAG_Double = 6,
  TAG_ByteArray = 7,
  TAG_String = 8,
  TAG_List = 9,
  TAG_Compound = 10,
  TAG_IntArray = 11,
};

class Tag;
using TagPtr = std::shared_ptr<Tag>;

class Tag {
public:
  TagType type = TAG_End;
  std::int64_t i = 0;  // byte/short/int/long
  double d = 0;        // float/double
  std::string s;       // string / byte array bytes
  std::vector<std::int32_t> ia;
  // list
  TagType list_type = TAG_End;
  std::vector<TagPtr> list;
  // compound (insertion ordered)
  std::vector<std::pair<std::string, TagPtr>> comp;

  static TagPtr Byte(std::int8_t v) { return make(TAG_Byte, v); }
  static TagPtr Short(std::int16_t v) { return make(TAG_Short, v); }
  static TagPtr Int(std::int32_t v) { return make(TAG_Int, v); }
  static TagPtr Long(std::int64_t v) { return make(TAG_Long, v); }
  static TagPtr Float(float v) {
    auto t = std::make_shared<Tag>();
    t->type = TAG_Float;
    t->d = v;
    return t;
  }
  static TagPtr Double(double v) {
    auto t = std::make_shared<Tag>();
    t->type = TAG_Double;
    t->d = v;
    return t;
  }
  static TagPtr String(std::string v) {
    auto t = std::make_shared<Tag>();
    t->type = TAG_String;
    t->s = std::move(v);
    return t;
  }
  static TagPtr ByteArray(std::string v) {
    auto t = std::make_shared<Tag>();
    t->type = TAG_ByteArray;
    t->s = std::move(v);
    return t;
  }
  static TagPtr IntArray(std::vector<std::int32_t> v) {
    auto t = std::make_shared<Tag>();
    t->type = TAG_IntArray;
    t->ia = std::move(v);
    return t;
  }
  static TagPtr List(TagType elem, std::vector<TagPtr> v = {}) {
    auto t = std::make_shared<Tag>();
    t->type = TAG_List;
    t->list_type = elem;
    t->list = std::move(v);
    return t;
  }
  static TagPtr Compound() {
    auto t = std::make_shared<Tag>();
    t->type = TAG_Compound;
    return t;
  }

  // Compound helpers
  TagPtr get(std::string_view key) const {
    for (const auto& [k, v] : comp)
      if (k == key) return v;
    return nullptr;
  }
  bool has(std::string_view key) const { return get(key) != nullptr; }
  Tag& set(std::string key, TagPtr v) {
    for (auto& [k, old] : comp) {
      if (k == key) {
        old = std::move(v);
        return *this;
      }
    }
    comp.emplace_back(std::move(key), std::move(v));
    return *this;
  }
  void remove(std::string_view key) {
    for (auto it = comp.begin(); it != comp.end(); ++it) {
      if (it->first == key) {
        comp.erase(it);
        return;
      }
    }
  }
  std::int64_t getInt(std::string_view key, std::int64_t def = 0) const {
    auto t = get(key);
    if (!t) return def;
    if (t->type == TAG_Float || t->type == TAG_Double) return static_cast<std::int64_t>(t->d);
    if (t->type >= TAG_Byte && t->type <= TAG_Long) return t->i;
    return def;
  }
  double getDouble(std::string_view key, double def = 0) const {
    auto t = get(key);
    if (!t) return def;
    if (t->type == TAG_Float || t->type == TAG_Double) return t->d;
    if (t->type >= TAG_Byte && t->type <= TAG_Long) return static_cast<double>(t->i);
    return def;
  }
  std::string getString(std::string_view key, std::string def = {}) const {
    auto t = get(key);
    if (!t) return def;
    if (t->type == TAG_String || t->type == TAG_ByteArray) return t->s;
    if (t->type >= TAG_Byte && t->type <= TAG_Long) return std::to_string(t->i);
    return def;
  }

  TagPtr clone() const;

private:
  static TagPtr make(TagType tt, std::int64_t v) {
    auto t = std::make_shared<Tag>();
    t->type = tt;
    t->i = v;
    return t;
  }
};

enum class Endian { Big, Little };

// Serialize a named root tag
std::string write(const Tag& root, std::string_view root_name = "", Endian e = Endian::Big);
// Parse a named root tag. Returns nullptr on failure. root_name_out optional.
TagPtr read(std::string_view data, Endian e = Endian::Big, std::string* root_name_out = nullptr,
            std::size_t* consumed = nullptr);

// Compression helpers (zlib deflate / gzip)
std::string zlibCompress(std::string_view raw, int level = 7);
std::string gzipCompress(std::string_view raw, int level = 7);
// Auto-detect zlib or gzip
std::optional<std::string> inflateAny(std::string_view compressed);

TagPtr readCompressed(std::string_view data, Endian e = Endian::Big);
std::string writeGzip(const Tag& root, std::string_view root_name = "", Endian e = Endian::Big);
std::string writeZlib(const Tag& root, std::string_view root_name = "", Endian e = Endian::Big,
                      int level = 7);

// Debug dump
std::string toString(const Tag& t, int indent = 0);

} // namespace mpmpes::nbt
