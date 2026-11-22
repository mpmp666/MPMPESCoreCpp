// Port of PocketMine network/MultiProtocol.php (+ the wire bits of
// RakLibInterface/Network/LoginPacket) operating on raw native buffers.
#include "mpmpes/protocol/MultiProtocol.hpp"

#include "mpmpes/nbt/Tag.hpp"
#include "mpmpes/protocol/Info.hpp"
#include "mpmpes/protocol/MultiProtocolData.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace mpmpes::protocol::mp {

namespace {

// ---------------------------------------------------------------- byte helpers
struct Rd {
  std::string_view b;
  std::size_t o = 0;
  explicit Rd(std::string_view s, std::size_t off = 0) : b(s), o(off) {}
  void need(std::size_t n) const {
    if (o + n > b.size()) throw std::out_of_range("short buffer");
  }
  std::size_t left() const { return o < b.size() ? b.size() - o : 0; }
  bool eof() const { return o >= b.size(); }
  std::uint8_t u8() { need(1); return static_cast<std::uint8_t>(b[o++]); }
  std::uint16_t be16() { need(2); auto v = static_cast<std::uint16_t>((std::uint8_t(b[o]) << 8) | std::uint8_t(b[o + 1])); o += 2; return v; }
  std::uint16_t le16() { need(2); auto v = static_cast<std::uint16_t>(std::uint8_t(b[o]) | (std::uint8_t(b[o + 1]) << 8)); o += 2; return v; }
  std::uint32_t be32() {
    need(4);
    std::uint32_t v = (std::uint32_t(std::uint8_t(b[o])) << 24) | (std::uint32_t(std::uint8_t(b[o + 1])) << 16) |
                      (std::uint32_t(std::uint8_t(b[o + 2])) << 8) | std::uint32_t(std::uint8_t(b[o + 3]));
    o += 4;
    return v;
  }
  std::uint32_t le32() {
    need(4);
    std::uint32_t v = std::uint32_t(std::uint8_t(b[o])) | (std::uint32_t(std::uint8_t(b[o + 1])) << 8) |
                      (std::uint32_t(std::uint8_t(b[o + 2])) << 16) | (std::uint32_t(std::uint8_t(b[o + 3])) << 24);
    o += 4;
    return v;
  }
  std::int32_t i32() { return static_cast<std::int32_t>(be32()); }
  std::int64_t i64() {
    std::uint64_t hi = be32(), lo = be32();
    return static_cast<std::int64_t>((hi << 32) | lo);
  }
  float f32() { auto u = be32(); float f; std::memcpy(&f, &u, 4); return f; }
  float lf32() { auto u = le32(); float f; std::memcpy(&f, &u, 4); return f; }
  std::string_view raw(std::size_t n) { need(n); auto s = b.substr(o, n); o += n; return s; }
  std::string_view rest() { auto s = o < b.size() ? b.substr(o) : std::string_view{}; o = b.size(); return s; }
  std::string str() { auto n = be16(); return std::string(raw(n)); }
  std::uint64_t uvar() {
    std::uint64_t v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
      auto c = u8();
      v |= static_cast<std::uint64_t>(c & 0x7f) << shift;
      if (!(c & 0x80)) return v;
    }
    throw std::runtime_error("varint too long");
  }
  std::int64_t var() { auto u = uvar(); return static_cast<std::int64_t>((u >> 1) ^ (~(u & 1) + 1)); }
  std::string str16() { auto n = uvar(); if (n > (1u << 24)) throw std::runtime_error("str"); return std::string(raw(static_cast<std::size_t>(n))); }
};

