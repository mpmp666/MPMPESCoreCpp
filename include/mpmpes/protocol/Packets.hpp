#pragma once

#include "mpmpes/binary/BinaryStream.hpp"
#include "mpmpes/item/Item.hpp"
#include "mpmpes/protocol/Info.hpp"

#include <array>
#include <functional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mpmpes::protocol {

// ---------- clientbound encoders ----------

inline std::string encodePlayStatus(std::int32_t status) {
  binary::BinaryStream out;
  out.putByte(PLAY_STATUS_PACKET);
  out.putInt(status);
  return out.buffer();
}

inline std::string encodeDisconnect(std::string_view message) {
  binary::BinaryStream out;
  out.putByte(DISCONNECT_PACKET);
  out.putString(message);
  return out.buffer();
}

inline std::string encodeStartGame(std::int32_t seed, std::uint8_t dimension,
                                   std::int32_t generator, std::int32_t gamemode,
                                   std::int64_t eid, std::int32_t spawn_x, std::int32_t spawn_y,
                                   std::int32_t spawn_z, float x, float y, float z,
                                   std::string_view unknown = "") {
  binary::BinaryStream out;
  out.putByte(START_GAME_PACKET);
  out.putInt(seed);
  out.putByte(dimension);
  out.putInt(generator);
  out.putInt(gamemode);
  out.putLong(eid);
  out.putInt(spawn_x);
  out.putInt(spawn_y);
  out.putInt(spawn_z);
  out.putFloat(x);
  out.putFloat(y);
  out.putFloat(z);
  out.putByte(1);
  out.putByte(1);
  out.putByte(0);
  out.putString(unknown);
  return out.buffer();
}

inline std::string encodeSetTime(std::int32_t time, bool started = true) {
  binary::BinaryStream out;
  out.putByte(SET_TIME_PACKET);
  out.putInt(time);
  out.putByte(started ? 1 : 0);
  return out.buffer();
}

// PE 0.14 ChangeDimension: dimension byte + unknown byte (PM puts 0).
// Pass dimension as-is (0/1 common; other values allowed for client experiments).
inline std::string encodeChangeDimension(std::uint8_t dimension) {
  binary::BinaryStream out;
  out.putByte(CHANGE_DIMENSION_PACKET);
  out.putByte(dimension);
  out.putByte(0);
  return out.buffer();
}

// EntityEvent 0xa4 — hurt/death animations (PM Living::attack)
inline std::string encodeEntityEvent(std::int64_t eid, std::uint8_t event) {
  binary::BinaryStream out;
  out.putByte(ENTITY_EVENT_PACKET);
  out.putLong(eid);
  out.putByte(event);
  return out.buffer();
}

// Animate 0xb2 — arm swing etc (PM: action byte + eid long)
// action 1 = swing arm (PE 0.14)
inline constexpr std::uint8_t ANIMATE_SWING_ARM = 1;

inline std::string encodeAnimate(std::uint8_t action, std::int64_t eid) {
  binary::BinaryStream out;
  out.putByte(ANIMATE_PACKET);
  out.putByte(action);
  out.putLong(eid);
  return out.buffer();
}

struct AnimateDecoded {
  std::uint8_t action = 0;
  std::int64_t eid = 0;
  bool ok = false;
};

inline AnimateDecoded decodeAnimate(std::string_view buffer) {
  AnimateDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != ANIMATE_PACKET) return d;
    d.action = in.getByte();
    d.eid = in.getLong();
    d.ok = true;
  } catch (...) {
  }
  return d;
}

// Respawn 0xb3
inline std::string encodeRespawn(float x, float y, float z) {
  binary::BinaryStream out;
  out.putByte(RESPAWN_PACKET);
  out.putFloat(x);
  out.putFloat(y);
  out.putFloat(z);
  return out.buffer();
}

inline std::string encodeSetSpawnPosition(std::int32_t x, std::int32_t y, std::int32_t z) {
  binary::BinaryStream out;
  out.putByte(SET_SPAWN_POSITION_PACKET);
  out.putInt(x);
  out.putInt(y);
  out.putInt(z);
  return out.buffer();
}

inline std::string encodeSetHealth(std::int32_t health) {
  binary::BinaryStream out;
  out.putByte(SET_HEALTH_PACKET);
  out.putInt(health);
  return out.buffer();
}

inline std::string encodeSetDifficulty(std::int32_t difficulty) {
  binary::BinaryStream out;
  out.putByte(SET_DIFFICULTY_PACKET);
  out.putInt(difficulty);
  return out.buffer();
}

inline std::string encodeSetPlayerGameType(std::int32_t gamemode) {
  binary::BinaryStream out;
  out.putByte(SET_PLAYER_GAMETYPE_PACKET);
  out.putInt(gamemode);
  return out.buffer();
}

