#include "mpmpes/nbt/Tag.hpp"

#include <zlib.h>

#include <cstring>
#include <sstream>

namespace mpmpes::nbt {

TagPtr Tag::clone() const {
  auto t = std::make_shared<Tag>(*this);
  for (auto& e : t->list) e = e ? e->clone() : nullptr;
  for (auto& [k, v] : t->comp) v = v ? v->clone() : nullptr;
  return t;
}

namespace {

struct Writer {
  std::string out;
  Endian e;
  template <typename T>
  void putInt(T v) {
    using U = std::make_unsigned_t<T>;
    U u = static_cast<U>(v);
    char buf[sizeof(T)];
    for (std::size_t k = 0; k < sizeof(T); ++k) {
      std::size_t shift = (e == Endian::Big) ? (sizeof(T) - 1 - k) * 8 : k * 8;
      buf[k] = static_cast<char>((u >> shift) & 0xff);
    }
    out.append(buf, sizeof(T));
  }
  void putStr(std::string_view s) {
    putInt<std::uint16_t>(static_cast<std::uint16_t>(s.size()));
    out.append(s);
  }
  void payload(const Tag& t) {
    switch (t.type) {
      case TAG_End: break;
      case TAG_Byte: out.push_back(static_cast<char>(t.i)); break;
      case TAG_Short: putInt<std::int16_t>(static_cast<std::int16_t>(t.i)); break;
      case TAG_Int: putInt<std::int32_t>(static_cast<std::int32_t>(t.i)); break;
      case TAG_Long: putInt<std::int64_t>(t.i); break;
      case TAG_Float: {
        float f = static_cast<float>(t.d);
        std::uint32_t u;
        std::memcpy(&u, &f, 4);
        putInt<std::uint32_t>(u);
        break;
      }
      case TAG_Double: {
        std::uint64_t u;
        std::memcpy(&u, &t.d, 8);
        putInt<std::uint64_t>(u);
        break;
      }
      case TAG_ByteArray:
        putInt<std::int32_t>(static_cast<std::int32_t>(t.s.size()));
        out.append(t.s);
        break;
      case TAG_String: putStr(t.s); break;
      case TAG_List: {
        TagType lt = t.list_type;
        if (lt == TAG_End && !t.list.empty() && t.list[0]) lt = t.list[0]->type;
        out.push_back(static_cast<char>(lt));
        putInt<std::int32_t>(static_cast<std::int32_t>(t.list.size()));
        for (const auto& c : t.list) {
          if (c) payload(*c);
        }
        break;
      }
      case TAG_Compound:
        for (const auto& [k, v] : t.comp) {
          if (!v || v->type == TAG_End) continue;
          out.push_back(static_cast<char>(v->type));
          putStr(k);
          payload(*v);
        }
        out.push_back(static_cast<char>(TAG_End));
        break;
      case TAG_IntArray:
        putInt<std::int32_t>(static_cast<std::int32_t>(t.ia.size()));
        for (auto v : t.ia) putInt<std::int32_t>(v);
        break;
    }
  }
};

struct Reader {
  std::string_view in;
  std::size_t off = 0;
  Endian e;
  bool fail = false;
  int depth = 0;
  bool need(std::size_t n) {
    if (off + n > in.size()) {
      fail = true;
      return false;
    }
    return true;
  }
  template <typename T>
  T getInt() {
    using U = std::make_unsigned_t<T>;
    if (!need(sizeof(T))) return 0;
    U u = 0;
    for (std::size_t k = 0; k < sizeof(T); ++k) {
      U b = static_cast<std::uint8_t>(in[off + k]);
      std::size_t shift = (e == Endian::Big) ? (sizeof(T) - 1 - k) * 8 : k * 8;
      u |= static_cast<U>(b << shift);
    }
    off += sizeof(T);
    return static_cast<T>(u);
  }
  std::string getStr() {
    auto n = getInt<std::uint16_t>();
    if (!need(n)) return {};
    std::string s(in.substr(off, n));
    off += n;
    return s;
  }
  TagPtr payload(TagType tt) {
    if (++depth > 512) {
      fail = true;
      return nullptr;
    }
    auto t = std::make_shared<Tag>();
    t->type = tt;
    switch (tt) {
      case TAG_End: break;
      case TAG_Byte:
        if (need(1)) t->i = static_cast<std::int8_t>(in[off++]);
        break;
      case TAG_Short: t->i = getInt<std::int16_t>(); break;
      case TAG_Int: t->i = getInt<std::int32_t>(); break;
      case TAG_Long: t->i = getInt<std::int64_t>(); break;
      case TAG_Float: {
        std::uint32_t u = getInt<std::uint32_t>();
        float f;
        std::memcpy(&f, &u, 4);
        t->d = f;
        break;
      }
      case TAG_Double: {
        std::uint64_t u = getInt<std::uint64_t>();
        std::memcpy(&t->d, &u, 8);
        break;
      }
      case TAG_ByteArray: {
        auto n = getInt<std::int32_t>();
        if (n < 0 || !need(static_cast<std::size_t>(n))) {
          fail = true;
          break;
        }
        t->s.assign(in.substr(off, static_cast<std::size_t>(n)));
        off += static_cast<std::size_t>(n);
        break;
      }
      case TAG_String: t->s = getStr(); break;
      case TAG_List: {
        if (!need(1)) break;
        t->list_type = static_cast<TagType>(static_cast<std::uint8_t>(in[off++]));
        auto n = getInt<std::int32_t>();
        if (n < 0 || n > 16 * 1024 * 1024) {
          fail = true;
          break;
        }
        for (std::int32_t k = 0; k < n && !fail; ++k) t->list.push_back(payload(t->list_type));
        break;
      }
      case TAG_Compound:
        while (!fail) {
          if (!need(1)) break;
          auto ct = static_cast<TagType>(static_cast<std::uint8_t>(in[off++]));
          if (ct == TAG_End) break;
          if (ct > TAG_IntArray) {
            fail = true;
            break;
          }
          std::string name = getStr();
          auto v = payload(ct);
          t->comp.emplace_back(std::move(name), v);
        }
        break;
      case TAG_IntArray: {
        auto n = getInt<std::int32_t>();
        if (n < 0 || !need(static_cast<std::size_t>(n) * 4)) {
          fail = true;
          break;
        }
        t->ia.resize(static_cast<std::size_t>(n));
        for (auto& v : t->ia) v = getInt<std::int32_t>();
        break;
      }
      default: fail = true;
    }
    --depth;
    return t;
  }
};

} // namespace

std::string write(const Tag& root, std::string_view root_name, Endian e) {
  Writer w{{}, e};
  w.out.push_back(static_cast<char>(root.type));
  w.putStr(root_name);
  w.payload(root);
  return std::move(w.out);
}

TagPtr read(std::string_view data, Endian e, std::string* root_name_out, std::size_t* consumed) {
  Reader r{data, 0, e};
  if (data.empty()) return nullptr;
  auto tt = static_cast<TagType>(static_cast<std::uint8_t>(data[0]));
  r.off = 1;
  if (tt == TAG_End || tt > TAG_IntArray) return nullptr;
  std::string name = r.getStr();
  auto t = r.payload(tt);
  if (r.fail) return nullptr;
  if (root_name_out) *root_name_out = name;
  if (consumed) *consumed = r.off;
  return t;
}

static std::string deflateWith(std::string_view raw, int level, int window_bits) {
  z_stream zs{};
  if (deflateInit2(&zs, level, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
  std::string out;
  out.resize(deflateBound(&zs, static_cast<uLong>(raw.size())) + 32);
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.data()));
  zs.avail_in = static_cast<uInt>(raw.size());
  zs.next_out = reinterpret_cast<Bytef*>(out.data());
  zs.avail_out = static_cast<uInt>(out.size());
  int rc = deflate(&zs, Z_FINISH);
  out.resize(zs.total_out);
  deflateEnd(&zs);
  if (rc != Z_STREAM_END) return {};
  return out;
}

std::string zlibCompress(std::string_view raw, int level) { return deflateWith(raw, level, 15); }
std::string gzipCompress(std::string_view raw, int level) { return deflateWith(raw, level, 15 + 16); }

std::optional<std::string> inflateAny(std::string_view compressed) {
  z_stream zs{};
  if (inflateInit2(&zs, 15 + 32) != Z_OK) return std::nullopt;
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
  zs.avail_in = static_cast<uInt>(compressed.size());
  std::string out;
  char buf[65536];
  int rc;
  do {
    zs.next_out = reinterpret_cast<Bytef*>(buf);
    zs.avail_out = sizeof(buf);
    rc = inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) {
      inflateEnd(&zs);
      return std::nullopt;
    }
    out.append(buf, sizeof(buf) - zs.avail_out);
    if (out.size() > 64u * 1024 * 1024) {
      inflateEnd(&zs);
      return std::nullopt;
    }
  } while (rc != Z_STREAM_END && (zs.avail_in > 0 || zs.avail_out == 0));
  inflateEnd(&zs);
  return out;
}

