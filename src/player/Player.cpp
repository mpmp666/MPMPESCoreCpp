#include "mpmpes/player/Player.hpp"

#include "mpmpes/binary/BinaryStream.hpp"
#include "mpmpes/protocol/Info.hpp"
#include "mpmpes/protocol/MultiProtocol.hpp"
#include "mpmpes/item/Recipes.hpp"
#include "mpmpes/protocol/Packets.hpp"
#include "mpmpes/util/Logger.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <sstream>
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
  if (payload.empty()) return;
  const int family = protocol::mp::wireFamily(p.protocol);
  auto emit = [&](std::string body) {
    std::string wire(protocol::mp::encapPrefix(family));
    wire += body;
    raklib::EncapsulatedPacket enc;
    // RELIABLE_ORDERED + channel 0 (same as PM for game packets)
    enc.reliability = 3;
    enc.order_channel = 0;
    enc.buffer = std::move(wire);
    session->addEncapsulated(std::move(enc), immediate);
  };
  if (family == protocol::mp::F014) {
    if (static_cast<std::uint8_t>(payload[0]) >= 0xec) return; // 0.16-only virtual packet
    emit(maybeBatch(std::move(payload)));
    return;
  }
  // Cross-version (PHP RakLibInterface::putPacket + MultiProtocol::translateForFamily)
  auto bufs = protocol::mp::translateOutgoing(family, payload);
  if (bufs.empty()) return;
  const bool want_batch = batch_threshold_kb_ >= 0 &&
                          static_cast<std::uint8_t>(payload[0]) != protocol::BATCH_PACKET &&
                          payload.size() >= static_cast<std::size_t>(batch_threshold_kb_) * 1024u;
  if (want_batch || bufs.size() > 1) {
    emit(protocol::mp::buildBatch(family, bufs, compression_level_));
    return;
  }
  emit(std::move(bufs[0]));
}

