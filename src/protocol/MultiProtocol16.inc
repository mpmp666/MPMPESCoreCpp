// MCPE 0.16.x (protocol 90/91) outgoing re-encode. Included from MultiProtocol.cpp.
// Each case parses the native 0.14 layout (PocketMine encode()) and writes the
// 0.16 layout from MultiProtocol::translateOutgoing16().
namespace mpmpes::protocol::mp {

static std::string chunk16FromNative(std::string_view buf) {
  Rd r(buf, 1);
  auto x = r.i32();
  auto z = r.i32();
  auto order = r.u8();
  auto len = r.i32();
  auto data = r.raw(static_cast<std::size_t>(std::max(0, std::min<std::int32_t>(len, static_cast<std::int32_t>(r.left())))));
  Wr w;
  w.u8(0x39).var(x).var(z).u8(order).uvar(data.size()).raw(data);
  return w.s;
}

static std::string attributeName16(const std::string& name) {
  static const std::map<std::string, std::string> m = {
      {"generic.absorption", "minecraft:absorption"},
      {"player.saturation", "minecraft:player.saturation"},
      {"player.exhaustion", "minecraft:player.exhaustion"},
      {"generic.knockbackResistance", "minecraft:knockback_resistance"},
      {"generic.health", "minecraft:health"},
      {"generic.movementSpeed", "minecraft:movement"},
      {"generic.followRange", "minecraft:follow_range"},
      {"player.hunger", "minecraft:player.hunger"},
      {"generic.attackDamage", "minecraft:attack_damage"},
      {"player.level", "minecraft:player.level"},
      {"player.experience", "minecraft:player.experience"},
  };
  auto it = m.find(name);
  if (it != m.end()) return it->second;
  return name.find(':') == std::string::npos ? "minecraft:" + name : name;
}

static std::string crafting16(std::string_view buf) {
  bool clean = true;
  auto list = parseCrafting(buf, clean);
  Wr body;
  int count = 0;
  for (auto& rc : list) {
    if (rc.type == 0) {
      body.var(0).uvar(rc.in.size());
      for (auto& s : rc.in) writeSlot16(body, s);
      body.uvar(1);
      writeSlot16(body, rc.result);
      body.raw(rc.uuid);
    } else if (rc.type == 1) {
      body.var(1).var(rc.w).var(rc.h);
      for (int z = 0; z < rc.h; ++z)
        for (int x = 0; x < rc.w; ++x) writeSlot16(body, shapedAt(rc, x, z));
      body.uvar(1);
      writeSlot16(body, rc.result);
      body.raw(rc.uuid);
    } else if (rc.type == 3) {
      body.var(3).var(rc.furnace_input >> 16).var(rc.furnace_input & 0xffff);
      writeSlot16(body, rc.result);
    } else if (rc.type == 2) {
      body.var(2).var(rc.furnace_input);
      writeSlot16(body, rc.result);
    } else {
      continue;
    }
    ++count;
  }
  Wr w;
  w.u8(0x34).uvar(static_cast<std::uint64_t>(count)).raw(body.s).u8(clean ? 1 : 0);
  return w.s;
}

static std::string batch16FromNative(std::string_view buf) {
  Rd r(buf, 1);
  auto len = r.i32();
  auto data = r.raw(static_cast<std::size_t>(std::max(0, len)));
  Wr w;
  w.u8(0x06).uvar(data.size()).raw(data);
  return w.s;
}

// Difficulty used in 0.16 StartGame (PHP reads Server::getDifficulty()).
static int g_difficulty16 = 1;
static std::string g_world_name16;
void setStartGame16Info(int difficulty, std::string world_name) {
  g_difficulty16 = difficulty;
  g_world_name16 = std::move(world_name);
}

static std::vector<std::string> out16(std::string_view buf) {
  const auto pid = static_cast<std::uint8_t>(buf[0]);
  Rd r(buf, 1);
  Wr w;
  switch (pid) {
    case RESOURCE_PACKS_INFO_PACKET: return {std::string("\x07\x00\x00\x00\x00\x00", 6)};
    case AVAILABLE_COMMANDS_PACKET: {
      auto cmds = r.str();
      auto unk = r.str();
      w.u8(0x4b).str16(cmds).str16(unk);
      return {w.s};
    }
    case BATCH_PACKET: return {batch16FromNative(buf)};
    case PLAY_STATUS_PACKET: return {"\x02" + std::string(buf.substr(1, 4))};
    case DISCONNECT_PACKET: w.u8(0x05).u8(0).str16(r.str()); return {w.s};
    case TEXT_PACKET: {
      auto type = r.u8();
      w.u8(0x0a).u8(type);
      switch (type) {
        case 3:
        case 1: w.str16(r.str()); [[fallthrough]];
        case 0:
        case 4:
        case 5: w.str16(r.str()); break;
        case 2: {
          w.str16(r.str());
          auto n = r.u8();
          w.uvar(n);
          for (int i = 0; i < n; ++i) w.str16(r.str());
          break;
        }
      }
      return {w.s};
    }
    case SET_TIME_PACKET: {
      auto t = r.i32();
      auto started = r.u8();
      w.u8(0x0b).var(t).u8(started ? 1 : 0);
      return {w.s};
    }
    case START_GAME_PACKET: {
      auto seed = r.i32();
      auto dim = r.u8();
      auto gen = r.i32();
      auto gm = r.i32();
      (void)r.i64();
      auto sx = r.i32(), sy = r.i32(), sz = r.i32();
      float x = r.f32(), y = r.f32(), z = r.f32();
      w.u8(0x0c).var(0).var(0).vec3(x, y, z).lf32(0).lf32(0).var(seed).var(dim).var(gen).var(gm)
          .var(g_difficulty16).bc16(sx, sy, sz).u8(1).var(-1).u8(0).lf32(0).lf32(0).u8(1).u8(0)
          .str16("").str16(g_world_name16);
      return {w.s};
    }
    case ADD_PLAYER_PACKET: {
      auto uuid = r.raw(16);
      auto name = r.str();
      auto eid = r.i64();
      float x = r.f32(), y = r.f32(), z = r.f32();
      float sx = r.f32(), sy = r.f32(), sz = r.f32();
      float yaw = r.f32();
      (void)r.f32(); // headYaw
      float pitch = r.f32();
      auto held = readSlot14(r);
      auto meta = readMeta14(r);
      w.u8(0x0d).raw(uuid).str16(name).var(eid).var(eid).vec3(x, y, z).vec3(sx, sy, sz).lf32(pitch).lf32(yaw).lf32(yaw);
      writeSlot16(w, held);
      writeMeta16(w, meta);
      return {w.s};
    }
    case REMOVE_PLAYER_PACKET:
    case REMOVE_ENTITY_PACKET: w.u8(0x0f).var(r.i64()); return {w.s};
    case ADD_ENTITY_PACKET: {
      auto eid = r.i64();
      auto type = r.i32();
      float x = r.f32(), y = r.f32(), z = r.f32();
      float sx = r.f32(), sy = r.f32(), sz = r.f32();
      float yaw = r.f32(), pitch = r.f32();
      auto meta = readMeta14(r);
      w.u8(0x0e).var(eid).var(eid).uvar(static_cast<std::uint32_t>(type)).vec3(x, y, z).vec3(sx, sy, sz)
          .lf32(yaw * (256.f / 360.f)).lf32(pitch * (256.f / 360.f)).uvar(0);
      writeMeta16(w, meta);
      auto nl = r.left() >= 2 ? r.be16() : 0;
      w.uvar(nl);
      for (int i = 0; i < nl; ++i) {
        auto from = r.i64(), to = r.i64();
        auto t = r.u8();
        w.var(from).var(to).u8(t);
      }
      return {w.s};
    }
    case ADD_ITEM_ENTITY_PACKET: {
      auto eid = r.i64();
      auto s = readSlot14(r);
      float x = r.f32(), y = r.f32(), z = r.f32();
      float sx = r.f32(), sy = r.f32(), sz = r.f32();
      w.u8(0x10).var(eid).var(eid);
      writeSlot16(w, s);
      w.vec3(x, y, z).vec3(sx, sy, sz);
      return {w.s};
    }
    case TAKE_ITEM_ENTITY_PACKET: {
      auto target = r.i64(), eid = r.i64();
      w.u8(0x12).var(target).var(eid);
      return {w.s};
    }
    case MOVE_ENTITY_PACKET: {
      std::vector<std::string> out;
      auto n = r.i32();
      for (int i = 0; i < n; ++i) {
        auto eid = r.i64();
        float x = r.f32(), y = r.f32(), z = r.f32();
        float yaw = r.f32(), head = r.f32(), pitch = r.f32();
        Wr e;
        e.u8(0x13).var(eid).vec3(x, y, z).u8(angle8(pitch)).u8(angle8(yaw)).u8(angle8(head));
        out.push_back(std::move(e.s));
      }
      return out;
    }
    case MOVE_PLAYER_PACKET: {
      auto eid = r.i64();
      float x = r.f32(), y = r.f32(), z = r.f32();
      float yaw = r.f32(), body = r.f32(), pitch = r.f32();
      auto mode = r.u8();
      auto og = r.eof() ? 0 : r.u8();
      w.u8(0x14).var(eid).vec3(x, y, z).lf32(pitch).lf32(yaw).lf32(body).u8(mode).u8(og ? 1 : 0);
      return {w.s};
    }
    case UPDATE_BLOCK_PACKET: {
      std::vector<std::string> out;
      auto n = r.i32();
      for (int i = 0; i < n; ++i) {
        auto x = r.i32(), z = r.i32();
        auto y = r.u8(), id = r.u8(), fm = r.u8();
        Wr e;
        e.u8(0x17).bc16(x, y, z).uvar(id).uvar(((fm >> 4) << 4) | (fm & 0x0f));
        out.push_back(std::move(e.s));
      }
      return out;
    }
    case ADD_PAINTING_PACKET: {
      auto eid = r.i64();
      auto x = r.i32(), y = r.i32(), z = r.i32(), dir = r.i32();
      auto title = r.str();
      w.u8(0x18).var(eid).var(eid).bc16(x, y, z).var(dir).str16(title);
      return {w.s};
    }
    case EXPLODE_PACKET: {
      float x = r.f32(), y = r.f32(), z = r.f32(), rad = r.f32();
      auto n = r.i32();
      w.u8(0x19).vec3(x, y, z).lf32(rad).uvar(static_cast<std::uint32_t>(std::max(0, n)));
      for (int i = 0; i < n; ++i) {
        auto bx = static_cast<std::int8_t>(r.u8()), by = static_cast<std::int8_t>(r.u8()), bz = static_cast<std::int8_t>(r.u8());
        w.bc16(bx, by, bz);
      }
      return {w.s};
    }
    case LEVEL_EVENT_PACKET: {
      auto ev = static_cast<std::int16_t>(r.be16());
      float x = r.f32(), y = r.f32(), z = r.f32();
      auto data = r.i32();
      w.u8(0x1b).var(ev).vec3(x, y, z).var(data);
      return {w.s};
    }
    case BLOCK_EVENT_PACKET: {
      auto x = r.i32(), y = r.i32(), z = r.i32(), c1 = r.i32(), c2 = r.i32();
      w.u8(0x1c).bc16(x, y, z).var(c1).var(c2);
      return {w.s};
    }
    case ENTITY_EVENT_PACKET: {
      auto eid = r.i64();
      auto ev = r.u8();
      w.u8(0x1d).var(eid).u8(ev).var(0);
      return {w.s};
    }
    case MOB_EFFECT_PACKET: {
      auto eid = r.i64();
      auto ev = r.u8(), id = r.u8(), amp = r.u8(), part = r.u8();
      auto dur = r.i32();
      w.u8(0x1e).var(eid).u8(ev).var(id).var(amp).u8(part ? 1 : 0).var(dur);
      return {w.s};
    }
    case UPDATE_ATTRIBUTES_PACKET: {
      auto eid = r.i64();
      auto n = r.be16();
      w.u8(0x1f).var(eid).uvar(n);
      for (int i = 0; i < n; ++i) {
        float mn = r.f32(), mx = r.f32(), v = r.f32();
        auto name = r.str();
        // native has no default value; PHP getDefaultValue() == initial default (use current)
        w.lf32(mn).lf32(mx).lf32(v).lf32(v).str16(attributeName16(name));
      }
      return {w.s};
    }
    case MOB_EQUIPMENT_PACKET: {
      auto eid = r.i64();
      auto s = readSlot14(r);
      auto slot = r.u8(), sel = r.u8();
      w.u8(0x20).var(eid);
      writeSlot16(w, s);
      w.u8(slot).u8(sel).u8(0);
      return {w.s};
    }
    case MOB_ARMOR_EQUIPMENT_PACKET: {
      auto eid = r.i64();
      w.u8(0x21).var(eid);
      for (int i = 0; i < 4; ++i) writeSlot16(w, readSlot14(r));
      return {w.s};
    }
    case INTERACT_PACKET: {
      auto a = r.u8();
      auto t = r.i64();
      w.u8(0x22).u8(a).var(t);
      return {w.s};
    }
    case HURT_ARMOR_PACKET: w.u8(0x25).var(static_cast<std::int8_t>(r.u8())); return {w.s};
    case SET_ENTITY_DATA_PACKET: {
      auto eid = r.i64();
      auto meta = readMeta14(r);
      w.u8(0x26).var(eid);
      writeMeta16(w, meta);
      return {w.s};
    }
    case SET_ENTITY_MOTION_PACKET: {
      std::vector<std::string> out;
      auto n = r.i32();
      for (int i = 0; i < n; ++i) {
        auto eid = r.i64();
        float x = r.f32(), y = r.f32(), z = r.f32();
        Wr e;
        e.u8(0x27).var(eid).vec3(x, y, z);
        out.push_back(std::move(e.s));
      }
      return out;
    }
    case SET_ENTITY_LINK_PACKET: {
      auto f = r.i64(), t = r.i64();
      auto ty = r.u8();
      w.u8(0x28).var(f).var(t).u8(ty);
      return {w.s};
    }
    case SET_HEALTH_PACKET: w.u8(0x29).var(r.i32()); return {w.s};
    case SET_SPAWN_POSITION_PACKET: {
      auto x = r.i32(), y = r.i32(), z = r.i32();
      w.u8(0x2a).var(0).bc16(x, y, z).u8(0);
      return {w.s};
    }
    case ANIMATE_PACKET: {
      auto a = r.u8();
      auto eid = r.i64();
      w.u8(0x2b).var(a).var(eid);
      return {w.s};
    }
    case RESPAWN_PACKET: {
      float x = r.f32(), y = r.f32(), z = r.f32();
      w.u8(0x2c).vec3(x, y, z);
      return {w.s};
    }
    case CONTAINER_OPEN_PACKET: {
      auto wid = r.u8(), type = r.u8();
      auto slots = static_cast<std::int16_t>(r.be16());
      auto x = r.i32(), y = r.i32(), z = r.i32();
      std::int64_t ent = r.left() >= 8 ? r.i64() : -1;
      w.u8(0x2f).u8(wid).u8(type).var(slots).bc16(x, y, z).var(ent);
      return {w.s};
    }
    case CONTAINER_CLOSE_PACKET: w.u8(0x30).u8(r.u8()); return {w.s};
    case CONTAINER_SET_SLOT_PACKET: {
      auto wid = r.u8();
      auto slot = static_cast<std::int16_t>(r.be16());
      auto hb = static_cast<std::int16_t>(r.be16());
      auto s = readSlot14(r);
      w.u8(0x31).u8(wid).var(slot).var(hb);
      writeSlot16(w, s);
      return {w.s};
    }
    case CONTAINER_SET_DATA_PACKET: {
      auto wid = r.u8();
      auto prop = static_cast<std::int16_t>(r.be16());
      auto val = static_cast<std::int16_t>(r.be16());
      w.u8(0x32).u8(wid).var(prop).var(val);
      return {w.s};
    }
    case CONTAINER_SET_CONTENT_PACKET: {
      auto wid = r.u8();
      auto n = r.be16();
      w.u8(0x33).u8(wid).uvar(n);
      for (int i = 0; i < n; ++i) writeSlot16(w, readSlot14(r));
      std::vector<std::int32_t> hotbar;
      if (r.left() >= 2) {
        auto hn = r.be16();
        for (int i = 0; i < hn; ++i) hotbar.push_back(r.i32());
      }
      if (wid == 0 && !hotbar.empty()) {
        w.uvar(hotbar.size());
        for (auto h : hotbar) w.var(h);
      } else {
        w.uvar(0);
      }
      return {w.s};
    }
    case CRAFTING_DATA_PACKET: return {crafting16(buf)};
    case ADVENTURE_SETTINGS_PACKET: {
      auto f = r.be32();
      std::uint32_t up = r.left() >= 4 ? r.be32() : 2;
      std::uint32_t f16 = f & 0x0f;
      if (f & 0x40) f16 |= 0x20;
      if (f & 0x80) f16 |= 0x40;
      if (f & 0x100) f16 |= 0x80;
      w.u8(0x36).uvar(f16).uvar(up);
      return {w.s};
    }
    case BLOCK_ENTITY_DATA_PACKET: {
      auto x = r.i32(), y = r.i32(), z = r.i32();
      w.u8(0x37).bc16(x, y, z).raw(r.rest());
      return {w.s};
    }
    case FULL_CHUNK_DATA_PACKET: return {chunk16FromNative(buf)};
    case SET_DIFFICULTY_PACKET: w.u8(0x3b).uvar(static_cast<std::uint32_t>(r.i32())); return {w.s};
    case CHANGE_DIMENSION_PACKET: w.u8(0x3c).var(r.u8()).vec3(0, 0, 0).u8(0); return {w.s};
    case SET_PLAYER_GAMETYPE_PACKET: w.u8(0x3d).uvar(static_cast<std::uint32_t>(r.i32())); return {w.s};
    case PLAYER_LIST_PACKET: {
      auto type = r.u8();
      auto n = r.i32();
      w.u8(0x3e).u8(type).uvar(static_cast<std::uint32_t>(std::max(0, n)));
      for (int i = 0; i < n; ++i) {
        w.raw(r.raw(16));
        if (type == 0) {
          auto eid = r.i64();
          auto name = r.str(), skin_id = r.str(), skin = r.str();
          w.var(eid).str16(name).str16(skin_id).str16(skin);
        }
      }
      return {w.s};
    }
    case CHUNK_RADIUS_UPDATE_PACKET: w.u8(0x44).var(r.i32()); return {w.s};
    default: return {};
  }
}

} // namespace mpmpes::protocol::mp
