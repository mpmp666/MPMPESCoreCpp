#include "mpmpes/player/Player.hpp"

#include "mpmpes/binary/BinaryStream.hpp"
#include "mpmpes/protocol/Info.hpp"
#include "mpmpes/protocol/Packets.hpp"
#include "mpmpes/util/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <zlib.h>

namespace fs = std::filesystem;

namespace mpmpes::player {
namespace {
std::string keyOf(const raklib::Endpoint& ep) {
  return ep.address + ":" + std::to_string(ep.port);
}

// Safe filename from username (alnum + _ -)
std::string playerFileName(const std::string& username) {
  std::string s;
  s.reserve(username.size());
  for (unsigned char c : username) {
    if (std::isalnum(c) || c == '_' || c == '-' || c == '.') s.push_back(static_cast<char>(c));
    else s.push_back('_');
  }
  if (s.empty()) s = "player";
  return s + ".dat";
}

std::string deflateZlib(std::string_view raw, int level) {
  if (raw.empty()) return {};
  if (level < 0) level = 0;
  if (level > 9) level = 9;
  uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::string out;
  out.resize(bound);
  uLongf out_len = bound;
  // zlib wrapper (same as PHP ZLIB_ENCODING_DEFLATE / zlib_encode)
  const int rc =
      compress2(reinterpret_cast<Bytef*>(out.data()), &out_len,
                reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), level);
  if (rc != Z_OK) return {};
  out.resize(out_len);
  return out;
}
} // namespace

PlayerManager::PlayerManager(raklib::SessionManager& sessions) : sessions_(sessions) {}

Player* PlayerManager::get(const raklib::Endpoint& ep) {
  auto it = players_.find(keyOf(ep));
  if (it == players_.end()) return nullptr;
  return &it->second;
}

Player& PlayerManager::getOrCreate(const raklib::Endpoint& ep, std::int64_t client_id) {
  auto k = keyOf(ep);
  auto it = players_.find(k);
  if (it != players_.end()) {
    it->second.client_id = client_id;
    return it->second;
  }
  Player p;
  p.endpoint = ep;
  p.client_id = client_id;
  p.state = PlayerState::Connected;
  p.inventory = item::starterInventory(true);
  p.armor.assign(4, item::ItemStack::air());
  auto [ins, _] = players_.emplace(k, std::move(p));
  return ins->second;
}

void PlayerManager::remove(const raklib::Endpoint& ep) {
  auto* pl = get(ep);
  if (pl && pl->spawned && !pl->username.empty()) {
    savePlayer(*pl);
    broadcastPlayerListRemove(*pl);
  }
  players_.erase(keyOf(ep));
}

std::string PlayerManager::maybeBatch(std::string payload) const {
  if (batch_threshold_kb_ < 0) return payload; // disabled
  if (payload.empty()) return payload;
  // Already a Batch — don't nest
  if (static_cast<std::uint8_t>(payload[0]) == protocol::BATCH_PACKET) return payload;
  const std::size_t threshold_bytes =
      (batch_threshold_kb_ == 0)
          ? 0
          : static_cast<std::size_t>(batch_threshold_kb_) * 1024u;
  if (payload.size() < threshold_bytes) return payload;
  // PM: zlib_encode(writeInt(len) . buffer)
  binary::BinaryStream framed;
  framed.putInt(static_cast<std::int32_t>(payload.size()));
  framed.put(payload);
  auto z = deflateZlib(framed.buffer(), compression_level_);
  if (z.empty()) return payload; // fallback raw
  return protocol::encodeBatch(z);
}

void PlayerManager::sendPacket(Player& p, std::string payload, bool immediate) {
  auto* session = sessions_.getSession(p.endpoint);
  if (!session || session->closed()) return;
  payload = maybeBatch(std::move(payload));
  // 0.14 wire: chr(0x8e) . packet->buffer (RakLibInterface.php)
  std::string wire;
  wire.reserve(payload.size() + 1);
  wire.push_back(static_cast<char>(protocol::MCPE_RAKNET_CUSTOM_PACKET_ID));
  wire += payload;
  raklib::EncapsulatedPacket enc;
  // RELIABLE_ORDERED + channel 0 (same as PM for game packets)
  enc.reliability = 3;
  enc.order_channel = 0;
  enc.buffer = std::move(wire);
  session->addEncapsulated(std::move(enc), immediate);
}