struct Wr {
  std::string s;
  Wr& u8(std::uint32_t v) { s.push_back(static_cast<char>(v & 0xff)); return *this; }
  Wr& be16(std::uint32_t v) { u8(v >> 8); return u8(v); }
  Wr& le16(std::uint32_t v) { u8(v); return u8(v >> 8); }
  Wr& be32(std::uint32_t v) { u8(v >> 24); u8(v >> 16); u8(v >> 8); return u8(v); }
  Wr& le32(std::uint32_t v) { u8(v); u8(v >> 8); u8(v >> 16); return u8(v >> 24); }
  Wr& i64(std::int64_t v) { be32(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v) >> 32)); return be32(static_cast<std::uint32_t>(v)); }
  Wr& f32(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return be32(u); }
  Wr& lf32(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return le32(u); }
  Wr& raw(std::string_view d) { s.append(d); return *this; }
  Wr& str(std::string_view d) { be16(static_cast<std::uint32_t>(d.size())); return raw(d); }
  Wr& lstr(std::string_view d) { le16(static_cast<std::uint32_t>(d.size())); return raw(d); }
  Wr& uvar(std::uint64_t v) {
    do {
      std::uint8_t c = v & 0x7f;
      v >>= 7;
      if (v) c |= 0x80;
      u8(c);
    } while (v);
    return *this;
  }
  Wr& var(std::int64_t v) { return uvar((static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63)); }
  Wr& str16(std::string_view d) { uvar(d.size()); return raw(d); }
  Wr& vec3(float x, float y, float z) { return lf32(x).lf32(y).lf32(z); }
  Wr& bc16(std::int32_t x, std::int32_t y, std::int32_t z) { var(x); u8(static_cast<std::uint32_t>(y)); return var(z); }
};

struct Slot {
  std::int16_t id = 0;
  std::uint8_t count = 0;
  std::int16_t damage = 0;
  std::string nbt;
};

// Native 0.14 slot: [BE short id][count][BE short damage][LShort nbtlen][nbt] (air: short 0 only)
Slot readSlot14(Rd& r) {
  Slot s;
  s.id = static_cast<std::int16_t>(r.be16());
  if (s.id <= 0) { s.id = 0; return s; }
  s.count = r.u8();
  s.damage = static_cast<std::int16_t>(r.be16());
  auto n = r.le16();
  if (n) s.nbt = std::string(r.raw(n));
  return s;
}
void writeSlot14(Wr& w, const Slot& s) {
  if (s.id <= 0) { w.be16(0); return; }
  w.be16(static_cast<std::uint16_t>(s.id)).u8(s.count).be16(static_cast<std::uint16_t>(s.damage)).lstr(s.nbt);
}
void writeSlot16(Wr& w, const Slot& s) {
  if (s.id <= 0) { w.var(0); return; }
  w.var(s.id).var((static_cast<std::int64_t>(s.damage) << 8) | s.count).lstr(s.nbt);
}
Slot readSlot16(Rd& r) {
  Slot s;
  auto id = r.var();
  if (id <= 0) return s;
  s.id = static_cast<std::int16_t>(id);
  auto aux = r.var();
  s.damage = static_cast<std::int16_t>(aux >> 8);
  s.count = static_cast<std::uint8_t>(aux & 0xff);
  if (r.left() >= 2) {
    auto n = r.le16();
    if (n) s.nbt = std::string(r.raw(std::min<std::size_t>(n, r.left())));
  }
  return s;
}
void writeSlot11(Wr& w, const Slot& s) {
  w.be16(static_cast<std::uint16_t>(s.id)).u8(s.id ? s.count : 0).be16(static_cast<std::uint16_t>(s.id ? s.damage : 0));
}
Slot readSlot11(Rd& r) {
  Slot s;
  if (r.left() < 5) { r.o = r.b.size(); return s; }
  s.id = static_cast<std::int16_t>(r.be16());
  s.count = r.u8();
  s.damage = static_cast<std::int16_t>(r.be16());
  if (s.id <= 0) s = Slot{};
  return s;
}

// Native metadata entry (PHP Binary::writeMetadata)
struct MetaVal {
  int type = 0;
  std::int64_t i = 0;
  float f = 0;
  std::string s;
  Slot slot;
  std::int32_t pos[3]{};
};
using Meta = std::map<int, MetaVal>; // ordered by key like PHP arrays built in key order

Meta readMeta14(Rd& r) {
  Meta m;
  while (!r.eof()) {
    auto b = r.u8();
    if (b == 0x7f) break;
    MetaVal v;
    v.type = b >> 5;
    int key = b & 0x1f;
    switch (v.type) {
      case 0: v.i = static_cast<std::int8_t>(r.u8()); break;
      case 1: v.i = static_cast<std::int16_t>(r.le16()); break;
      case 2: v.i = static_cast<std::int32_t>(r.le32()); break;
      case 3: v.f = r.lf32(); break;
      case 4: v.s = std::string(r.raw(r.le16())); break;
      case 5: v.slot.id = static_cast<std::int16_t>(r.le16()); v.slot.count = r.u8(); v.slot.damage = static_cast<std::int16_t>(r.le16()); break;
      case 6: for (auto& p : v.pos) p = static_cast<std::int32_t>(r.le32()); break;
      case 7: { std::uint64_t lo = r.le32(), hi = r.le32(); v.i = static_cast<std::int64_t>((hi << 32) | lo); break; }
      default: return m;
    }
    m[key] = std::move(v);
  }
  return m;
}

