// Incoming translation (family wire -> native 0.14 buffers) + batches + JWT login.
// Included from MultiProtocol.cpp.
namespace mpmpes::protocol::mp {

// Native LOGIN rebuilt from any family login, in full 0.14 layout so the core
// decoder (decodeLogin) handles every version the same way.
static std::string nativeLogin(const std::string& username, int protocol, std::int64_t client_id,
                               const std::array<std::uint8_t, 16>& uuid, const std::string& server_addr,
                               const std::string& skin_name, const std::string& skin) {
  Wr w;
  w.u8(LOGIN_PACKET).str(username).be32(static_cast<std::uint32_t>(protocol)).be32(static_cast<std::uint32_t>(protocol))
      .i64(client_id).raw(std::string_view(reinterpret_cast<const char*>(uuid.data()), 16)).str(server_addr).str("")
      .str(skin_name).str(skin);
  return w.s;
}

static std::array<std::uint8_t, 16> randomUuid(std::string_view seed) {
  std::array<std::uint8_t, 16> u{};
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char c : seed) h = (h ^ c) * 1099511628211ull;
  std::uint64_t h2 = h * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
  for (int i = 0; i < 8; ++i) u[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(h >> (i * 8));
  for (int i = 0; i < 8; ++i) u[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(h2 >> (i * 8));
  u[6] = static_cast<std::uint8_t>((u[6] & 0x0f) | 0x40);
  u[8] = static_cast<std::uint8_t>((u[8] & 0x3f) | 0x80);
  return u;
}

// ---- minimal JSON / base64 helpers for JWT payloads
static std::string b64decode(std::string_view in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  std::string out;
  int buf = 0, bits = 0;
  for (char c : in) {
    int v = val(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buf >> bits) & 0xff));
    }
  }
  return out;
}