inline std::string encodeFullChunkData(std::int32_t chunk_x, std::int32_t chunk_z,
                                       std::string_view payload,
                                       std::uint8_t order = CHUNK_ORDER_COLUMNS) {
  binary::BinaryStream out;
  out.putByte(FULL_CHUNK_DATA_PACKET);
  out.putInt(chunk_x);
  out.putInt(chunk_z);
  out.putByte(order);
  out.putInt(static_cast<std::int32_t>(payload.size()));
  out.put(payload);
  return out.buffer();
}

inline std::string encodeChunkRadiusUpdate(std::int32_t radius) {
  binary::BinaryStream out;
  out.putByte(CHUNK_RADIUS_UPDATE_PACKET);
  out.putInt(radius);
  return out.buffer();
}

inline std::string encodeAdventureSettings(std::int32_t flags, std::int32_t user_perm = 2,
                                           std::int32_t global_perm = 2) {
  binary::BinaryStream out;
  out.putByte(ADVENTURE_SETTINGS_PACKET);
  out.putInt(flags);
  out.putInt(user_perm);
  out.putInt(global_perm);
  return out.buffer();
}

inline std::string encodeTextSystem(std::string_view message) {
  binary::BinaryStream out;
  out.putByte(TEXT_PACKET);
  out.putByte(TEXT_SYSTEM);
  out.putString(message);
  return out.buffer();
}

inline std::string encodeTextChat(std::string_view source, std::string_view message) {
  binary::BinaryStream out;
  out.putByte(TEXT_PACKET);
  out.putByte(TEXT_CHAT);
  out.putString(source);
  out.putString(message);
  return out.buffer();
}

inline std::string encodeTextRaw(std::string_view message) {
  binary::BinaryStream out;
  out.putByte(TEXT_PACKET);
  out.putByte(TEXT_RAW);
  out.putString(message);
  return out.buffer();
}

inline std::string encodeMovePlayer(std::int64_t eid, float x, float y, float z, float yaw,
                                    float body_yaw, float pitch, std::uint8_t mode = 0,
                                    bool on_ground = true) {
  binary::BinaryStream out;
  out.putByte(MOVE_PLAYER_PACKET);
  out.putLong(eid);
  out.putFloat(x);
  out.putFloat(y);
  out.putFloat(z);
  out.putFloat(yaw);
  out.putFloat(body_yaw);
  out.putFloat(pitch);
  out.putByte(mode);
  out.putByte(on_ground ? 1 : 0);
  return out.buffer();
}

// UpdateBlock: records = x,z,y,id,data,flags
inline std::string encodeUpdateBlock(std::int32_t x, std::int32_t y, std::int32_t z,
                                     std::uint8_t block_id, std::uint8_t meta,
                                     std::uint8_t flags = UPDATE_FLAG_ALL) {
  binary::BinaryStream out;
  out.putByte(UPDATE_BLOCK_PACKET);
  out.putInt(1);
  out.putInt(x);
  out.putInt(z);
  out.putByte(static_cast<std::uint8_t>(y & 0xff));
  out.putByte(block_id);
  out.putByte(static_cast<std::uint8_t>((flags << 4) | (meta & 0x0f)));
  return out.buffer();
}

inline std::string encodeLevelEvent(std::int16_t evid, float x, float y, float z,
                                    std::int32_t data = 0) {
  binary::BinaryStream out;
  out.putByte(LEVEL_EVENT_PACKET);
  out.putShort(static_cast<std::uint16_t>(evid));
  out.putFloat(x);
  out.putFloat(y);
  out.putFloat(z);
  out.putInt(data);
  return out.buffer();
}

inline std::string encodeDestroyBlockParticle(float x, float y, float z, std::uint8_t block_id,
                                              std::uint8_t meta = 0) {
  // DestroyBlockParticle: data = id + (damage << 12)
  const std::int32_t data =
      static_cast<std::int32_t>(block_id) + (static_cast<std::int32_t>(meta) << 12);
  return encodeLevelEvent(EVENT_PARTICLE_DESTROY, x, y, z, data);
}

// BlockEvent 0xa3 — chest lid open/close (PM ChestInventory: case1=1, case2=2 open / 0 close)
inline std::string encodeBlockEvent(std::int32_t x, std::int32_t y, std::int32_t z,
                                    std::int32_t case1, std::int32_t case2) {
  binary::BinaryStream out;
  out.putByte(BLOCK_EVENT_PACKET);
  out.putInt(x);
  out.putInt(y);
  out.putInt(z);
  out.putInt(case1);
  out.putInt(case2);
  return out.buffer();
}

inline std::string encodeRemoveEntity(std::int64_t eid) {
  binary::BinaryStream out;
  out.putByte(REMOVE_ENTITY_PACKET);
  out.putLong(eid);
  return out.buffer();
}

// RemovePlayer 0x97 — PE Human despawn (eid + uuid)
inline std::string encodeRemovePlayer(std::int64_t eid, const std::array<std::uint8_t, 16>& uuid) {
  binary::BinaryStream out;
  out.putByte(REMOVE_PLAYER_PACKET);
  out.putLong(eid);
  out.put(std::string_view(reinterpret_cast<const char*>(uuid.data()), 16));
  return out.buffer();
}