void writeMeta14(Wr& w, const Meta& m) {
  for (auto& [k, v] : m) {
    w.u8(static_cast<std::uint32_t>((v.type << 5) | (k & 0x1f)));
    switch (v.type) {
      case 0: w.u8(static_cast<std::uint32_t>(v.i)); break;
      case 1: w.le16(static_cast<std::uint32_t>(v.i)); break;
      case 2: w.le32(static_cast<std::uint32_t>(v.i)); break;
      case 3: w.lf32(v.f); break;
      case 4: w.lstr(v.s); break;
      case 5: w.le16(static_cast<std::uint16_t>(v.slot.id)).u8(v.slot.count).le16(static_cast<std::uint16_t>(v.slot.damage)); break;
      case 6: for (auto p : v.pos) w.le32(static_cast<std::uint32_t>(p)); break;
      case 7: w.le32(static_cast<std::uint32_t>(v.i)).le32(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v.i) >> 32)); break;
    }
  }
  w.u8(0x7f);
}

// 0.15 metadata: native + injected DATA_LEAD_HOLDER(23)=-1 long and key 24 byte 0
std::string meta15FromRaw(std::string_view raw_meta) {
  std::string m(raw_meta);
  if (!m.empty() && m.back() == '\x7f') {
    m.pop_back();
    m += "\xf7";
    m += std::string(8, '\xff');
    m += std::string("\x18\x00", 2);
    m += "\x7f";
  }
  return m;
}

void writeMeta16(Wr& out, const Meta& data) {
  std::int64_t flags = 0;
  if (auto it = data.find(0); it != data.end()) flags = it->second.i;
  auto truthy = [&](int k) { auto it = data.find(k); return it != data.end() && it->second.i != 0; };
  if (truthy(15)) flags |= 1 << 16;
  if (truthy(4)) flags |= 1 << 17;
  if (truthy(3)) flags |= 1 << 14;
  Wr body;
  int count = 0;
  auto entry = [&](int key, int type, const MetaVal& v) {
    body.uvar(static_cast<std::uint64_t>(key));
    switch (type) {
      case 0: body.uvar(0).u8(static_cast<std::uint32_t>(v.i)); break;
      case 1: body.uvar(1).le16(static_cast<std::uint32_t>(v.i)); break;
      case 2: body.uvar(2).var(v.i); break;
      case 3: body.uvar(3).lf32(v.f); break;
      case 4: body.uvar(4).str16(v.s); break;
      case 5: body.uvar(5); writeSlot16(body, v.slot); break;
      case 6: body.uvar(6).bc16(v.pos[0], v.pos[1], v.pos[2]); break;
      default: body.uvar(7).var(v.i); break;
    }
    ++count;
  };
  static const std::map<int, std::pair<int, int>> key_map = {
      {2, {4, 4}}, {7, {8, 2}}, {8, {9, 0}}};
  for (auto& [k, v] : data) {
    if (k == 0) {
      MetaVal fv;
      fv.i = flags;
      entry(0, 7, fv);
      continue;
    }
    if (k == 1 || k == 3 || k == 4 || k == 15) continue;
    auto it = key_map.find(k);
    if (it == key_map.end()) continue;
    entry(it->second.first, it->second.second, v);
  }
  MetaVal lead;
  lead.i = -1;
  entry(38, 7, lead);
  out.uvar(static_cast<std::uint64_t>(count)).raw(body.s);
}

// ---------------------------------------------------------------- pid tables
struct PidMap {
  std::array<std::int16_t, 256> fwd{};
  std::array<std::int16_t, 256> rev{};
  PidMap(std::initializer_list<std::pair<int, int>> l) {
    fwd.fill(-1);
    rev.fill(-1);
    for (auto [a, b] : l) {
      fwd[static_cast<std::size_t>(a)] = static_cast<std::int16_t>(b);
      rev[static_cast<std::size_t>(b)] = static_cast<std::int16_t>(a);
    }
  }
};

