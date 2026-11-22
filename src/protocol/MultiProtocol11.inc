// MCPE 0.11.x (protocol 21-33) outgoing. Included from MultiProtocol.cpp.
namespace mpmpes::protocol::mp {

// Skin lookup for 0.11 AddPlayer (native AddPlayer does not carry skins).
static SkinLookup g_skin_lookup;
void setSkinLookup(SkinLookup f) { g_skin_lookup = std::move(f); }

static bool blockKnown11(std::uint8_t id) {
  static const auto set = [] {
    std::array<bool, 256> s{};
    for (int b : data::blocks11) s[static_cast<std::size_t>(b)] = true;
    return s;
  }();
  return set[id];
}

static std::string chunk11FromNative(std::string_view buf) {
  if (buf.size() < 14) return {};
  Rd r(buf, 1);
  auto x = r.i32();
  auto z = r.i32();
  (void)r.u8(); // order: both column/layered pass through unchanged
  auto len = r.i32();
  auto data = buf.substr(14, static_cast<std::size_t>(std::max(0, std::min<std::int32_t>(len, static_cast<std::int32_t>(buf.size() - 14)))));
  std::string body;
  if (data.size() >= 83200) {
    std::string ids(data.substr(0, 32768));
    for (auto& c : ids)
      if (!blockKnown11(static_cast<std::uint8_t>(c))) c = 0;
    body = ids;
    body.append(data.substr(32768, 16384 * 3)); // meta, sky, block light
    body.append(data.substr(81920, 256));        // height map in the 0.11 "biome" slot
    body.append(data.substr(81920 + 256, 1024)); // biome colors
    std::size_t extra = 81920 + 256 + 1024;
    if (data.size() >= extra + 4) {
      Rd e(data, extra);
      auto cnt = static_cast<std::int32_t>(e.le32());
      extra += 4 + static_cast<std::size_t>(std::max(0, cnt)) * 6;
      if (extra <= data.size()) body.append(data.substr(extra));
    }
  } else {
    body = std::string(data);
    body.append(std::string(256 + 1024, '\0'));
  }
  // chunk11Raw = true: payload sent uncompressed, no inner x/z prefix
  Wr w;
  w.u8(0xaf).be32(static_cast<std::uint32_t>(x)).be32(static_cast<std::uint32_t>(z))
      .be32(static_cast<std::uint32_t>(body.size())).raw(body);
  return w.s;
}

static std::vector<std::string> out11(std::string_view buf) {
  const auto pid = static_cast<std::uint8_t>(buf[0]);
  if (pid == FULL_CHUNK_DATA_PACKET) {
    auto c = chunk11FromNative(buf);
    if (c.empty()) return {};
    return {c};
  }
  auto mapped = toClientPid11(pid);
  if (!mapped) return {};
  Rd r(buf, 1);
  Wr w;
  switch (pid) {
    case BATCH_PACKET: {
      auto len = r.i32();
      w.u8(0xb1).be32(static_cast<std::uint32_t>(len)).raw(r.raw(static_cast<std::size_t>(std::max(0, len))));
      return {w.s};
    }
    case TEXT_PACKET: {
      auto type = r.u8();
      std::string source, message;
      std::vector<std::string> params;
      switch (type) {
        case 3:
        case 1: source = r.str(); [[fallthrough]];
        case 0:
        case 4:
        case 5: message = r.str(); break;
        case 2: {
          message = r.str();
          auto n = r.u8();
          for (int i = 0; i < n; ++i) params.push_back(r.str());
          break;
        }
      }
      if (type == 5) type = 0;
      w.u8(0x85).u8(type);
      if (type == 1) {
        w.str(source).str(message);
      } else if (type == 2) {
        w.str(message).u8(static_cast<std::uint32_t>(params.size()));
        for (auto& p : params) w.str(p);
      } else {
        w.str(message);
      }
      return {w.s};
    }
    case START_GAME_PACKET: {
      auto seed = r.i32();
      (void)r.u8();
      auto gen = r.i32();
      auto gm = r.i32();
      auto eid = r.i64();
      auto sx = r.i32(), sy = r.i32(), sz = r.i32();
      float x = r.f32(), y = r.f32(), z = r.f32();
      w.u8(0x87).be32(static_cast<std::uint32_t>(seed)).be32(static_cast<std::uint32_t>(gen))
          .be32(static_cast<std::uint32_t>(gm)).i64(eid).be32(static_cast<std::uint32_t>(sx))
          .be32(static_cast<std::uint32_t>(sy)).be32(static_cast<std::uint32_t>(sz)).f32(x).f32(y).f32(z);
      return {w.s};
    }
    case ADD_PLAYER_PACKET: {
      (void)r.raw(16);
      auto name = r.str();
      auto eid = r.i64();
      float x = r.f32(), y = r.f32(), z = r.f32();
      float sx = r.f32(), sy = r.f32(), sz = r.f32();
      float yaw = r.f32();
      (void)r.f32();
      float pitch = r.f32();
      auto held = readSlot14(r);
      std::size_t ms = r.o;
      (void)readMeta14(r);
      auto meta = buf.substr(ms, r.o - ms);
      std::string skin;
      bool slim = false;
      if (g_skin_lookup) g_skin_lookup(eid, skin, slim);
      w.u8(0x88).i64(eid).lstr(name).i64(eid).f32(x).f32(y).f32(z).f32(sx).f32(sy).f32(sz)
          .f32(yaw).f32(yaw).f32(pitch).be16(static_cast<std::uint16_t>(held.id))
          .be16(static_cast<std::uint16_t>(held.damage)).u8(slim ? 1 : 0).lstr(skin).raw(meta);
      return {w.s};
    }
    case REMOVE_PLAYER_PACKET: {
      auto eid = r.i64();
      w.u8(0x89).i64(eid).i64(eid);
      return {w.s};
    }
    case ADD_ITEM_ENTITY_PACKET: {
      auto eid = r.i64();
      auto s = readSlot14(r);
      w.u8(0x8c).i64(eid);
      writeSlot11(w, s);
      for (int i = 0; i < 6; ++i) w.f32(r.f32());
      return {w.s};
    }
    case SET_TIME_PACKET: {
      auto t = r.i32();
      auto started = r.u8();
      w.u8(0x86).be32(static_cast<std::uint32_t>(static_cast<int>((t / 24000.0) * 19200))).u8(started ? 0x80 : 0);
      return {w.s};
    }
    case CONTAINER_OPEN_PACKET: return {"\xa6" + std::string(buf.substr(1, 16))};
    case CONTAINER_SET_SLOT_PACKET: {
      auto wid = r.u8();
      auto slot = r.be16();
      (void)r.be16();
      auto s = readSlot14(r);
      w.u8(0xa8).u8(wid).be16(slot);
      writeSlot11(w, s);
      return {w.s};
    }
    case CONTAINER_SET_CONTENT_PACKET: {
      auto wid = r.u8();
      auto n = r.be16();
      w.u8(0xaa).u8(wid).be16(n);
      for (int i = 0; i < n; ++i) writeSlot11(w, readSlot14(r));
      if (wid == 0) {
        std::vector<std::int32_t> hb;
        if (r.left() >= 2) {
          auto hn = r.be16();
          for (int i = 0; i < hn; ++i) hb.push_back(r.i32());
        }
        w.be16(static_cast<std::uint32_t>(hb.size()));
        for (auto h : hb) w.be32(static_cast<std::uint32_t>(h));
      }
      return {w.s};
    }
    case MOB_EQUIPMENT_PACKET: {
      auto eid = r.i64();
      auto s = readSlot14(r);
      auto slot = r.u8(), sel = r.u8();
      w.u8(0x98).i64(eid).be16(static_cast<std::uint16_t>(s.id)).be16(static_cast<std::uint16_t>(s.damage)).u8(slot).u8(sel);
      return {w.s};
    }
    case MOB_ARMOR_EQUIPMENT_PACKET: {
      auto eid = r.i64();
      w.u8(0x99).i64(eid);
      for (int i = 0; i < 4; ++i) w.u8(static_cast<std::uint32_t>(readSlot14(r).id) & 0xff);
      return {w.s};
    }
    case SET_SPAWN_POSITION_PACKET: {
      auto x = r.i32(), y = r.i32(), z = r.i32();
      w.u8(0xa2).be32(static_cast<std::uint32_t>(x)).be32(static_cast<std::uint32_t>(z)).u8(static_cast<std::uint32_t>(y));
      return {w.s};
    }
    case ADVENTURE_SETTINGS_PACKET: w.u8(0xac).be32(r.be32()); return {w.s};
    case BLOCK_ENTITY_DATA_PACKET: {
      auto x = r.i32(), y = r.i32(), z = r.i32();
      w.u8(0xad).be32(static_cast<std::uint32_t>(x)).u8(static_cast<std::uint32_t>(y)).be32(static_cast<std::uint32_t>(z)).raw(r.rest());
      return {w.s};
    }
    case PLAYER_LIST_PACKET: return {};
    default: return {pidStr(*mapped) + std::string(buf.substr(1))};
  }
}

} // namespace mpmpes::protocol::mp