// Minimal metadata terminator for AddEntity
inline std::string encodeEmptyMetadata() {
  // DATA_FLAGS byte 0, DATA_AIR short 300, DATA_SHOW_NAMETAG 1, terminator 0x7f
  std::string m;
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_FLAGS & 0x1f)));
  m.push_back(0);
  m.push_back(static_cast<char>((DATA_TYPE_SHORT << 5) | (DATA_AIR & 0x1f)));
  m.push_back(static_cast<char>(300 & 0xff));
  m.push_back(static_cast<char>((300 >> 8) & 0xff)); // LShort
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_SHOW_NAMETAG & 0x1f)));
  m.push_back(1);
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_NO_AI & 0x1f)));
  m.push_back(0); // AI enabled
  m.push_back(0x7f);
  return m;
}

// Sheep metadata: base + DATA_COLOR_INFO (color | 0x10 if sheared)
inline std::string encodeSheepMetadata(std::uint8_t color, bool sheared) {
  std::string m;
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_FLAGS & 0x1f)));
  m.push_back(0);
  m.push_back(static_cast<char>((DATA_TYPE_SHORT << 5) | (DATA_AIR & 0x1f)));
  m.push_back(static_cast<char>(300 & 0xff));
  m.push_back(static_cast<char>((300 >> 8) & 0xff));
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_SHOW_NAMETAG & 0x1f)));
  m.push_back(1);
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_NO_AI & 0x1f)));
  m.push_back(0);
  std::uint8_t c = static_cast<std::uint8_t>(color & 0x0f);
  if (sheared) c = static_cast<std::uint8_t>(c | 0x10);
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_COLOR_INFO & 0x1f)));
  m.push_back(static_cast<char>(c));
  m.push_back(0x7f);
  return m;
}

// SetEntityData 0xad — long eid + metadata blob (same format as AddEntity meta)
inline std::string encodeSetEntityData(std::int64_t eid, std::string_view metadata) {
  binary::BinaryStream out;
  out.putByte(SET_ENTITY_DATA_PACKET);
  out.putLong(eid);
  out.put(metadata);
  return out.buffer();
}

// Player/Human metadata for AddPlayer (nametag + show nametag)
inline std::string encodePlayerMetadata(std::string_view nametag) {
  std::string m;
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_FLAGS & 0x1f)));
  m.push_back(0);
  m.push_back(static_cast<char>((DATA_TYPE_SHORT << 5) | (DATA_AIR & 0x1f)));
  m.push_back(static_cast<char>(300 & 0xff));
  m.push_back(static_cast<char>((300 >> 8) & 0xff));
  // DATA_NAMETAG string (LShort len + bytes) — PE writeMetadata STRING type
  m.push_back(static_cast<char>((DATA_TYPE_STRING << 5) | (DATA_NAMETAG & 0x1f)));
  const auto n = static_cast<std::uint16_t>(std::min(nametag.size(), static_cast<std::size_t>(0xffff)));
  m.push_back(static_cast<char>(n & 0xff));
  m.push_back(static_cast<char>((n >> 8) & 0xff));
  m.append(nametag.data(), n);
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_SHOW_NAMETAG & 0x1f)));
  m.push_back(1);
  m.push_back(static_cast<char>((DATA_TYPE_BYTE << 5) | (DATA_NO_AI & 0x1f)));
  m.push_back(0);
  m.push_back(0x7f);
  return m;
}

// AddPlayer 0x96 — world entity for another human (PM Human::spawnTo)
// uuid(16) + name + eid + xyz + speed xyz + yaw + headYaw + pitch + slot + metadata
inline std::string encodeAddPlayer(const std::array<std::uint8_t, 16>& uuid, std::string_view username,
                                   std::int64_t eid, float x, float y, float z, float yaw, float pitch,
                                   const item::ItemStack& held = item::ItemStack::air(),
                                   std::string_view metadata = {}) {
  binary::BinaryStream out;
  out.putByte(ADD_PLAYER_PACKET);
  out.put(std::string_view(reinterpret_cast<const char*>(uuid.data()), 16));
  out.putString(username);
  out.putLong(eid);
  out.putFloat(x);
  out.putFloat(y);
  out.putFloat(z);
  out.putFloat(0.f); // speedX
  out.putFloat(0.f); // speedY
  out.putFloat(0.f); // speedZ
  out.putFloat(yaw);
  out.putFloat(yaw); // head rotation (PM: yaw twice)
  out.putFloat(pitch);
  item::putSlot(out, held);
  if (metadata.empty()) {
    auto meta = encodePlayerMetadata(username);
    out.put(meta);
  } else {
    out.put(metadata);
  }
  return out.buffer();
}