const PidMap& map15() {
  static const PidMap m{
      {0x8f, 0x01}, {0x90, 0x02}, {0x91, 0x05}, {0x92, 0x06}, {0x93, 0x07}, {0x94, 0x08},
      {0x95, 0x09}, {0x96, 0x0a}, {0x98, 0x0b}, {0x99, 0x0c}, {0x9a, 0x0d}, {0x9b, 0x0e},
      {0x9c, 0x0f}, {0x9d, 0x10}, {0x9e, 0x12}, {0x9f, 0x13}, {0xa0, 0x14}, {0xa1, 0x15},
      {0xa2, 0x16}, {0xa3, 0x17}, {0xa4, 0x18}, {0xa5, 0x19}, {0xa6, 0x1a}, {0xa7, 0x1b},
      {0xa8, 0x1c}, {0xa9, 0x1e}, {0xaa, 0x1f}, {0xab, 0x20}, {0xac, 0x21}, {0xad, 0x22},
      {0xae, 0x23}, {0xaf, 0x24}, {0xb0, 0x25}, {0xb1, 0x26}, {0xb2, 0x27}, {0xb3, 0x28},
      {0xb4, 0x29}, {0xb5, 0x2a}, {0xb6, 0x2b}, {0xb7, 0x2c}, {0xb8, 0x2d}, {0xb9, 0x2e},
      {0xba, 0x2f}, {0xbb, 0x30}, {0xbc, 0x31}, {0xbd, 0x32}, {0xbe, 0x33}, {0xbf, 0x34},
      {0xc0, 0x35}, {0xc1, 0x36}, {0xc2, 0x37}, {0xc3, 0x38}, {0xc6, 0x3b}, {0xc7, 0x3c},
      {0xc8, 0x3d}, {0xc9, 0x3e}, {0xca, 0x3f}};
  return m;
}

// 0.11 wire id -> native
const PidMap& map11() {
  static const PidMap m{
      {0x8f, 0x82}, {0x90, 0x83}, {0x91, 0x84}, {0x93, 0x85}, {0x94, 0x86}, {0x95, 0x87},
      {0x96, 0x88}, {0x97, 0x89}, {0x98, 0x8a}, {0x99, 0x8b}, {0x9a, 0x8c}, {0x9b, 0x8d},
      {0x9c, 0x8e}, {0x9d, 0x8f}, {0x9e, 0x90}, {0x9f, 0x91}, {0xa0, 0x92}, {0xa1, 0x93},
      {0xa2, 0x94}, {0xa3, 0x95}, {0xa4, 0x96}, {0xa5, 0x97}, {0xa7, 0x98}, {0xa8, 0x99},
      {0xa9, 0x9a}, {0xaa, 0x9b}, {0xab, 0x9c}, {0xac, 0x9d}, {0xad, 0x9e}, {0xae, 0x9f},
      {0xaf, 0xa0}, {0xb0, 0xa1}, {0xb1, 0xa2}, {0xb2, 0xa3}, {0xb3, 0xa4}, {0xb4, 0xa5},
      {0xb5, 0xa6}, {0xb6, 0xa7}, {0xb7, 0xa8}, {0xb8, 0xa9}, {0xb9, 0xaa}, {0xbc, 0xac},
      {0xbd, 0xad}, {0xbf, 0xaf}, {0xc0, 0xb0}, {0x92, 0xb1}};
  return m;
}

std::uint8_t angle8(float deg) { return static_cast<std::uint8_t>(static_cast<int>(deg / (360.0f / 256.0f)) & 0xff); }

std::string pidStr(std::uint8_t pid) { return std::string(1, static_cast<char>(pid)); }

} // namespace

// ============================================================== public helpers
int wireFamily(int p) {
  if (p <= 0) return F014;
  if (is011(p)) return F011;
  if (p >= PROTOCOL_0_16) return F016;
  if (p >= PROTOCOL_0_15) return F015;
  if (is013(p)) return F013;
  if (p == PROTOCOL_0_12) return F012;
  return F014;
}