bool PlayerManager::savePlayer(const Player& p) const {
  if (p.username.empty() || players_data_path_.empty()) return false;
  std::error_code ec;
  fs::create_directories(players_data_path_, ec);
  const auto path = players_data_path_ + "/" + playerFileName(p.username);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write("PLR1", 4);
  // username
  std::uint16_t nlen = static_cast<std::uint16_t>(std::min<std::size_t>(p.username.size(), 65535));
  out.write(reinterpret_cast<const char*>(&nlen), 2);
  out.write(p.username.data(), nlen);
  // world name
  std::string wname = p.level ? p.level->name() : "";
  std::uint16_t wlen = static_cast<std::uint16_t>(std::min<std::size_t>(wname.size(), 65535));
  out.write(reinterpret_cast<const char*>(&wlen), 2);
  out.write(wname.data(), wlen);
  out.write(reinterpret_cast<const char*>(&p.x), 4);
  out.write(reinterpret_cast<const char*>(&p.y), 4);
  out.write(reinterpret_cast<const char*>(&p.z), 4);
  out.write(reinterpret_cast<const char*>(&p.yaw), 4);
  out.write(reinterpret_cast<const char*>(&p.pitch), 4);
  std::int32_t gm = p.gamemode;
  std::int32_t hp = p.health;
  out.write(reinterpret_cast<const char*>(&gm), 4);
  out.write(reinterpret_cast<const char*>(&hp), 4);
  out.write(reinterpret_cast<const char*>(&p.selected_hotbar), 4);
  // hotbar links
  for (int i = 0; i < 9; ++i) {
    std::int32_t h = p.hotbar_link[static_cast<std::size_t>(i)];
    out.write(reinterpret_cast<const char*>(&h), 4);
  }
  // inventory 36
  std::uint16_t invn = static_cast<std::uint16_t>(p.inventory.size());
  out.write(reinterpret_cast<const char*>(&invn), 2);
  for (const auto& s : p.inventory) {
    std::int16_t id = s.id;
    std::uint8_t cnt = s.count;
    std::int16_t dmg = s.damage;
    out.write(reinterpret_cast<const char*>(&id), 2);
    out.write(reinterpret_cast<const char*>(&cnt), 1);
    out.write(reinterpret_cast<const char*>(&dmg), 2);
  }
  // armor 4
  std::uint16_t arn = static_cast<std::uint16_t>(p.armor.size());
  out.write(reinterpret_cast<const char*>(&arn), 2);
  for (const auto& s : p.armor) {
    std::int16_t id = s.id;
    std::uint8_t cnt = s.count;
    std::int16_t dmg = s.damage;
    out.write(reinterpret_cast<const char*>(&id), 2);
    out.write(reinterpret_cast<const char*>(&cnt), 1);
    out.write(reinterpret_cast<const char*>(&dmg), 2);
  }
  return static_cast<bool>(out);
}