inline std::string encodeAddEntity(std::int64_t eid, std::int32_t type, float x, float y, float z,
                                   float yaw = 0, float pitch = 0, float sx = 0, float sy = 0,
                                   float sz = 0, std::string_view metadata = {}) {
  binary::BinaryStream out;
  out.putByte(ADD_ENTITY_PACKET);
  out.putLong(eid);
  out.putInt(type);
  out.putFloat(x);
  out.putFloat(y);
  out.putFloat(z);
  out.putFloat(sx);
  out.putFloat(sy);
  out.putFloat(sz);
  out.putFloat(yaw);
  out.putFloat(pitch);
  if (metadata.empty()) {
    auto meta = encodeEmptyMetadata();
    out.put(meta);
  } else {
    out.put(metadata);
  }
  out.putShort(0); // links
  return out.buffer();
}

// MoveEntity batch of one
inline std::string encodeMoveEntity(std::int64_t eid, float x, float y, float z, float yaw,
                                    float head_yaw, float pitch) {
  binary::BinaryStream out;
  out.putByte(MOVE_ENTITY_PACKET);
  out.putInt(1);
  out.putLong(eid);
  out.putFloat(x);
  out.putFloat(y);
  out.putFloat(z);
  out.putFloat(yaw);
  out.putFloat(head_yaw);
  out.putFloat(pitch);
  return out.buffer();
}

// AddItemEntity 0x9a — dropped item on ground
inline std::string encodeAddItemEntity(std::int64_t eid, const item::ItemStack& stack, float x,
                                       float y, float z, float sx = 0, float sy = 0, float sz = 0) {
  binary::BinaryStream out;
  out.putByte(ADD_ITEM_ENTITY_PACKET);
  out.putLong(eid);
  item::putSlot(out, stack);
  out.putFloat(x);
  out.putFloat(y);
  out.putFloat(z);
  out.putFloat(sx);
  out.putFloat(sy);
  out.putFloat(sz);
  return out.buffer();
}

// TakeItemEntity 0x9b — pickup animation (target=item eid, eid=collector)
inline std::string encodeTakeItemEntity(std::int64_t target_item_eid, std::int64_t collector_eid) {
  binary::BinaryStream out;
  out.putByte(TAKE_ITEM_ENTITY_PACKET);
  out.putLong(target_item_eid);
  out.putLong(collector_eid);
  return out.buffer();
}

// SetEntityMotion 0xae — PM: int count + (long eid, float mx,my,mz)*
inline std::string encodeSetEntityMotion(std::int64_t eid, float mx, float my, float mz) {
  binary::BinaryStream out;
  out.putByte(SET_ENTITY_MOTION_PACKET);
  out.putInt(1);
  out.putLong(eid);
  out.putFloat(mx);
  out.putFloat(my);
  out.putFloat(mz);
  return out.buffer();
}

inline std::string encodeContainerSetContent(std::uint8_t window_id,
                                             const std::vector<item::ItemStack>& slots,
                                             const std::vector<std::int32_t>& hotbar = {}) {
  binary::BinaryStream out;
  out.putByte(CONTAINER_SET_CONTENT_PACKET);
  out.putByte(window_id);
  out.putShort(static_cast<std::uint16_t>(slots.size()));
  for (const auto& s : slots) item::putSlot(out, s);
  // PM always writes hotbar short count (0 if not inventory / empty)
  if (window_id == WINDOW_INVENTORY && !hotbar.empty()) {
    out.putShort(static_cast<std::uint16_t>(hotbar.size()));
    for (auto h : hotbar) out.putInt(h);
  } else {
    out.putShort(0);
  }
  return out.buffer();
}

// ContainerSetSlot 0xb7 (server → client resync one slot)
inline std::string encodeContainerSetSlot(std::uint8_t window_id, std::int16_t slot,
                                          std::int16_t hotbar_slot,
                                          const item::ItemStack& stack) {
  binary::BinaryStream out;
  out.putByte(CONTAINER_SET_SLOT_PACKET);
  out.putByte(window_id);
  out.putShort(static_cast<std::uint16_t>(slot));
  out.putShort(static_cast<std::uint16_t>(hotbar_slot));
  item::putSlot(out, stack);
  return out.buffer();
}

// ContainerOpen 0xb5 — open chest/workbench UI (PHP ContainerOpenPacket)
inline std::string encodeContainerOpen(std::uint8_t window_id, std::uint8_t type,
                                       std::int16_t slots, std::int32_t x, std::int32_t y,
                                       std::int32_t z, std::int64_t entity_id = -1) {
  binary::BinaryStream out;
  out.putByte(CONTAINER_OPEN_PACKET);
  out.putByte(window_id);
  out.putByte(type);
  out.putShort(static_cast<std::uint16_t>(slots));
  out.putInt(x);
  out.putInt(y);
  out.putInt(z);
  out.putLong(entity_id);
  return out.buffer();
}