bool PlayerManager::savePlayer(const Player& p) const {
  if (p.username.empty() || players_data_path_.empty()) return false;
  std::error_code ec;
  fs::create_directories(players_data_path_, ec);
  const auto path = players_data_path_ + "/" + playerFileName(p.username);
  using nbt::Tag;
  auto root = Tag::Compound();
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count();
  // keep firstPlayed across saves
  {
    std::ifstream old(path, std::ios::binary);
    if (old) {
      std::stringstream ss;
      ss << old.rdbuf();
      if (auto oldNbt = nbt::readCompressed(ss.str(), nbt::Endian::Big)) {
        root->set("firstPlayed", Tag::Long(oldNbt->getInt("firstPlayed", now_ms)));
      }
    }
  }
  if (!root->has("firstPlayed")) root->set("firstPlayed", Tag::Long(now_ms));
  root->set("lastPlayed", Tag::Long(now_ms));
  {
    auto pos = Tag::List(nbt::TAG_Double);
    pos->list = {Tag::Double(p.x), Tag::Double(p.y), Tag::Double(p.z)};
    root->set("Pos", pos);
  }
  root->set("Level", Tag::String(p.level ? p.level->name() : ""));
  {
    auto inv = Tag::List(nbt::TAG_Compound);
    auto putInv = [&](const std::vector<item::ItemStack>& cont, int slot_base) {
      for (std::size_t i = 0; i < cont.size(); ++i) {
        const auto& it = cont[i];
        auto c = Tag::Compound();
        c->set("id", Tag::Short(it.id));
        c->set("Count", Tag::Byte(static_cast<std::int8_t>(it.count)));
        c->set("Damage", Tag::Short(it.damage));
        c->set("Slot", Tag::Byte(static_cast<std::int8_t>(slot_base + static_cast<int>(i))));
        c->set("TrueSlot", Tag::Byte(static_cast<std::int8_t>(it.empty() ? -1 : slot_base + static_cast<int>(i))));
        if (!it.nbt.empty()) {
          if (auto t = nbt::read(it.nbt, nbt::Endian::Little)) c->set("tag", t);
        }
        inv->list.push_back(c);
      }
    };
    // PHP: armor at slots 0-3, then main inventory at 9+ (slot index = slot+9)
    putInv(p.armor, 0);
    std::vector<item::ItemStack> main27(p.inventory.begin(), p.inventory.begin() + std::min<std::size_t>(27, p.inventory.size()));
    putInv(main27, 9);
    // hotbar links: PHP stores hotbar slots pointing at inventory indexes
    std::vector<item::ItemStack> hotbar(9, item::ItemStack::air());
    for (int i = 0; i < 9; ++i) {
      int link = p.hotbar_link[static_cast<std::size_t>(i)];
      if (link >= 0 && link < static_cast<int>(p.inventory.size())) hotbar[static_cast<std::size_t>(i)] = p.inventory[static_cast<std::size_t>(link)];
    }
    putInv(hotbar, 27);
    root->set("Inventory", inv);
  }
  root->set("Achievements", Tag::Compound());
  root->set("playerGameType", Tag::Int(p.gamemode));
  {
    auto motion = Tag::List(nbt::TAG_Double);
    motion->list = {Tag::Double(0), Tag::Double(0), Tag::Double(0)};
    root->set("Motion", motion);
    auto rot = Tag::List(nbt::TAG_Float);
    rot->list = {Tag::Float(p.yaw), Tag::Float(p.pitch)};
    root->set("Rotation", rot);
  }
  root->set("FallDistance", Tag::Float(p.fall_distance_));
  root->set("Fire", Tag::Short(static_cast<std::int16_t>(p.fire_ticks_)));
  root->set("Air", Tag::Short(300));
  root->set("OnGround", Tag::Byte(p.on_ground_ ? 1 : 0));
  root->set("Invulnerable", Tag::Byte(0));
  root->set("NameTag", Tag::String(p.username));
  root->set("Hunger", Tag::Short(static_cast<std::int16_t>(p.food)));
  root->set("Health", Tag::Short(static_cast<std::int16_t>(p.health)));
  root->set("MaxHealth", Tag::Short(20));
  root->set("Experience", Tag::Long(p.xp_total));
  root->set("ExpLevel", Tag::Long(p.xp_level));
  root->set("XpLevel", Tag::Int(p.xp_level));
  auto buf = nbt::writeGzip(*root, "", nbt::Endian::Big);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
  return static_cast<bool>(out);
}