bool PlayerManager::loadPlayer(Player& p) const {
  if (p.username.empty() || players_data_path_.empty()) return false;
  const auto path = players_data_path_ + "/" + playerFileName(p.username);
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[4]{};
  in.read(magic, 4);
  if (std::memcmp(magic, "PLR1", 4) != 0) return false;
  std::uint16_t nlen = 0;
  in.read(reinterpret_cast<char*>(&nlen), 2);
  std::string uname(nlen, '\0');
  in.read(uname.data(), nlen);
  std::uint16_t wlen = 0;
  in.read(reinterpret_cast<char*>(&wlen), 2);
  std::string wname(wlen, '\0');
  in.read(wname.data(), wlen);
  in.read(reinterpret_cast<char*>(&p.x), 4);
  in.read(reinterpret_cast<char*>(&p.y), 4);
  in.read(reinterpret_cast<char*>(&p.z), 4);
  in.read(reinterpret_cast<char*>(&p.yaw), 4);
  in.read(reinterpret_cast<char*>(&p.pitch), 4);
  std::int32_t gm = 0, hp = 20;
  in.read(reinterpret_cast<char*>(&gm), 4);
  in.read(reinterpret_cast<char*>(&hp), 4);
  p.gamemode = gm;
  p.health = hp;
  in.read(reinterpret_cast<char*>(&p.selected_hotbar), 4);
  for (int i = 0; i < 9; ++i) {
    std::int32_t h = 0;
    in.read(reinterpret_cast<char*>(&h), 4);
    p.hotbar_link[static_cast<std::size_t>(i)] = h;
  }
  std::uint16_t invn = 0;
  in.read(reinterpret_cast<char*>(&invn), 2);
  p.inventory.assign(36, item::ItemStack::air());
  for (std::uint16_t i = 0; i < invn && i < 36; ++i) {
    std::int16_t id = 0;
    std::uint8_t cnt = 0;
    std::int16_t dmg = 0;
    in.read(reinterpret_cast<char*>(&id), 2);
    in.read(reinterpret_cast<char*>(&cnt), 1);
    in.read(reinterpret_cast<char*>(&dmg), 2);
    p.inventory[i] = item::ItemStack::of(id, cnt, dmg);
  }
  std::uint16_t arn = 0;
  in.read(reinterpret_cast<char*>(&arn), 2);
  p.armor.assign(4, item::ItemStack::air());
  for (std::uint16_t i = 0; i < arn && i < 4; ++i) {
    std::int16_t id = 0;
    std::uint8_t cnt = 0;
    std::int16_t dmg = 0;
    in.read(reinterpret_cast<char*>(&id), 2);
    in.read(reinterpret_cast<char*>(&cnt), 1);
    in.read(reinterpret_cast<char*>(&dmg), 2);
    p.armor[i] = item::ItemStack::of(id, cnt, dmg);
  }
  p.saved_world_name = wname;
  p.has_saved_data = true;
  return true;
}

void PlayerManager::saveAllPlayers() const {
  int n = 0;
  for (const auto& [_, p] : players_) {
    if (p.spawned && !p.username.empty() && savePlayer(p)) ++n;
  }
  if (n > 0) util::Logger::instance().info("Saved ", n, " players");
}

void PlayerManager::broadcastPacket(const std::string& payload, level::Level* level,
                                    const Player* except) {
  for (auto& [_, p] : players_) {
    if (p.state != PlayerState::Playing || !p.spawned) continue;
    if (except && p.key() == except->key()) continue;
    if (level && p.level != level) continue;
    sendPacket(p, payload);
  }
}

void PlayerManager::broadcastNear(float x, float y, float z, float radius,
                                  const std::string& payload, level::Level* level,
                                  const Player* except) {
  const float r2 = radius * radius;
  for (auto& [_, p] : players_) {
    if (p.state != PlayerState::Playing || !p.spawned) continue;
    if (except && p.key() == except->key()) continue;
    if (level && p.level != level) continue;
    const float dx = p.x - x, dy = p.y - y, dz = p.z - z;
    if (dx * dx + dy * dy + dz * dz > r2) continue;
    sendPacket(p, payload);
  }
}

void PlayerManager::sendChunksAround(Player& p, int radius_override) {
  if (!p.level) return;
  const int radius = radius_override > 0 ? radius_override : p.chunk_radius;
  const int pcx = static_cast<int>(p.x) >> 4;
  const int pcz = static_cast<int>(p.z) >> 4;
  int sent = 0;
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      if (dx * dx + dz * dz > radius * radius + radius) continue;
      const int cx = pcx + dx;
      const int cz = pcz + dz;
      if (p.sent_chunks.count({cx, cz})) continue;
      // v0.4.16: tiles embedded in FullChunkData payload (PM ChunkRequestTask parity).
      auto payload = p.level->chunkNetworkPayload(cx, cz);
      auto pk = protocol::encodeFullChunkData(cx, cz, payload);
      sendPacket(p, std::move(pk), false);
      // Also spawnTo-style BED after chunk (PM Spawnable) — reliable ordered after chunk.
      for (const auto& chest : p.level->chestsInChunk(cx, cz)) {
        if (p.level->getBlockId(chest.x, chest.y, chest.z) != protocol::BLOCK_CHEST) continue;
        const bool paired =
            chest.isPaired() &&
            p.level->getBlockId(chest.pair_x, chest.y, chest.pair_z) == protocol::BLOCK_CHEST;
        sendPacket(p,
                   protocol::encodeBlockEntityData(
                       chest.x, chest.y, chest.z,
                       protocol::encodeChestSpawnNbt(chest.x, chest.y, chest.z, paired,
                                                     paired ? chest.pair_x : 0,
                                                     paired ? chest.pair_z : 0)),
                   true);
      }
      p.sent_chunks.insert({cx, cz});
      ++sent;
      if (sent >= 16) return;
    }
  }
}