// ContainerSetData 0xb8 — furnace progress (prop 0 cook, 1 burn ticks 0-200)
inline std::string encodeContainerSetData(std::uint8_t window_id, std::int16_t property,
                                          std::int16_t value) {
  binary::BinaryStream out;
  out.putByte(CONTAINER_SET_DATA_PACKET);
  out.putByte(window_id);
  out.putShort(static_cast<std::uint16_t>(property));
  out.putShort(static_cast<std::uint16_t>(value));
  return out.buffer();
}

// ContainerClose 0xb6
inline std::string encodeContainerClose(std::uint8_t window_id) {
  binary::BinaryStream out;
  out.putByte(CONTAINER_CLOSE_PACKET);
  out.putByte(window_id);
  return out.buffer();
}

// ---- Minimal little-endian NBT (PM Spawnable uses NBT::LITTLE_ENDIAN) ----
// writeTag: [type u8][name: LShort len + bytes][payload]; TAG_End is type only.
namespace nbt_le {
inline constexpr std::uint8_t TAG_End = 0;
inline constexpr std::uint8_t TAG_Short = 2;
inline constexpr std::uint8_t TAG_Int = 3;
inline constexpr std::uint8_t TAG_String = 8;
inline constexpr std::uint8_t TAG_Compound = 10;

inline void putName(binary::BinaryStream& o, std::string_view name) {
  o.putLShort(static_cast<std::uint16_t>(name.size()));
  if (!name.empty()) o.put(name);
}
inline void putStringTag(binary::BinaryStream& o, std::string_view name, std::string_view val) {
  o.putByte(TAG_String);
  putName(o, name);
  o.putLShort(static_cast<std::uint16_t>(val.size()));
  if (!val.empty()) o.put(val);
}
inline void putIntTag(binary::BinaryStream& o, std::string_view name, std::int32_t v) {
  o.putByte(TAG_Int);
  putName(o, name);
  o.putLInt(v);
}
inline void putShortTag(binary::BinaryStream& o, std::string_view name, std::int16_t v) {
  o.putByte(TAG_Short);
  putName(o, name);
  o.putLShort(static_cast<std::uint16_t>(v));
}
// Root unnamed compound: type + empty name + children + End
inline std::string writeRootCompound(const std::function<void(binary::BinaryStream&)>& children) {
  binary::BinaryStream o;
  o.putByte(TAG_Compound);
  putName(o, "");
  children(o);
  o.putByte(TAG_End);
  return o.buffer();
}
} // namespace nbt_le

// BlockEntityData 0xbd — PE expects LE NBT payload after xyz (PM Spawnable::spawnTo)
inline std::string encodeBlockEntityData(std::int32_t x, std::int32_t y, std::int32_t z,
                                         const std::string& namedtag_le) {
  binary::BinaryStream out;
  out.putByte(BLOCK_ENTITY_DATA_PACKET);
  out.putInt(x);
  out.putInt(y);
  out.putInt(z);
  out.put(namedtag_le);
  return out.buffer();
}

// Furnace tile spawn compound (PM tile/Furnace::getSpawnCompound)
inline std::string encodeFurnaceSpawnNbt(std::int32_t x, std::int32_t y, std::int32_t z,
                                         std::int16_t burn_time, std::int16_t cook_time) {
  return nbt_le::writeRootCompound([&](binary::BinaryStream& o) {
    nbt_le::putStringTag(o, "id", "Furnace");
    nbt_le::putIntTag(o, "x", x);
    nbt_le::putIntTag(o, "y", y);
    nbt_le::putIntTag(o, "z", z);
    nbt_le::putShortTag(o, "BurnTime", burn_time);
    nbt_le::putShortTag(o, "CookTime", cook_time);
  });
}

// Chest tile spawn compound (PM Chest::getSpawnCompound)
// paired: include pairx/pairz so PE draws double-chest model (not ghost/transparent).
inline std::string encodeChestSpawnNbt(std::int32_t x, std::int32_t y, std::int32_t z,
                                       bool paired = false, std::int32_t pair_x = 0,
                                       std::int32_t pair_z = 0) {
  return nbt_le::writeRootCompound([&](binary::BinaryStream& o) {
    nbt_le::putStringTag(o, "id", "Chest");
    nbt_le::putIntTag(o, "x", x);
    nbt_le::putIntTag(o, "y", y);
    nbt_le::putIntTag(o, "z", z);
    if (paired) {
      nbt_le::putIntTag(o, "pairx", pair_x);
      nbt_le::putIntTag(o, "pairz", pair_z);
    }
  });
}

// Batch 0x92: [pid][int32 zlib_len][zlib_deflate(concat int32_len + packet...)]
// payloads are bare DataPacket buffers (NO 0x8e inside).
inline std::string encodeBatch(const std::string& zlib_payload) {
  binary::BinaryStream out;
  out.putByte(BATCH_PACKET);
  out.putInt(static_cast<std::int32_t>(zlib_payload.size()));
  out.put(zlib_payload);
  return out.buffer();
}