bool PlayerManager::loadPlayer(Player& p) const {
  if (p.username.empty() || players_data_path_.empty()) return false;
  const auto path = players_data_path_ + "/" + playerFileName(p.username);
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  auto root = nbt::readCompressed(ss.str(), nbt::Endian::Big);
  if (!root || root->type != nbt::TAG_Compound) return false;
  if (auto pos = root->get("Pos"); pos && pos->list.size() == 3) {
    p.x = static_cast<float>(pos->list[0]->d);
    p.y = static_cast<float>(pos->list[1]->d);
    p.z = static_cast<float>(pos->list[2]->d);
  }
  if (auto rot = root->get("Rotation"); rot && rot->list.size() == 2) {
    p.yaw = static_cast<float>(rot->list[0]->d);
    p.pitch = static_cast<float>(rot->list[1]->d);
  }
  p.gamemode = static_cast<int>(root->getInt("playerGameType", p.gamemode));
  p.health = std::clamp(static_cast<int>(root->getInt("Health", 20)), 0, 20);
  p.food = static_cast<float>(root->getInt("Hunger", 20));
  p.xp_total = root->getInt("Experience", 0);
  p.xp_level = static_cast<int>(root->getInt("ExpLevel", root->getInt("XpLevel", 0)));
  p.fall_distance_ = static_cast<float>(root->getDouble("FallDistance", 0));
  p.fire_ticks_ = static_cast<int>(root->getInt("Fire", 0));
  p.saved_world_name = root->getString("Level");
  // Inventory: armor slots 0-3, main 9-35, hotbar 27-35 mapping back to hotbar links
  p.inventory.assign(36, item::ItemStack::air());
  p.armor.assign(4, item::ItemStack::air());
  if (auto inv = root->get("Inventory"); inv && inv->type == nbt::TAG_List) {
    for (auto& c : inv->list) {
      if (!c || c->type != nbt::TAG_Compound) continue;
      int slot = static_cast<int>(c->getInt("Slot", -1));
      item::ItemStack it;
      it.id = static_cast<std::int16_t>(c->getInt("id"));
      it.count = static_cast<std::uint8_t>(std::clamp<std::int64_t>(c->getInt("Count"), 0, 255));
      it.damage = static_cast<std::int16_t>(c->getInt("Damage"));
      if (auto tag = c->get("tag"); tag && tag->type == nbt::TAG_Compound && !tag->comp.empty())
        it.nbt = nbt::write(*tag, "", nbt::Endian::Little);
      it = item::sanitizeSlot(it);
      if (slot >= 0 && slot <= 3) {
        p.armor[static_cast<std::size_t>(slot)] = it;
      } else if (slot >= 9 && slot < 36) {
        p.inventory[static_cast<std::size_t>(slot - 9)] = it;
      } else if (slot >= 27) {
        // hotbar entry: find its true inventory index and link
        int ts = static_cast<int>(c->getInt("TrueSlot", -1));
        int hb = slot - 27;
        if (ts >= 9 && ts < 36) p.hotbar_link[static_cast<std::size_t>(hb)] = ts - 9;
        else if (!it.empty()) {
          // hotbar item not in main grid: keep as direct hotbar entry
          p.hotbar_link[static_cast<std::size_t>(hb)] = -1; // marker: direct
          // stash into a spare main slot? PHP keeps hotbar items at 27-35 too; we map hotbar to inventory[link]
        }
      }
    }
  }
  // Ensure hotbar links valid
  for (int i = 0; i < 9; ++i) {
    int& link = p.hotbar_link[static_cast<std::size_t>(i)];
    if (link >= static_cast<int>(p.inventory.size())) link = i;
  }
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
  // Keep inv/armor size + slots clean — garbage ids/counts crash PE Win when opening E UI.
  if (p.inventory.size() < 36) p.inventory.resize(36, item::ItemStack::air());
  if (p.inventory.size() > 36) p.inventory.resize(36);
  for (auto& s : p.inventory) s = item::sanitizeSlot(s);
  if (p.armor.size() < 4) p.armor.resize(4, item::ItemStack::air());
  if (p.armor.size() > 4) p.armor.resize(4);
  for (auto& s : p.armor) s = item::sanitizeSlot(s);
  for (int i = 0; i < 9; ++i) {
    int& link = p.hotbar_link[static_cast<std::size_t>(i)];
    if (link < -1 || link >= static_cast<int>(p.inventory.size())) link = i; // 0-8 default
  }
  if (p.selected_hotbar < 0 || p.selected_hotbar > 8) p.selected_hotbar = 0;

  // PM PlayerInventory::sendContents: hotbar[i] = invIndex + 9 (or -1)
  auto hotbar = p.wireHotbar();
  sendPacket(p,
             protocol::encodeContainerSetContent(protocol::WINDOW_INVENTORY, p.inventory, hotbar),
             true);
  sendPacket(p, protocol::encodeContainerSetContent(protocol::WINDOW_ARMOR, p.armor), true);
}