void PlayerManager::sendInventory(Player& p) {
  // PM PlayerInventory::sendContents: hotbar[i] = invIndex + 9 (or -1)
  auto hotbar = p.wireHotbar();
  sendPacket(p,
             protocol::encodeContainerSetContent(protocol::WINDOW_INVENTORY, p.inventory, hotbar),
             true);
  sendPacket(p, protocol::encodeContainerSetContent(protocol::WINDOW_ARMOR, p.armor), true);
}

void PlayerManager::sendCreativeContents(Player& p) {
  auto items = item::creativeItems();
  sendPacket(p, protocol::encodeContainerSetContent(protocol::WINDOW_CREATIVE, items), true);
}

void PlayerManager::sendCraftingData(Player& p) {
  sendPacket(p, protocol::encodeCraftingDataBasic(true), true);
}

void PlayerManager::sendPlayerListTo(Player& p) {
  std::vector<protocol::PlayerListEntry> entries;
  entries.reserve(players_.size());
  for (auto& [_, other] : players_) {
    if (!other.spawned || other.username.empty()) continue;
    protocol::PlayerListEntry e;
    e.uuid = other.uuid;
    e.eid = other.entity_id;
    e.name = other.username;
    e.skin_name = other.skin_name.empty() ? "Standard_Custom" : other.skin_name;
    e.skin_data = other.skin_data;
    entries.push_back(std::move(e));
  }
  if (entries.empty()) return;
  sendPacket(p, protocol::encodePlayerListAdd(entries), true);
}

void PlayerManager::broadcastPlayerListAdd(const Player& joined) {
  if (joined.username.empty()) return;
  protocol::PlayerListEntry e;
  e.uuid = joined.uuid;
  e.eid = joined.entity_id;
  e.name = joined.username;
  e.skin_name = joined.skin_name.empty() ? "Standard_Custom" : joined.skin_name;
  e.skin_data = joined.skin_data;
  auto pk = protocol::encodePlayerListAdd({e});
  for (auto& [_, p] : players_) {
    if (p.state != PlayerState::Playing || !p.spawned) continue;
    // send to everyone including self (pause menu needs self entry)
    sendPacket(p, pk, true);
  }
}

void PlayerManager::broadcastPlayerListRemove(const Player& left) {
  auto pk = protocol::encodePlayerListRemove({left.uuid});
  for (auto& [_, p] : players_) {
    if (p.state != PlayerState::Playing || !p.spawned) continue;
    if (p.key() == left.key()) continue;
    sendPacket(p, pk, true);
  }
}