// PlayerList 0xc3 — pause-menu / tab list
// TYPE_ADD entry: UUID(16) + long eid + string name + string skinName + string skinData
// TYPE_REMOVE entry: UUID(16)
inline constexpr std::uint8_t PLAYER_LIST_TYPE_ADD = 0;
inline constexpr std::uint8_t PLAYER_LIST_TYPE_REMOVE = 1;

struct PlayerListEntry {
  std::array<std::uint8_t, 16> uuid{};
  std::int64_t eid = 0;
  std::string name;
  std::string skin_name = "Standard_Custom";
  std::string skin_data; // 64*32*4 or 64*64*4 raw RGBA, or empty
};

inline std::string encodePlayerListAdd(const std::vector<PlayerListEntry>& entries) {
  binary::BinaryStream out;
  out.putByte(PLAYER_LIST_PACKET);
  out.putByte(PLAYER_LIST_TYPE_ADD);
  out.putInt(static_cast<std::int32_t>(entries.size()));
  for (const auto& e : entries) {
    out.put(std::string_view(reinterpret_cast<const char*>(e.uuid.data()), 16));
    out.putLong(e.eid);
    out.putString(e.name);
    out.putString(e.skin_name.empty() ? "Standard_Custom" : e.skin_name);
    out.putString(e.skin_data);
  }
  return out.buffer();
}

inline std::string encodePlayerListRemove(const std::vector<std::array<std::uint8_t, 16>>& uuids) {
  binary::BinaryStream out;
  out.putByte(PLAYER_LIST_PACKET);
  out.putByte(PLAYER_LIST_TYPE_REMOVE);
  out.putInt(static_cast<std::int32_t>(uuids.size()));
  for (const auto& u : uuids) {
    out.put(std::string_view(reinterpret_cast<const char*>(u.data()), 16));
  }
  return out.buffer();
}

struct ContainerSetSlotDecoded {
  std::uint8_t window_id = 0;
  std::int16_t slot = 0;
  std::int16_t hotbar_slot = 0;
  item::ItemStack item;
  bool ok = false;
};

inline ContainerSetSlotDecoded decodeContainerSetSlot(std::string_view buffer) {
  ContainerSetSlotDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != CONTAINER_SET_SLOT_PACKET) return d;
    d.window_id = in.getByte();
    d.slot = static_cast<std::int16_t>(in.getShort());
    d.hotbar_slot = static_cast<std::int16_t>(in.getShort());
    d.item = item::getSlot(in);
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

// CraftingData (0xba).
// PE 0.14 Windows client is very brittle about recipe blobs when opening inventory (E):
// a non-empty / slightly-wrong shaped grid has historically crashed Win32 0.14.x.
// Default path: empty recipe list + clean=1 (keep client alive). CraftingEvent is still
// accepted server-side for client-driven crafts; fill recipes later only after Win smoke.
// Set include_stub_recipes=true to emit a tiny shaped set (Android-only experiments).
inline std::string encodeCraftingDataBasic(bool clean = true, bool include_stub_recipes = false) {
  using item::ItemStack;
  namespace ids = item::ids;

  binary::BinaryStream out;
  out.putByte(CRAFTING_DATA_PACKET);

  if (!include_stub_recipes) {
    out.putInt(0); // recipe count
    out.putByte(clean ? 1 : 0);
    return out.buffer();
  }

  // --- optional stub recipes (disabled by default; Win PE crash risk) ---
  auto write_shaped = [](binary::BinaryStream& entry, int width, int height,
                         const std::vector<ItemStack>& grid, const ItemStack& result,
                         const std::array<std::uint8_t, 16>& uuid) {
    entry.putInt(width);
    entry.putInt(height);
    // PM writes for z in width, x in height — keep same order
    for (int z = 0; z < width; ++z) {
      for (int x = 0; x < height; ++x) {
        const int i = z * height + x;
        if (i < static_cast<int>(grid.size()))
          item::putSlot(entry, grid[static_cast<std::size_t>(i)]);
        else
          item::putSlot(entry, ItemStack::air());
      }
    }
    entry.putInt(1);
    item::putSlot(entry, result);
    entry.put(std::string_view(reinterpret_cast<const char*>(uuid.data()), 16));
  };

  auto write_shapeless = [](binary::BinaryStream& entry, const std::vector<ItemStack>& ingredients,
                            const ItemStack& result, const std::array<std::uint8_t, 16>& uuid) {
    entry.putInt(static_cast<std::int32_t>(ingredients.size()));
    for (const auto& ing : ingredients) item::putSlot(entry, ing);
    entry.putInt(1);
    item::putSlot(entry, result);
    entry.put(std::string_view(reinterpret_cast<const char*>(uuid.data()), 16));
  };

  struct RecipeBlob {
    std::int32_t type;
    std::string data;
  };
  std::vector<RecipeBlob> recipes;

  auto add_shaped = [&](int w, int h, std::vector<ItemStack> grid, ItemStack result, int uuid_n) {
    binary::BinaryStream e;
    std::array<std::uint8_t, 16> uuid{};
    uuid[0] = static_cast<std::uint8_t>(uuid_n);
    uuid[15] = static_cast<std::uint8_t>(uuid_n ^ 0x5a);
    write_shaped(e, w, h, grid, result, uuid);
    recipes.push_back({CRAFT_ENTRY_SHAPED, e.buffer()});
  };
  auto add_shapeless = [&](std::vector<ItemStack> ings, ItemStack result, int uuid_n) {
    binary::BinaryStream e;
    std::array<std::uint8_t, 16> uuid{};
    uuid[0] = static_cast<std::uint8_t>(uuid_n);
    write_shapeless(e, ings, result, uuid);
    recipes.push_back({CRAFT_ENTRY_SHAPELESS, e.buffer()});
  };

  add_shaped(1, 1, {ItemStack::of(ids::LOG)}, ItemStack::of(ids::PLANKS, 4), 1);
  add_shaped(1, 2, {ItemStack::of(ids::PLANKS), ItemStack::of(ids::PLANKS)},
             ItemStack::of(ids::STICK, 4), 2);
  add_shaped(2, 2,
             {ItemStack::of(ids::PLANKS), ItemStack::of(ids::PLANKS), ItemStack::of(ids::PLANKS),
              ItemStack::of(ids::PLANKS)},
             ItemStack::of(ids::WORKBENCH), 3);
  add_shaped(1, 2, {ItemStack::of(ids::COAL), ItemStack::of(ids::STICK)},
             ItemStack::of(ids::TORCH, 4), 4);
  add_shapeless({ItemStack::of(ids::SUGARCANE)}, ItemStack::of(ids::SUGAR), 7);

  out.putInt(static_cast<std::int32_t>(recipes.size()));
  for (const auto& r : recipes) {
    out.putInt(r.type);
    out.putInt(static_cast<std::int32_t>(r.data.size()));
    out.put(r.data);
  }
  out.putByte(clean ? 1 : 0);
  return out.buffer();
}