TagPtr readCompressed(std::string_view data, Endian e) {
  auto raw = inflateAny(data);
  if (!raw) return nullptr;
  return read(*raw, e);
}

std::string writeGzip(const Tag& root, std::string_view root_name, Endian e) {
  return gzipCompress(write(root, root_name, e));
}
std::string writeZlib(const Tag& root, std::string_view root_name, Endian e, int level) {
  return zlibCompress(write(root, root_name, e), level);
}

std::string toString(const Tag& t, int indent) {
  std::ostringstream o;
  std::string pad(static_cast<std::size_t>(indent), ' ');
  switch (t.type) {
    case TAG_Byte:
    case TAG_Short:
    case TAG_Int:
    case TAG_Long: o << t.i; break;
    case TAG_Float:
    case TAG_Double: o << t.d; break;
    case TAG_String: o << '"' << t.s << '"'; break;
    case TAG_ByteArray: o << "<bytes " << t.s.size() << ">"; break;
    case TAG_IntArray: o << "<ints " << t.ia.size() << ">"; break;
    case TAG_List:
      o << "[\n";
      for (auto& c : t.list) o << pad << "  " << (c ? toString(*c, indent + 2) : "null") << "\n";
      o << pad << "]";
      break;
    case TAG_Compound:
      o << "{\n";
      for (auto& [k, v] : t.comp) o << pad << "  " << k << ": " << (v ? toString(*v, indent + 2) : "null") << "\n";
      o << pad << "}";
      break;
    default: o << "END";
  }
  return o.str();
}

} // namespace mpmpes::nbt