void PlayerManager::doLoginSequence(Player& p) {
  // Prefer already-loaded data (Server loads before StartGame for world resolve)
  const bool loaded = p.has_saved_data || loadPlayer(p);

  if (!p.level) {
    p.level = default_level_;
  }
  if (!p.level) {
    util::Logger::instance().error("Login failed: no default level for ", p.username);
    sendPacket(p, protocol::encodeDisconnect("No world loaded"), true);
    return;
  }

  const auto spawn = p.level->spawn();
  if (!loaded || !p.has_saved_data) {
    p.x = static_cast<float>(spawn.x) + 0.5f;
    p.y = static_cast<float>(spawn.y);
    p.z = static_cast<float>(spawn.z) + 0.5f;
  }
  if (!loaded && p.gamemode < 0) p.gamemode = p.level->settings().gamemode;
  p.state = PlayerState::LoggingIn;
  if (p.entity_id == 0) p.entity_id = nextEntityId();
  if (!loaded) {
    for (int i = 0; i < 9; ++i) p.hotbar_link[static_cast<std::size_t>(i)] = i;
    p.inventory = item::starterInventory(p.gamemode == 1);
  }
  if (p.armor.empty()) p.armor.assign(4, item::ItemStack::air());
  if (p.inventory.size() < 36) p.inventory.resize(36, item::ItemStack::air());
  if (p.skin_name.empty()) p.skin_name = "Standard_Custom";
  // zero uuid → derive from client_id + name so list entries are stable
  bool uuid_zero = true;
  for (auto b : p.uuid)
    if (b != 0) {
      uuid_zero = false;
      break;
    }
  if (uuid_zero) {
    // simple deterministic 16-byte id (not crypto UUID)
    std::uint64_t a = static_cast<std::uint64_t>(p.client_id);
    std::uint64_t b = 0;
    for (unsigned char c : p.username) b = b * 131 + c;
    for (int i = 0; i < 8; ++i) p.uuid[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((a >> (i * 8)) & 0xff);
    for (int i = 0; i < 8; ++i)
      p.uuid[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>((b >> (i * 8)) & 0xff);
  }

  sendPacket(p, protocol::encodePlayStatus(protocol::PLAY_STATUS_LOGIN_SUCCESS), true);

  // Client self-view eid is 0 in StartGame (PM convention)
  // PE 0.14: only dim 0/1 on wire — End (ender) reports OVERWORLD via protocolDimension()
  sendPacket(p,
             protocol::encodeStartGame(p.level->settings().seed, p.level->protocolDimension(),
                                       p.level->generatorIdForStartGame(), p.gamemode & 0x01,
                                       /*eid*/ 0, spawn.x, spawn.y, spawn.z, p.x, p.y, p.z),
             true);

  sendPacket(p, protocol::encodeSetTime(p.level->time(), true), true);
  sendPacket(p, protocol::encodeSetSpawnPosition(spawn.x, spawn.y, spawn.z), true);
  sendPacket(p, protocol::encodeSetHealth(p.health), true);
  sendPacket(p, protocol::encodeSetDifficulty(1), true);
  sendPacket(p, protocol::encodeSetPlayerGameType(p.gamemode), true);

  std::int32_t flags = 0x40; // auto_jump
  if (p.gamemode == 1) flags |= 0x80; // allow_fly
  sendPacket(p, protocol::encodeAdventureSettings(flags), true);

  sendCraftingData(p);
  sendInventory(p);
  if (p.gamemode == 1) sendCreativeContents(p);

  // Full list for joiner (includes self once we mark spawned below)
  // Send list with self first so pause menu is not empty even solo
  {
    protocol::PlayerListEntry self;
    self.uuid = p.uuid;
    self.eid = p.entity_id;
    self.name = p.username;
    self.skin_name = p.skin_name;
    self.skin_data = p.skin_data;
    sendPacket(p, protocol::encodePlayerListAdd({self}), true);
    // others already online
    std::vector<protocol::PlayerListEntry> others;
    for (auto& [_, o] : players_) {
      if (!o.spawned || o.username.empty() || o.key() == p.key()) continue;
      protocol::PlayerListEntry e;
      e.uuid = o.uuid;
      e.eid = o.entity_id;
      e.name = o.username;
      e.skin_name = o.skin_name.empty() ? "Standard_Custom" : o.skin_name;
      e.skin_data = o.skin_data;
      others.push_back(std::move(e));
    }
    if (!others.empty()) sendPacket(p, protocol::encodePlayerListAdd(others), true);
  }

  p.chunk_radius = 4;
  sendChunksAround(p, 3);
  sendPacket(p, protocol::encodePlayStatus(protocol::PLAY_STATUS_PLAYER_SPAWN), true);
  sendPacket(p, protocol::encodeChunkRadiusUpdate(p.chunk_radius), true);
  // v0.4.17: no welcome spam — only green/red join/leave broadcast from Server

  p.spawned = true;
  p.state = PlayerState::Playing;

  // Tell everyone else about this player (and refresh self list entry)
  broadcastPlayerListAdd(p);

  util::Logger::instance().notice(p.username, " logged in at ", p.level->name(), " ", p.x, ",",
                                  p.y, ",", p.z, " gm=", p.gamemode,
                                  " chunks=", p.sent_chunks.size(), " eid=", p.entity_id);
}

void PlayerManager::broadcastText(std::string_view message) {
  auto pk = protocol::encodeTextSystem(message);
  for (auto& [_, p] : players_) {
    if (p.state == PlayerState::Playing) sendPacket(p, pk);
  }
}

void PlayerManager::broadcastChat(std::string_view source, std::string_view message) {
  auto pk = protocol::encodeTextChat(source, message);
  for (auto& [_, p] : players_) {
    if (p.state == PlayerState::Playing) sendPacket(p, pk);
  }
}

} // namespace mpmpes::player