// ---------- serverbound decoders ----------

struct LoginDecoded {
  std::string username;
  std::int32_t protocol1 = 0;
  std::int32_t protocol2 = 0;
  std::int64_t client_id = 0;
  std::array<std::uint8_t, 16> uuid{};
  std::string server_address;
  std::string client_secret;
  std::string skin_name;
  std::string skin_data;
  bool ok = false;
};

inline LoginDecoded decodeLogin(std::string_view buffer) {
  LoginDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != LOGIN_PACKET) return d;
    d.username = in.getString();
    d.protocol1 = in.getInt();
    d.protocol2 = in.getInt();
    d.client_id = in.getLong();
    if (!in.feof()) {
      auto u = in.get(16);
      for (std::size_t i = 0; i < 16 && i < u.size(); ++i)
        d.uuid[i] = static_cast<std::uint8_t>(u[i]);
    }
    if (!in.feof()) d.server_address = in.getString();
    if (!in.feof()) d.client_secret = in.getString();
    if (!in.feof()) d.skin_name = in.getString();
    if (!in.feof()) d.skin_data = in.getString();
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

struct TextDecoded {
  std::uint8_t type = 0;
  std::string source;
  std::string message;
  bool ok = false;
};

inline TextDecoded decodeText(std::string_view buffer) {
  TextDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != TEXT_PACKET) return d;
    d.type = in.getByte();
    switch (d.type) {
      case TEXT_POPUP:
      case TEXT_CHAT:
        d.source = in.getString();
        [[fallthrough]];
      case TEXT_RAW:
      case TEXT_TIP:
      case TEXT_SYSTEM:
        d.message = in.getString();
        break;
      case TEXT_TRANSLATION:
        d.message = in.getString();
        {
          auto n = in.getByte();
          for (int i = 0; i < n; ++i) (void)in.getString();
        }
        break;
      default:
        break;
    }
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

struct MoveDecoded {
  std::int64_t eid = 0;
  float x = 0, y = 0, z = 0;
  float yaw = 0, body_yaw = 0, pitch = 0;
  std::uint8_t mode = 0;
  bool on_ground = false;
  bool ok = false;
};

inline MoveDecoded decodeMovePlayer(std::string_view buffer) {
  MoveDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != MOVE_PLAYER_PACKET) return d;
    d.eid = in.getLong();
    d.x = in.getFloat();
    d.y = in.getFloat();
    d.z = in.getFloat();
    d.yaw = in.getFloat();
    d.body_yaw = in.getFloat();
    d.pitch = in.getFloat();
    d.mode = in.getByte();
    d.on_ground = in.getByte() > 0;
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

struct ChunkRadiusDecoded {
  std::int32_t radius = 0;
  bool ok = false;
};

inline ChunkRadiusDecoded decodeRequestChunkRadius(std::string_view buffer) {
  ChunkRadiusDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != REQUEST_CHUNK_RADIUS_PACKET) return d;
    d.radius = in.getInt();
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