bool isAccepted(int p) {
  return p == 34 || (p >= 37 && p <= 39) || p == 45 || p == 46 || p == 60 || p == 70 ||
         (p >= 81 && p <= 99) || is011(p);
}

std::string_view encapPrefix(int family) {
  if (family == F015 || family == F016) return "\xfe";
  if (family == F013 || family == F012 || family == F011) return "";
  return "\x8e";
}

std::optional<std::uint8_t> toClientPid15(std::uint8_t n) {
  auto v = map15().fwd[n];
  if (v < 0) return std::nullopt;
  return static_cast<std::uint8_t>(v);
}
std::optional<std::uint8_t> toServerPid15(std::uint8_t w) {
  auto v = map15().rev[w];
  if (v < 0) return std::nullopt;
  return static_cast<std::uint8_t>(v);
}
std::optional<std::uint8_t> toClientPid11(std::uint8_t n) {
  auto v = map11().fwd[n];
  if (v < 0) return std::nullopt;
  return static_cast<std::uint8_t>(v);
}
std::optional<std::uint8_t> toServerPid11(std::uint8_t w) {
  auto v = map11().rev[w];
  if (v < 0) return std::nullopt;
  return static_cast<std::uint8_t>(v);
}

template <std::size_t N>
static bool inArr(const int (&a)[N], int v) {
  return std::find(std::begin(a), std::end(a), v) != std::end(a);
}
bool isUnknownToOldProtocol(int id) { return inArr(data::oldProtocolUnknownItems, id); }
bool isOldProtocolCreativeItem(int id) { return inArr(data::oldProtocolCreativeItems, id); }
bool is012ProtocolCreativeItem(int id) { return inArr(data::oldProtocolCreativeItems12, id); }
bool is011CreativeItem(int id, int meta) {
  for (auto& e : data::creativeItems11)
    if (e[0] == id && e[1] == meta) return true;
  return false;
}

std::string encodeResourcePacksInfo() { return pidStr(RESOURCE_PACKS_INFO_PACKET); }
std::string encodeAvailableCommands(std::string_view commands, std::string_view unknown) {
  Wr w;
  w.u8(AVAILABLE_COMMANDS_PACKET).str(commands).str(unknown);
  return w.s;
}

// Forward decls of per-family translators
static std::vector<std::string> out15(std::string_view buf);
static std::vector<std::string> out13(std::string_view buf);
static std::vector<std::string> out12(std::string_view buf);
static std::vector<std::string> out16(std::string_view buf);
static std::vector<std::string> out11(std::string_view buf);

std::vector<std::string> translateOutgoing(int family, std::string_view native) {
  if (native.empty()) return {};
  const auto pid = static_cast<std::uint8_t>(native[0]);
  try {
    switch (family) {
      case F015: if (pid >= 0xec) return {}; return out15(native);
      case F013: if (pid >= 0xec) return {}; return out13(native);
      case F012: if (pid >= 0xec) return {}; return out12(native);
      case F016: return out16(native);
      case F011: if (pid >= 0xec) return {}; return out11(native);
      default:
        if (pid >= 0xec) return {};
        return {std::string(native)};
    }
  } catch (const std::exception&) {
    return {}; // malformed native buffer for this family: drop rather than crash the client
  }
}