// Extract a JSON string value for "key" (first occurrence); handles \" \\ \/ \uXXXX (BMP).
static std::optional<std::string> jsonString(std::string_view j, std::string_view key) {
  std::string pat = "\"" + std::string(key) + "\"";
  auto p = j.find(pat);
  if (p == std::string_view::npos) return std::nullopt;
  p += pat.size();
  while (p < j.size() && (j[p] == ' ' || j[p] == ':')) ++p;
  if (p >= j.size()) return std::nullopt;
  if (j[p] != '"') {
    // number / literal
    auto e = p;
    while (e < j.size() && j[e] != ',' && j[e] != '}' && j[e] != ']') ++e;
    return std::string(j.substr(p, e - p));
  }
  ++p;
  std::string out;
  while (p < j.size() && j[p] != '"') {
    char c = j[p++];
    if (c == '\\' && p < j.size()) {
      char e = j[p++];
      switch (e) {
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'r': out.push_back('\r'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'u': {
          if (p + 4 > j.size()) return out;
          unsigned cp = static_cast<unsigned>(std::stoul(std::string(j.substr(p, 4)), nullptr, 16));
          p += 4;
          if (cp < 0x80) out.push_back(static_cast<char>(cp));
          else if (cp < 0x800) { out.push_back(static_cast<char>(0xc0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
          else { out.push_back(static_cast<char>(0xe0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
          break;
        }
        default: out.push_back(e);
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

static std::string jwtPayload(std::string_view token) {
  auto a = token.find('.');
  if (a == std::string_view::npos) return {};
  auto b = token.find('.', a + 1);
  auto mid = token.substr(a + 1, b == std::string_view::npos ? std::string_view::npos : b - a - 1);
  return b64decode(mid);
}

static bool parseUuidString(const std::string& s, std::array<std::uint8_t, 16>& out) {
  std::string hex;
  for (char c : s)
    if (std::isxdigit(static_cast<unsigned char>(c))) hex.push_back(c);
  if (hex.size() != 32) return false;
  for (int i = 0; i < 16; ++i) out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::stoi(hex.substr(static_cast<std::size_t>(i) * 2, 2), nullptr, 16));
  return true;
}

JwtLogin decodeJwtLogin(std::string_view b) {
  JwtLogin L;
  try {
    Rd r(b);
    L.protocol = r.i32();
    std::optional<std::string> raw;
    if (L.protocol >= 90) {
      (void)r.u8();
      auto n = r.uvar();
      raw = nbt::inflateAny(r.raw(std::min<std::size_t>(static_cast<std::size_t>(n), r.left())));
    } else {
      auto n = r.i32();
      raw = nbt::inflateAny(r.raw(static_cast<std::size_t>(n)));
    }
    if (!raw) return L;
    Rd d(*raw);
    auto chain_len = d.le32();
    std::string chain(d.raw(chain_len));
    // chain JSON: {"chain":["jwt","jwt",...]}
    std::size_t p = chain.find('[');
    while (p != std::string::npos) {
      auto q = chain.find('"', p + 1);
      if (q == std::string::npos) break;
      auto e = chain.find('"', q + 1);
      if (e == std::string::npos) break;
      auto payload = jwtPayload(std::string_view(chain).substr(q + 1, e - q - 1));
      auto ed = payload.find("\"extraData\"");
      if (ed != std::string::npos) {
        std::string_view sub(payload);
        sub = sub.substr(ed);
        if (auto n = jsonString(sub, "displayName")) L.username = *n;
        if (auto id = jsonString(sub, "identity")) parseUuidString(*id, L.uuid);
      }
      p = chain.find(',', e);
      if (p != std::string::npos && chain.find(']', e) < p) break;
    }
    auto skin_len = d.le32();
    auto skin_payload = jwtPayload(d.raw(skin_len));
    if (auto c = jsonString(skin_payload, "ClientRandomId")) {
      try { L.client_id = std::stoll(*c); } catch (...) {}
    }
    if (auto s = jsonString(skin_payload, "ServerAddress")) L.server_address = *s;
    if (auto s = jsonString(skin_payload, "SkinData")) L.skin = b64decode(*s);
    if (auto s = jsonString(skin_payload, "SkinId")) L.skin_id = *s;
    if (L.skin_id.empty()) L.skin_id = "Standard_Custom";
    bool zero = std::all_of(L.uuid.begin(), L.uuid.end(), [](auto v) { return v == 0; });
    if (zero) L.uuid = randomUuid(L.username + std::to_string(L.client_id));
    L.ok = !L.username.empty();
  } catch (...) {
    L.ok = false;
  }
  return L;
}

// Rewrite a native-layout LOGIN of any pre-JWT protocol (0.11/0.12/0.13/0.14) to full 0.14.
static std::string loginToNative(std::string_view buf /* [0x8f][...] */) {
  Rd peek(buf, 1);
  auto p0 = peek.be32();
  if (p0 > 0 && p0 < 65536) {
    auto L = decodeJwtLogin(buf.substr(1));
    if (!L.ok) return {};
    return nativeLogin(L.username, L.protocol, L.client_id, L.uuid, L.server_address, L.skin_id, L.skin);
  }
  Rd r(buf, 1);
  auto username = r.str();
  auto p1 = r.i32();
  (void)r.i32();
  if (is011(p1)) {
    auto cid = r.i32();
    bool slim = r.u8() > 0;
    auto skin = r.str();
    return nativeLogin(username, p1, cid, randomUuid(username + std::to_string(cid)), "",
                       slim ? "CustomSlim" : "Standard_Custom", skin);
  }
  if (p1 < 38) {
    if (p1 == 34) {
      auto cid = r.i64();
      std::array<std::uint8_t, 16> u{};
      auto us = r.raw(16);
      std::memcpy(u.data(), us.data(), 16);
      auto addr = r.str();
      (void)r.str();
      bool slim = r.u8() > 0;
      auto skin = r.str();
      return nativeLogin(username, p1, cid, u, addr, slim ? "CustomSlim" : "Standard_Steve", skin);
    }
    return nativeLogin(username, p1, 0, randomUuid(username), "", "Standard_Custom", "");
  }
  return std::string(buf);
}

static std::vector<std::string> unpackBatch(int family, std::string_view payload);

// ---- 0.16 inbound
static std::vector<std::string> in16(std::string_view buf) {
  const auto pid = static_cast<std::uint8_t>(buf[0]);
  Rd r(buf, 1);
  Wr w;
  switch (pid) {
    case 0x01: return {loginToNative("\x8f" + std::string(buf.substr(1)))};
    case 0x06: {
      auto n = r.uvar();
      return unpackBatch(F016, r.raw(std::min<std::size_t>(static_cast<std::size_t>(n), r.left())));
    }
    case 0x09: return {};
    case 0x0a: {
      auto type = r.u8();
      std::string src, msg;
      std::vector<std::string> params;
      switch (type) {
        case 3:
        case 1: src = r.str16(); [[fallthrough]];
        case 0:
        case 4:
        case 5: msg = r.str16(); break;
        case 2: {
          msg = r.str16();
          auto n = r.uvar();
          for (std::uint64_t i = 0; i < n && i < 64; ++i) params.push_back(r.str16());
          break;
        }
      }
      w.u8(TEXT_PACKET).u8(type);
      if (type == 1 || type == 3) w.str(src);
      w.str(msg);
      if (type == 2) {
        w.u8(static_cast<std::uint32_t>(params.size()));
        for (auto& p : params) w.str(p);
      }
      return {w.s};
    }
    case 0x14: {
      auto eid = r.var();
      float x = r.lf32(), y = r.lf32(), z = r.lf32();
      float pitch = r.lf32(), yaw = r.lf32(), body = r.lf32();
      auto mode = r.u8();
      bool og = !r.eof() && r.u8() > 0;
      w.u8(MOVE_PLAYER_PACKET).i64(eid).f32(x).f32(y).f32(z).f32(yaw).f32(body).f32(pitch).u8(mode).u8(og ? 1 : 0);
      return {w.s};
    }
    case 0x16: {
      auto x = r.var();
      auto y = r.u8();
      auto z = r.var();
      w.u8(REMOVE_BLOCK_PACKET).i64(0).be32(static_cast<std::uint32_t>(x)).be32(static_cast<std::uint32_t>(z)).u8(y);
      return {w.s};
    }
    case 0x1d: {
      auto eid = r.var();
      auto ev = r.u8();
      w.u8(ENTITY_EVENT_PACKET).i64(eid).u8(ev);
      return {w.s};
    }
    case 0x20: {
      auto eid = r.var();
      auto s = readSlot16(r);
      auto slot = r.u8(), sel = r.u8();
      w.u8(MOB_EQUIPMENT_PACKET).i64(eid);
      writeSlot14(w, s);
      w.u8(slot).u8(sel);
      return {w.s};
    }
    case 0x22: {
      auto a = r.u8();
      auto t = r.var();
      w.u8(INTERACT_PACKET).u8(a).i64(t);
      return {w.s};
    }
    case 0x23: {
      auto x = r.var();
      auto y = r.u8();
      auto z = r.var();
      auto face = r.var();
      float fx = r.lf32(), fy = r.lf32(), fz = r.lf32();
      float px = r.lf32(), py = r.lf32(), pz = r.lf32();
      auto slot = r.var();
      auto s = readSlot16(r);
      w.u8(USE_ITEM_PACKET).be32(static_cast<std::uint32_t>(x)).be32(y).be32(static_cast<std::uint32_t>(z))
          .u8(static_cast<std::uint32_t>(face)).f32(fx).f32(fy).f32(fz).f32(px).f32(py).f32(pz)
          .be32(static_cast<std::uint32_t>(slot));
      writeSlot14(w, s);
      return {w.s};
    }
    case 0x24: {
      auto eid = r.var();
      auto a = r.var();
      auto x = r.var();
      auto y = r.u8();
      auto z = r.var();
      auto face = r.var();
      w.u8(PLAYER_ACTION_PACKET).i64(eid).be32(static_cast<std::uint32_t>(a)).be32(static_cast<std::uint32_t>(x))
          .be32(y).be32(static_cast<std::uint32_t>(z)).be32(static_cast<std::uint32_t>(face));
      return {w.s};
    }
    case 0x2b: {
      auto a = r.var();
      auto eid = r.var();
      w.u8(ANIMATE_PACKET).u8(static_cast<std::uint32_t>(a)).i64(eid);
      return {w.s};
    }
    case 0x2d: {
      auto t = r.u8();
      auto s = readSlot16(r);
      w.u8(DROP_ITEM_PACKET).u8(t);
      writeSlot14(w, s);
      return {w.s};
    }
    case 0x30: w.u8(CONTAINER_CLOSE_PACKET).u8(r.u8()); return {w.s};
    case 0x31: {
      auto wid = r.u8();
      auto slot = r.var();
      auto hb = r.var();
      auto s = readSlot16(r);
      w.u8(CONTAINER_SET_SLOT_PACKET).u8(wid).be16(static_cast<std::uint32_t>(slot)).be16(static_cast<std::uint32_t>(hb));
      writeSlot14(w, s);
      return {w.s};
    }
    case 0x35: {
      auto wid = r.u8();
      auto type = r.var();
      auto uuid = r.raw(16);
      std::vector<Slot> in, out;
      auto n = r.uvar();
      for (std::uint64_t i = 0; i < n && i < 128 && !r.eof(); ++i) in.push_back(readSlot16(r));
      n = r.uvar();
      for (std::uint64_t i = 0; i < n && i < 128 && !r.eof(); ++i) out.push_back(readSlot16(r));
      w.u8(CRAFTING_EVENT_PACKET).u8(wid).be32(static_cast<std::uint32_t>(type)).raw(uuid).be32(static_cast<std::uint32_t>(in.size()));
      for (auto& s : in) writeSlot14(w, s);
      w.be32(static_cast<std::uint32_t>(out.size()));
      for (auto& s : out) writeSlot14(w, s);
      return {w.s};
    }
    case 0x37: {
      auto x = r.var();
      auto y = r.u8();
      auto z = r.var();
      w.u8(BLOCK_ENTITY_DATA_PACKET).be32(static_cast<std::uint32_t>(x)).be32(y).be32(static_cast<std::uint32_t>(z)).raw(r.rest());
      return {w.s};
    }
    case 0x38: {
      float mx = r.lf32(), my = r.lf32();
      bool j = !r.eof() && r.u8() > 0;
      bool s = !r.eof() && r.u8() > 0;
      w.u8(PLAYER_INPUT_PACKET).f32(mx).f32(my).u8((j ? 0x80 : 0) | (s ? 0x40 : 0));
      return {w.s};
    }
    case 0x43: w.u8(REQUEST_CHUNK_RADIUS_PACKET).be32(static_cast<std::uint32_t>(r.var())); return {w.s};
    case 0x45: {
      auto x = r.var();
      auto y = r.u8();
      auto z = r.var();
      auto s = readSlot16(r);
      w.u8(ITEM_FRAME_DROP_ITEM_PACKET).be32(static_cast<std::uint32_t>(x)).be32(y).be32(static_cast<std::uint32_t>(z));
      writeSlot14(w, s);
      return {w.s};
    }
    case 0x4c: {
      // CommandStep -> native chat "/command args..." (core runs commands from TEXT)
      auto cmd = r.str16();
      (void)r.str16();
      (void)r.uvar();
      (void)r.uvar();
      (void)r.u8();
      (void)r.uvar();
      auto args_json = r.eof() ? std::string() : r.str16();
      std::string text = cmd;
      if (text.find(' ') == std::string::npos && !args_json.empty() && args_json != "null") {
        // flatten: prefer "value"/"text" entries, else every scalar string/number
        std::vector<std::string> vals;
        std::size_t p = 0;
        bool any_kv = false;
        for (auto key : {"\"value\"", "\"text\"", "\"args\""}) {
          p = 0;
          while ((p = args_json.find(key, p)) != std::string::npos) {
            auto v = jsonString(std::string_view(args_json).substr(p), std::string_view(key).substr(1, std::strlen(key) - 2));
            if (v && !v->empty() && (*v)[0] != '{' && (*v)[0] != '[') {
              vals.push_back(*v);
              any_kv = true;
            }
            p += std::strlen(key);
          }
          if (any_kv) break;
        }
        if (!any_kv) {
          // plain array of strings
          p = 0;
          while ((p = args_json.find('"', p)) != std::string::npos) {
            auto e = args_json.find('"', p + 1);
            if (e == std::string::npos) break;
            vals.push_back(args_json.substr(p + 1, e - p - 1));
            p = e + 1;
          }
        }
        for (auto& v : vals) text += " " + v;
      }
      w.u8(TEXT_PACKET).u8(TEXT_CHAT).str("").str("/" + text);
      return {w.s};
    }
    default: return {};
  }
}

// ---- 0.11 inbound
static std::vector<std::string> in11(std::string_view buf, std::size_t* consumed) {
  const auto pid = static_cast<std::uint8_t>(buf[0]);
  Rd r(buf, 1);
  Wr w;
  std::vector<std::string> res;
  switch (pid) {
    case 0x82: {
      auto n = loginToNative("\x8f" + std::string(buf.substr(1)));
      if (consumed) {
        // consume only this login's own bytes (PHP decodeIncoming11/LoginPacket offset)
        try {
          Rd r2(buf, 1);
          r2.str();
          auto p1 = r2.i32();
          (void)r2.i32();
          if (is011(p1)) {
            (void)r2.i32();
            (void)r2.u8();
            (void)r2.str();
          } else if (p1 < 38) {
            if (p1 == 34) {
              (void)r2.i64();
              (void)r2.raw(16);
              (void)r2.str();
              (void)r2.str();
              (void)r2.u8();
              (void)r2.str();
            }
          } else {
            (void)r2.i64();
            (void)r2.raw(16);
            (void)r2.str();
            (void)r2.str();
            (void)r2.str();
            (void)r2.str();
          }
          *consumed = r2.o;
        } catch (...) {
          *consumed = buf.size();
        }
      }
      if (n.empty()) return {};
      return {n};
    }
    case 0xb1: {
      auto size = r.i32();
      if (size < 0 || static_cast<std::size_t>(size) > r.left()) return {};
      auto payload = r.raw(static_cast<std::size_t>(size));
      if (consumed) *consumed = r.o;
      return unpackBatch(F011, payload);
    }
    case 0x85: {
      auto type = r.eof() ? 0 : r.u8();
      std::string src, msg;
      std::vector<std::string> params;
      if (type == 1) src = r.str();
      if (type == 2) {
        msg = r.str();
        auto n = r.eof() ? 0 : r.u8();
        for (int i = 0; i < n; ++i) params.push_back(r.str());
      } else {
        msg = r.str();
      }
      w.u8(TEXT_PACKET).u8(type);
      if (type == 1 || type == 3) w.str(src);
      w.str(msg);
      if (type == 2) {
        w.u8(static_cast<std::uint32_t>(params.size()));
        for (auto& p : params) w.str(p);
      }
      break;
    }
    case 0x8f: {
      auto eid = r.i64();
      float x = r.f32(), y = r.f32(), z = r.f32(), yaw = r.f32(), body = r.f32(), pitch = r.f32();
      auto mode = r.eof() ? 0 : r.u8();
      w.u8(MOVE_PLAYER_PACKET).i64(eid).f32(x).f32(y).f32(z).f32(yaw).f32(body).f32(pitch).u8(mode).u8(1);
      break;
    }
    case 0x90: {
      auto eid = r.i64();
      auto x = r.i32(), z = r.i32();
      auto y = r.eof() ? 0 : r.u8();
      w.u8(REMOVE_BLOCK_PACKET).i64(eid).be32(static_cast<std::uint32_t>(x)).be32(static_cast<std::uint32_t>(z)).u8(y);
      break;
    }
    case 0x96: {
      auto eid = r.i64();
      auto ev = r.eof() ? 0 : r.u8();
      w.u8(ENTITY_EVENT_PACKET).i64(eid).u8(ev);
      break;
    }
    case 0x98: {
      auto eid = r.i64();
      auto id = static_cast<std::int16_t>(r.be16());
      auto dmg = static_cast<std::int16_t>(r.be16());
      auto slot = r.eof() ? 0 : r.u8();
      auto sel = r.eof() ? 0 : r.u8();
      Slot s;
      s.id = id > 0 ? id : 0;
      s.count = id > 0 ? 1 : 0;
      s.damage = id > 0 ? dmg : 0;
      w.u8(MOB_EQUIPMENT_PACKET).i64(eid);
      writeSlot14(w, s);
      w.u8(slot).u8(sel);
      break;
    }
    case 0x99: {
      auto eid = r.i64();
      w.u8(MOB_ARMOR_EQUIPMENT_PACKET).i64(eid);
      for (int i = 0; i < 4; ++i) {
        Slot s;
        s.id = r.eof() ? 0 : r.u8();
        s.count = s.id ? 1 : 0;
        writeSlot14(w, s);
      }
      break;
    }
    case 0x9a: {
      auto a = r.eof() ? 0 : r.u8();
      auto t = r.i64();
      w.u8(INTERACT_PACKET).u8(a).i64(t);
      break;
    }
    case 0x9b: {
      auto x = r.i32(), y = r.i32(), z = r.i32();
      auto face = r.eof() ? 0 : r.u8();
      auto id = static_cast<std::int16_t>(r.be16());
      auto dmg = static_cast<std::int16_t>(r.be16());
      (void)r.i64();
      float fx = r.f32(), fy = r.f32(), fz = r.f32(), px = r.f32(), py = r.f32(), pz = r.f32();
      Slot s;
      s.id = id > 0 ? id : 0;
      s.count = id > 0 ? 1 : 0;
      s.damage = id > 0 ? dmg : 0;
      w.u8(USE_ITEM_PACKET).be32(static_cast<std::uint32_t>(x)).be32(static_cast<std::uint32_t>(y)).be32(static_cast<std::uint32_t>(z))
          .u8(face).f32(fx).f32(fy).f32(fz).f32(px).f32(py).f32(pz).be32(0xffffffffu);
      writeSlot14(w, s);
      break;
    }
    case 0x9c: {
      auto eid = r.i64();
      w.u8(PLAYER_ACTION_PACKET).i64(eid);
      for (int i = 0; i < 5; ++i) w.be32(static_cast<std::uint32_t>(r.i32()));
      break;
    }
    case 0x9e: {
      auto eid = r.i64();
      w.u8(SET_ENTITY_DATA_PACKET).i64(eid).raw(r.rest());
      break;
    }
    case 0xa3: {
      auto a = r.eof() ? 0 : r.u8();
      auto eid = r.i64();
      w.u8(ANIMATE_PACKET).u8(a).i64(eid);
      break;
    }
    case 0xa4: {
      float x = r.f32(), y = r.f32(), z = r.f32();
      w.u8(RESPAWN_PACKET).f32(x).f32(y).f32(z);
      break;
    }
    case 0xa5: {
      auto t = r.eof() ? 0 : r.u8();
      auto s = readSlot11(r);
      w.u8(DROP_ITEM_PACKET).u8(t);
      writeSlot14(w, s);
      break;
    }
    case 0xa7: w.u8(CONTAINER_CLOSE_PACKET).u8(r.eof() ? 0 : r.u8()); break;
    case 0xa8: {
      auto wid = r.eof() ? 0 : r.u8();
      auto slot = r.be16();
      auto s = readSlot11(r);
      w.u8(CONTAINER_SET_SLOT_PACKET).u8(wid).be16(slot).be16(0);
      writeSlot14(w, s);
      break;
    }
    case 0xa9: {
      auto wid = r.eof() ? 0 : r.u8();
      auto prop = r.be16(), val = r.be16();
      w.u8(CONTAINER_SET_DATA_PACKET).u8(wid).be16(prop).be16(val);
      break;
    }
    case 0xaa: {
      auto wid = r.eof() ? 0 : r.u8();
      auto n = r.be16();
      w.u8(CONTAINER_SET_CONTENT_PACKET).u8(wid).be16(n);
      for (int i = 0; i < n; ++i) writeSlot14(w, readSlot11(r));
      std::vector<std::uint32_t> hb;
      if (wid == 0 && r.left() >= 2) {
        auto hn = r.be16();
        for (int i = 0; i < hn; ++i) hb.push_back(r.be32());
      }
      w.be16(static_cast<std::uint32_t>(hb.size()));
      for (auto h : hb) w.be32(h);
      break;
    }
    default:
      if (consumed) *consumed = 0;
      return {};
  }
  if (consumed) *consumed = r.o;
  res.push_back(std::move(w.s));
  return res;
}

// ---- generic inbound per family
static std::vector<std::string> inOne(int family, std::string_view buf) {
  if (buf.empty()) return {};
  auto pid = static_cast<std::uint8_t>(buf[0]);
  switch (family) {
    case F016:
      if (pid == 0xfe && buf.size() > 1) return in16(buf.substr(1));
      return in16(buf);
    case F011: return in11(buf, nullptr);
    case F015: {
      std::size_t start = 1;
      if (pid == 0xfe && buf.size() > 1) {
        pid = static_cast<std::uint8_t>(buf[1]);
        start = 2;
      }
      auto m = toServerPid15(pid);
      if (!m) return {};
      std::string n = pidStr(*m) + std::string(buf.substr(start));
      if (*m == BATCH_PACKET) {
        Rd r(n, 1);
        auto len = r.i32();
        return unpackBatch(F015, r.raw(static_cast<std::size_t>(std::max(0, len))));
      }
      if (*m == LOGIN_PACKET) return {loginToNative(n)};
      return {n};
    }
    default: {
      // 0.12/0.13 bare, 0.14 with optional 0x8e marker
      if (pid == MCPE_RAKNET_CUSTOM_PACKET_ID && buf.size() > 1) buf = buf.substr(1), pid = static_cast<std::uint8_t>(buf[0]);
      if (pid == BATCH_PACKET) {
        Rd r(buf, 1);
        auto len = r.i32();
        return unpackBatch(family, r.raw(static_cast<std::size_t>(std::max(0, std::min<std::int32_t>(len, static_cast<std::int32_t>(r.left()))))));
      }
      if (pid == LOGIN_PACKET) return {loginToNative(buf)};
      if (family == F012 && pid == CONTAINER_SET_SLOT_PACKET) {
        // 0.12 has no hotbarSlot short: [pid][wid][slot short][item]
        if (buf.size() < 4) return {};
        return {std::string(buf.substr(0, 4)) + std::string("\0\0", 2) + std::string(buf.substr(4))};
      }
      return {std::string(buf)};
    }
  }
}

static std::vector<std::string> unpackBatch(int family, std::string_view payload) {
  std::vector<std::string> out;
  auto raw = nbt::inflateAny(payload);
  if (!raw) return out;
  std::string_view s(*raw);
  std::size_t off = 0;
  int guard = 0;
  if (family == F011) {
    while (off < s.size() && ++guard < 4096) {
      std::size_t used = 0;
      auto v = in11(s.substr(off), &used);
      if (used == 0 || used > s.size() - off) break;
      for (auto& x : v) out.push_back(std::move(x));
      off += used;
    }
    return out;
  }
  while (off < s.size() && ++guard < 65536) {
    std::size_t len;
    if (family == F016) {
      Rd r(s, off);
      len = static_cast<std::size_t>(r.uvar());
      off = r.o;
    } else {
      if (s.size() - off < 5) break;
      Rd r(s, off);
      auto l = r.i32();
      if (l < 1) break;
      len = static_cast<std::size_t>(l);
      off += 4;
    }
    if (len < 1 || len > s.size() - off) break;
    auto item = s.substr(off, len);
    off += len;
    if (family == F014) {
      // 0.14 inner items are [0x8e][pid]... (PHP buf[1]); tolerate bare too
      if (static_cast<std::uint8_t>(item[0]) == MCPE_RAKNET_CUSTOM_PACKET_ID && item.size() > 1) item = item.substr(1);
    }
    auto pid = static_cast<std::uint8_t>(item[0]);
    if (family != F016 && family != F015 && pid == BATCH_PACKET) continue; // no nested batches
    for (auto& x : inOne(family, item)) out.push_back(std::move(x));
  }
  return out;
}

std::vector<std::string> translateIncoming(int family, std::string_view buf) {
#ifndef MP_DEBUG_NO_CATCH
  try {
    return inOne(family, buf);
  } catch (const std::exception&) {
    return {};
  }
#else
  return inOne(family, buf);
#endif
}

std::string buildBatch(int family, const std::vector<std::string>& inner, int level) {
  std::string body;
  for (auto& b : inner) {
    if (family == F011) {
      body += b;
    } else if (family == F016) {
      Wr w;
      w.uvar(b.size());
      body += w.s + b;
    } else {
      Wr w;
      w.be32(static_cast<std::uint32_t>(b.size()));
      body += w.s + b;
    }
  }
  auto z = nbt::zlibCompress(body, level);
  if (z.empty()) z = nbt::zlibCompress(body, 1);
  Wr w;
  switch (family) {
    case F016: w.u8(0x06).uvar(z.size()).raw(z); break;
    case F015: w.u8(0x06).be32(static_cast<std::uint32_t>(z.size())).raw(z); break;
    case F011: w.u8(0xb1).be32(static_cast<std::uint32_t>(z.size())).raw(z); break;
    default: w.u8(BATCH_PACKET).be32(static_cast<std::uint32_t>(z.size())).raw(z); break;
  }
  return w.s;
}

} // namespace mpmpes::protocol::mp