struct PlayerActionDecoded {
  std::int64_t eid = 0;
  std::int32_t action = 0;
  std::int32_t x = 0, y = 0, z = 0;
  std::int32_t face = 0;
  bool ok = false;
};

inline PlayerActionDecoded decodePlayerAction(std::string_view buffer) {
  PlayerActionDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != PLAYER_ACTION_PACKET) return d;
    d.eid = in.getLong();
    d.action = in.getInt();
    d.x = in.getInt();
    d.y = in.getInt();
    d.z = in.getInt();
    d.face = in.getInt();
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

struct RemoveBlockDecoded {
  std::int64_t eid = 0;
  std::int32_t x = 0, y = 0, z = 0;
  bool ok = false;
};

inline RemoveBlockDecoded decodeRemoveBlock(std::string_view buffer) {
  RemoveBlockDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != REMOVE_BLOCK_PACKET) return d;
    d.eid = in.getLong();
    d.x = in.getInt();
    d.z = in.getInt();
    d.y = in.getByte();
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

struct UseItemDecoded {
  std::int32_t x = 0, y = 0, z = 0;
  std::uint8_t face = 0;
  float fx = 0, fy = 0, fz = 0;
  float pos_x = 0, pos_y = 0, pos_z = 0;
  std::int32_t slot = -1;
  item::ItemStack item;
  bool ok = false;
};

inline UseItemDecoded decodeUseItem(std::string_view buffer) {
  UseItemDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != USE_ITEM_PACKET) return d;
    d.x = in.getInt();
    d.y = in.getInt();
    d.z = in.getInt();
    d.face = in.getByte();
    d.fx = in.getFloat();
    d.fy = in.getFloat();
    d.fz = in.getFloat();
    d.pos_x = in.getFloat();
    d.pos_y = in.getFloat();
    d.pos_z = in.getFloat();
    // protocol 70+: slot + item
    if (!in.feof()) {
      d.slot = in.getInt();
      d.item = item::getSlot(in);
    }
    d.ok = true;
  } catch (...) {
    // partial decode still useful for coords
    if (d.x != 0 || d.y != 0 || d.z != 0 || d.face != 0) d.ok = true;
  }
  return d;
}

struct MobEquipmentDecoded {
  std::int64_t eid = 0;
  item::ItemStack item;
  std::uint8_t slot = 0;
  std::uint8_t selected_slot = 0;
  bool ok = false;
};

inline MobEquipmentDecoded decodeMobEquipment(std::string_view buffer) {
  MobEquipmentDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != MOB_EQUIPMENT_PACKET) return d;
    d.eid = in.getLong();
    d.item = item::getSlot(in);
    d.slot = in.getByte();
    d.selected_slot = in.getByte();
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

struct CraftingEventDecoded {
  std::uint8_t window_id = 0;
  std::int32_t type = 0;
  std::array<std::uint8_t, 16> uuid{};
  std::vector<item::ItemStack> input;
  std::vector<item::ItemStack> output;
  bool ok = false;
};

inline CraftingEventDecoded decodeCraftingEvent(std::string_view buffer) {
  CraftingEventDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != CRAFTING_EVENT_PACKET) return d;
    d.window_id = in.getByte();
    d.type = in.getInt();
    auto uuid_s = in.get(16);
    for (std::size_t i = 0; i < 16 && i < uuid_s.size(); ++i)
      d.uuid[i] = static_cast<std::uint8_t>(uuid_s[i]);
    auto in_n = in.getInt();
    for (int i = 0; i < in_n && i < 128; ++i) d.input.push_back(item::getSlot(in));
    auto out_n = in.getInt();
    for (int i = 0; i < out_n && i < 128; ++i) d.output.push_back(item::getSlot(in));
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

struct InteractDecoded {
  std::uint8_t action = 0;
  std::int64_t target = 0;
  bool ok = false;
};

inline InteractDecoded decodeInteract(std::string_view buffer) {
  InteractDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != INTERACT_PACKET) return d;
    d.action = in.getByte();
    d.target = in.getLong();
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

// DropItem 0xb4 — Q / throw held item
struct DropItemDecoded {
  std::uint8_t type = 0; // 0 = drop item
  item::ItemStack item;
  bool ok = false;
};

inline DropItemDecoded decodeDropItem(std::string_view buffer) {
  DropItemDecoded d;
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != DROP_ITEM_PACKET) return d;
    d.type = in.getByte();
    d.item = item::getSlot(in);
    d.ok = true;
  } catch (...) {
    d.ok = false;
  }
  return d;
}

// Face offset for placement (PM Block::getSide)
inline void faceOffset(std::uint8_t face, std::int32_t& x, std::int32_t& y, std::int32_t& z) {
  switch (face) {
    case 0: --y; break; // down
    case 1: ++y; break; // up
    case 2: --z; break; // north
    case 3: ++z; break; // south
    case 4: --x; break; // west
    case 5: ++x; break; // east
    default: break;
  }
}

} // namespace mpmpes::protocol