// ---------------------------------------------------------------- 0.15
static std::vector<std::string> out15(std::string_view buf) {
  const auto pid = static_cast<std::uint8_t>(buf[0]);
  Rd r(buf, 1);
  switch (pid) {
    case UPDATE_BLOCK_PACKET: return {"\x13" + std::string(buf.substr(5))};
    case SET_ENTITY_MOTION_PACKET: return {"\x23" + std::string(buf.substr(5))};
    case MOVE_ENTITY_PACKET: {
      std::vector<std::string> out;
      auto n = r.i32();
      for (int i = 0; i < n && r.left() >= 32; ++i) {
        auto eid = r.raw(8);
        auto xyz = r.raw(12);
        float yaw = r.f32(), head = r.f32(), pitch = r.f32();
        Wr w;
        w.u8(0x0f).raw(eid).raw(xyz).u8(angle8(pitch)).u8(angle8(yaw)).u8(angle8(head));
        out.push_back(std::move(w.s));
      }
      return out;
    }
    case ADD_ENTITY_PACKET: {
      // [eid][type][xyz][speed xyz][yaw][pitch][meta][links short + (from,to,type)*]
      Wr w;
      w.u8(0x0b).i64(r.i64()).be32(static_cast<std::uint32_t>(r.i32()));
      for (int i = 0; i < 6; ++i) w.f32(r.f32());
      float yaw = r.f32(), pitch = r.f32();
      w.f32(yaw * 0.71111f).f32(pitch * 0.71111f).be32(0);
      std::size_t ms = r.o;
      (void)readMeta14(r);
      w.raw(meta15FromRaw(buf.substr(ms, r.o - ms)));
      w.raw(r.rest());
      return {w.s};
    }
    case SET_ENTITY_DATA_PACKET: return {"\x22" + std::string(buf.substr(1, 8)) + meta15FromRaw(buf.substr(9))};
    case REMOVE_PLAYER_PACKET: return {"\x0c" + std::string(buf.substr(1, 8))};
    case CHANGE_DIMENSION_PACKET: {
      Wr w;
      w.u8(0x36).u8(static_cast<std::uint8_t>(buf.size() > 1 ? buf[1] : 0));
      // PHP pack("f") is machine-endian; on little-endian hosts that is LE.
      w.lf32(0).lf32(0).lf32(0).u8(0);
      return {w.s};
    }
    default: {
      auto m = toClientPid15(pid);
      if (!m) return {};
      return {pidStr(*m) + std::string(buf.substr(1))};
    }
  }
}

// ---------------------------------------------------------------- 0.13
static std::vector<std::string> out13(std::string_view buf) {
  const auto pid = static_cast<std::uint8_t>(buf[0]);
  switch (pid) {
    case START_GAME_PACKET:
      if (buf.size() >= 47) return {std::string(buf.substr(0, 46)) + std::string(1, '\0')};
      return {std::string(buf)};
    case CONTAINER_OPEN_PACKET: return {std::string(buf.substr(0, std::min<std::size_t>(17, buf.size())))};
    case ADVENTURE_SETTINGS_PACKET: return {std::string(buf.substr(0, std::min<std::size_t>(5, buf.size())))};
    default: return {std::string(buf)};
  }
}

// ---------------------------------------------------------------- 0.12
static std::string playerList12(std::string_view buf) {
  Rd r(buf, 1);
  auto type = r.u8();
  auto count = r.i32();
  Wr w;
  w.u8(PLAYER_LIST_PACKET).u8(type).be32(static_cast<std::uint32_t>(count));
  for (int i = 0; i < count && r.o < buf.size(); ++i) {
    if (type == 0) {
      auto uuid = r.raw(16);
      auto eid = r.raw(8);
      // PHP MultiProtocol::playerList12 reads the name length with readLShort
      // even though the native writer used putString (BE short). The oversized
      // LE length swallows skinName+skin into "name" — replicate byte-for-byte.
      auto nl = r.le16();
      auto name = std::string(r.raw(std::min<std::size_t>(nl, r.left())));
      // off is now past the (clamped) name; PHP then reads skinName/skin from the
      // (usually exhausted) stream: empty strings, slim = false.
      bool slim = false;
      std::string skin_name, skin;
      if (r.left() >= 2) {
        auto sl = r.le16();
        skin_name = std::string(r.raw(std::min<std::size_t>(sl, r.left())));
        std::string lower = skin_name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        slim = lower.find("slim") != std::string::npos;
        if (r.left() >= 2) {
          auto kl = r.le16();
          skin = std::string(r.raw(std::min<std::size_t>(kl, r.left())));
        }
      }
      w.raw(uuid).raw(eid).le16(static_cast<std::uint32_t>(name.size())).raw(name).u8(slim ? 1 : 0)
          .le16(static_cast<std::uint32_t>(skin.size())).raw(skin);
    } else {
      w.raw(r.raw(16));
    }
  }
  return w.s;
}