void PlayerManager::sendCreativeContents(Player& p) {
  auto raw = item::creativeItems();
  // Sanitize + drop air. Also strip ids known to hard-crash some PE 0.14 clients when
  // clicked from creative palette (block forms that should be item forms only).
  std::vector<item::ItemStack> items;
  items.reserve(raw.size());
  const int pr = p.protocol;
  for (auto s : raw) {
    s = item::sanitizeSlot(s);
    if (s.empty()) continue;
    // PHP Player::getCreativeItemsForClient: per-version whitelists (unknown ids crash old clients)
    if (protocol::mp::is013(pr) && !protocol::mp::isOldProtocolCreativeItem(s.id)) continue;
    if (protocol::mp::is012(pr) && !protocol::mp::is012ProtocolCreativeItem(s.id)) continue;
    if (protocol::mp::is011(pr) && !protocol::mp::is011CreativeItem(s.id, s.damage)) continue;
    // Never expose block-form hopper/comparator in creative window
    if (s.id == 154 || s.id == 149) continue;
    items.push_back(s);
  }
  sendPacket(p, protocol::encodeContainerSetContent(protocol::WINDOW_CREATIVE, items), true);
}

void PlayerManager::sendCraftingData(Player& p) {
  // Full PocketMine recipe table, filtered per wire family (PHP sendRecipeList).
  const auto& reg = item::RecipeRegistry::instance();
  const int fam = protocol::mp::wireFamily(p.protocol);
  const std::string* buf = &reg.nativeFull();
  if (fam == protocol::mp::F012) buf = &reg.native012();
  else if (fam == protocol::mp::F013) buf = &reg.nativeOld();
  sendPacket(p, *buf, true);
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
  } else {
    // Saved coords may be void / under nether ceiling after gen changes — snap if unsafe.
    const int bx = static_cast<int>(std::floor(p.x));
    const int bz = static_cast<int>(std::floor(p.z));
    const int feet = p.level->safeStandFeetY(bx, bz);
    const bool bad_y = p.y < 1.f || p.y > 126.f || (feet >= 1 && std::fabs(p.y - static_cast<float>(feet)) > 24.f);
    const bool no_floor = feet < 1 && p.y < 40.f;
    if (bad_y || no_floor) {
      if (feet >= 1) {
        p.x = static_cast<float>(bx) + 0.5f;
        p.y = static_cast<float>(feet);
        p.z = static_cast<float>(bz) + 0.5f;
      } else {
        p.x = static_cast<float>(spawn.x) + 0.5f;
        p.y = static_cast<float>(spawn.y);
        p.z = static_cast<float>(spawn.z) + 0.5f;
      }
      util::Logger::instance().notice(p.username, " unsafe saved pos; snapped to ", p.x, ",",
                                      p.y, ",", p.z, " in ", p.level->name());
    }
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

  // JWT clients (0.15/0.16) already got LOGIN_SUCCESS right after LOGIN (PHP Player LOGIN)
  if (!protocol::mp::isJwt(p.protocol))
    sendPacket(p, protocol::encodePlayStatus(protocol::PLAY_STATUS_LOGIN_SUCCESS), true);
  // 0.16 waits for the resource-pack handshake before StartGame
  if (protocol::mp::is016(p.protocol)) sendPacket(p, protocol::mp::encodeResourcePacksInfo(), true);

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
  sendPacket(p, protocol::encodeSetDifficulty(difficulty_), true);
  // Weather state sync (Genisys Weather::sendWeather on join)
  if (p.level) {
    if (p.level->raining())
      sendPacket(p, protocol::encodeLevelEvent(protocol::EVENT_START_RAIN, 0, 0, 0, 10000), true);
    if (p.level->thundering())
      sendPacket(p, protocol::encodeLevelEvent(protocol::EVENT_START_THUNDER, 0, 0, 0, 10000), true);
  }
  sendPacket(p, protocol::encodeSetPlayerGameType(p.gamemode), true);

  std::int32_t flags = 0x40; // auto_jump
  if (p.gamemode == 1) flags |= 0x80; // allow_fly
  sendPacket(p, protocol::encodeAdventureSettings(flags), true);

  sendCraftingData(p);
  // PM setGamemode: creative palette before inventory contents
  if (p.gamemode == 1) sendCreativeContents(p);
  sendInventory(p);

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