// Parse native CraftingData into structured entries (shared by 0.12/0.16)
struct Recipe {
  int type = 0; // 0 shapeless 1 shaped 2 furnace 3 furnace data 4 multi
  int w = 0, h = 0;
  std::vector<Slot> in;   // shaped: native order z<w, x<h  => index z*h + x
  Slot result;
  std::string uuid;
  int furnace_input = 0;
  std::string raw;
};
static std::vector<Recipe> parseCrafting(std::string_view buf, bool& clean) {
  Rd r(buf, 1);
  auto n = r.i32();
  std::vector<Recipe> v;
  for (int i = 0; i < n; ++i) {
    Recipe rc;
    rc.type = r.i32();
    auto len = r.i32();
    auto body = r.raw(static_cast<std::size_t>(len));
    rc.raw = std::string(body);
    Rd e(body);
    switch (rc.type) {
      case 0: {
        auto k = e.i32();
        for (int j = 0; j < k; ++j) rc.in.push_back(readSlot14(e));
        (void)e.i32();
        rc.result = readSlot14(e);
        rc.uuid = std::string(e.raw(16));
        break;
      }
      case 1: {
        rc.w = e.i32();
        rc.h = e.i32();
        for (int j = 0; j < rc.w * rc.h; ++j) rc.in.push_back(readSlot14(e));
        (void)e.i32();
        rc.result = readSlot14(e);
        rc.uuid = std::string(e.raw(16));
        break;
      }
      case 2:
      case 3:
        rc.furnace_input = e.i32();
        rc.result = readSlot14(e);
        break;
      default: break;
    }
    v.push_back(std::move(rc));
  }
  clean = !r.eof() && r.u8() != 0;
  return v;
}
// Ingredient at (x, z) in the native z<width,x<height layout
static const Slot& shapedAt(const Recipe& rc, int x, int z) {
  static const Slot air;
  // Native writer iterates z in [0,w), x in [0,h) and stores getIngredient(x, z).
  int idx = z * rc.h + x;
  if (idx < 0 || idx >= static_cast<int>(rc.in.size())) return air;
  return rc.in[static_cast<std::size_t>(idx)];
}

static std::string crafting12(std::string_view buf) {
  bool clean = true;
  auto list = parseCrafting(buf, clean);
  Wr body;
  int count = 0;
  for (auto& rc : list) {
    Wr e;
    if (rc.type == 0) {
      e.be32(static_cast<std::uint32_t>(rc.in.size()));
      for (auto& s : rc.in) writeSlot14(e, s);
      e.be32(1);
      writeSlot14(e, rc.result);
      e.raw(rc.uuid);
    } else if (rc.type == 1) {
      e.be32(static_cast<std::uint32_t>(rc.w)).be32(static_cast<std::uint32_t>(rc.h));
      for (int z = 0; z < rc.h; ++z)
        for (int x = 0; x < rc.w; ++x) writeSlot14(e, shapedAt(rc, x, z));
      e.be32(1);
      writeSlot14(e, rc.result);
      e.raw(rc.uuid);
    } else if (rc.type == 2 || rc.type == 3) {
      e.raw(rc.raw);
    } else {
      continue;
    }
    body.be32(static_cast<std::uint32_t>(rc.type)).be32(static_cast<std::uint32_t>(e.s.size())).raw(e.s);
    ++count;
  }
  Wr w;
  w.u8(CRAFTING_DATA_PACKET).be32(static_cast<std::uint32_t>(count)).raw(body.s).u8(clean ? 1 : 0);
  return w.s;
}

static std::vector<std::string> out12(std::string_view buf) {
  const auto pid = static_cast<std::uint8_t>(buf[0]);
  switch (pid) {
    case START_GAME_PACKET:
    case CONTAINER_OPEN_PACKET:
    case ADVENTURE_SETTINGS_PACKET: return out13(buf);
    case CONTAINER_SET_SLOT_PACKET:
      if (buf.size() < 6) return {std::string(buf)};
      return {std::string(buf.substr(0, 4)) + std::string(buf.substr(6))};
    case CHANGE_DIMENSION_PACKET: {
      Wr w;
      w.u8(0xc1).be32(buf.size() > 1 ? static_cast<std::uint8_t>(buf[1]) : 0);
      return {w.s};
    }
    case PLAYER_LIST_PACKET: return {playerList12(buf)};
    case CRAFTING_DATA_PACKET: return {crafting12(buf)};
    default: return {std::string(buf)};
  }
}

} // namespace mpmpes::protocol::mp

#include "MultiProtocol16.inc"
#include "MultiProtocol11.inc"
#include "MultiProtocolIn.inc"
