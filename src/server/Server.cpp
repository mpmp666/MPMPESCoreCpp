#include "mpmpes/server/Server.hpp"

#include "mpmpes/item/Item.hpp"
#include "mpmpes/protocol/Info.hpp"
#include "mpmpes/protocol/Packets.hpp"
#include "mpmpes/util/Logger.hpp"

#include <algorithm>
#include <functional>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <vector>
#include <zlib.h>

namespace mpmpes::server {
namespace {
std::atomic<Server*> g_server{nullptr};

void onSignal(int) {
  if (auto* s = g_server.load()) s->stop();
}
} // namespace

Server::Server(ServerConfig cfg) : cfg_(std::move(cfg)) {
  std::mt19937_64 rng{std::random_device{}()};
  server_id_ = static_cast<std::int64_t>(rng());
}

Server::~Server() {
  stop();
  if (network_thread_.joinable()) network_thread_.join();
}

std::string Server::buildRaklibName() const {
  const int online = players_ ? static_cast<int>(players_->count()) : 0;
  return "MCPE;" + cfg_.motd + ";" + std::to_string(cfg_.protocol) + ";" + cfg_.version_name +
         ";" + std::to_string(online) + ";" + std::to_string(cfg_.max_players) + ";" +
         std::to_string(server_id_);
}

void Server::broadcastBlockUpdate(level::Level* level, int x, int y, int z, std::uint8_t id,
                                  std::uint8_t meta, player::Player* except) {
  if (!players_ || !level) return;
  auto pk = protocol::encodeUpdateBlock(x, y, z, id, meta, protocol::UPDATE_FLAG_ALL);
  players_->broadcastNear(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                          static_cast<float>(z) + 0.5f, 128.f, pk, level, except);
}

void Server::breakBlock(player::Player& p, int x, int y, int z) {
  if (!p.level || !p.spawned) return;
  if (y < 0 || y >= 128) return;

  auto id = p.level->getBlockId(x, y, z);
  auto meta = p.level->getBlockMeta(x, y, z);
  if (id == 0) return;
  // Bedrock: silent reject (do NOT TextSystem — client dig spam flooded chat).
  // Creative may break bedrock except floor y=0.
  if (id == protocol::BLOCK_BEDROCK) {
    if (p.gamemode != 1 || y == 0) return;
  }

  if (!p.level->setBlock(x, y, z, 0, 0)) return;

  // destroy particles
  auto particle = protocol::encodeDestroyBlockParticle(static_cast<float>(x) + 0.5f,
                                                       static_cast<float>(y) + 0.5f,
                                                       static_cast<float>(z) + 0.5f, id, meta);
  players_->broadcastNear(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                          static_cast<float>(z) + 0.5f, 64.f, particle, p.level, nullptr);

  broadcastBlockUpdate(p.level, x, y, z, 0, 0, nullptr);

  // close anyone viewing this container, drop tile contents
  for (auto& [_, pl] : players_->all()) {
    if (pl.open_window_id != 0 && pl.open_chest_x == x && pl.open_chest_y == y &&
        pl.open_chest_z == z) {
      closeContainer(pl, true);
    }
  }
  if (id == protocol::BLOCK_CHEST) {
    int partner_x = static_cast<int>(0x80000000), partner_z = static_cast<int>(0x80000000);
    if (auto* chest = p.level->getChest(x, y, z)) {
      if (chest->isPaired()) {
        partner_x = chest->pair_x;
        partner_z = chest->pair_z;
      }
      std::mt19937 rng{std::random_device{}()};
      std::uniform_real_distribution<float> j(-0.15f, 0.15f);
      for (auto& slot : chest->slots) {
        if (slot.empty()) continue;
        dropItemInWorld(p.level, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                        static_cast<float>(z) + 0.5f, slot, j(rng), 0.25f, j(rng), 0);
      }
      p.level->removeChest(x, y, z); // unpairs partner
    }
    broadcastChestLid(p.level, x, y, z, false);
    // remaining half becomes single — refresh BED without pair tags
    if (partner_x != static_cast<int>(0x80000000)) {
      auto bed = protocol::encodeBlockEntityData(
          partner_x, y, partner_z,
          protocol::encodeChestSpawnNbt(partner_x, y, partner_z, false, 0, 0));
      players_->broadcastNear(static_cast<float>(partner_x) + 0.5f, static_cast<float>(y) + 0.5f,
                              static_cast<float>(partner_z) + 0.5f, 64.f, bed, p.level, nullptr);
    }
  } else if (id == protocol::BLOCK_FURNACE || id == protocol::BLOCK_BURNING_FURNACE) {
    if (auto* fur = p.level->getFurnace(x, y, z)) {
      std::mt19937 rng{std::random_device{}()};
      std::uniform_real_distribution<float> j(-0.15f, 0.15f);
      for (auto& slot : fur->slots) {
        if (slot.empty()) continue;
        dropItemInWorld(p.level, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                        static_cast<float>(z) + 0.5f, slot, j(rng), 0.25f, j(rng), 0);
      }
      p.level->removeFurnace(x, y, z);
    }
  }

  // Drop into world: survival always; creative when always_drop_on_break
  // pickup_delay=0 so drops are immediately pickable (PM default 10 was too sticky for tests)
  if (p.gamemode != 1 || cfg_.always_drop_on_break) {
    auto drop = item::breakDrop(id, meta);
    if (!drop.empty()) {
      std::mt19937 rng{std::random_device{}()};
      std::uniform_real_distribution<float> j(-0.1f, 0.1f);
      dropItemInWorld(p.level, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                      static_cast<float>(z) + 0.5f, drop, j(rng), 0.2f, j(rng), 0);
    }
  }

  // Survival tool durability: dig costs 1 (PM Tool::useOn type=1)
  if (p.gamemode != 1) {
    auto& held = p.heldItem();
    if (item::isToolItem(held.id) && item::toolKind(held.id) != item::ToolKind::Bow) {
      if (item::applyDurability(held, 1)) players_->sendInventory(p);
    }
  }

  plugin::BlockEvent bev;
  bev.username = p.username;
  bev.x = x;
  bev.y = y;
  bev.z = z;
  bev.action = protocol::ACTION_STOP_BREAK;
  bev.face = 0;
  plugins_.fireBlock(bev);
  // no per-break info log (was making dig feel laggy on s390x)
}

void Server::placeBlock(player::Player& p, int x, int y, int z, std::uint8_t face,
                        const item::ItemStack& held) {
  if (!p.level || !p.spawned) return;
  if (face > 5) return;

  std::int32_t px = x, py = y, pz = z;
  protocol::faceOffset(face, px, py, pz);
  if (py < 0 || py >= 128) return;

  // replace air only
  if (p.level->getBlockId(px, py, pz) != 0) return;

  std::uint8_t block_id = item::itemToBlockId(held.id);
  if (block_id == 0) {
    // spawn egg?
    if (held.id == item::ids::SPAWN_EGG) {
      auto kind = entity::kindFromSpawnMeta(held.damage);
      auto& e = entities_.spawn(kind, p.level, static_cast<float>(px) + 0.5f,
                                static_cast<float>(py) + 0.1f, static_cast<float>(pz) + 0.5f);
      // show to nearby players
      for (auto& [_, pl] : players_->all()) {
        if (pl.level == p.level && pl.spawned) spawnEntityToPlayer(pl, e);
      }
      // no chat on spawn-egg place (was noisy; client already sees the entity)
      if (p.gamemode != 1) {
        auto& h = p.heldItem();
        if (h.count > 1) --h.count;
        else h = item::ItemStack::air();
        players_->sendInventory(p);
      }
      return;
    }
    return;
  }

  // v0.4.14: refuse placing furnace / burning furnace (feature removed)
  if (block_id == protocol::BLOCK_FURNACE || block_id == protocol::BLOCK_BURNING_FURNACE) {
    players_->sendPacket(p, protocol::encodeTextSystem("Furnace is disabled on this server"), false);
    return;
  }

  // Chest faces player horizontal look (PM faces[] from getDirection)
  std::uint8_t meta = static_cast<std::uint8_t>(held.damage & 0x0f);
  if (block_id == protocol::BLOCK_CHEST) {
    meta = protocol::horizontalFaceMeta(p.yaw);
  }
  if (!p.level->setBlock(px, py, pz, block_id, meta)) return;

  if (block_id == protocol::BLOCK_CHEST) {
    // PM Chest::place — pair with adjacent same-facing unpaired chest
    auto& self = p.level->getOrCreateChest(px, py, pz);
    // scan sides 2..5; skip axis matching our facing (PM)
    // meta 2/3 face N/S → only pair on E/W (sides 4/5); meta 4/5 face W/E → only N/S (2/3)
    // block side order in PM: 0D 1U 2N 3S 4W 5E
    static constexpr int sdx[6] = {0, 0, 0, 0, -1, 1};
    static constexpr int sdz[6] = {0, 0, -1, 1, 0, 0};
    int pair_found_x = static_cast<int>(0x80000000);
    int pair_found_z = static_cast<int>(0x80000000);
    for (int side = 2; side <= 5; ++side) {
      if ((meta == 4 || meta == 5) && (side == 4 || side == 5)) continue;
      if ((meta == 3 || meta == 2) && (side == 2 || side == 3)) continue;
      const int nx = px + sdx[side];
      const int nz = pz + sdz[side];
      if (p.level->getBlockId(nx, py, nz) != protocol::BLOCK_CHEST) continue;
      if (p.level->getBlockMeta(nx, py, nz) != meta) continue;
      auto* other = p.level->getChest(nx, py, nz);
      if (!other) other = &p.level->getOrCreateChest(nx, py, nz);
      if (other->isPaired()) continue;
      // pair both ways
      self.pair_x = nx;
      self.pair_z = nz;
      self.dirty = true;
      other->pair_x = px;
      other->pair_z = pz;
      other->dirty = true;
      pair_found_x = nx;
      pair_found_z = nz;
      break;
    }
    broadcastBlockUpdate(p.level, px, py, pz, block_id, meta, nullptr);
    // spawn tile to nearby players (fixes ghost until open / rejoin)
    auto bed_self = protocol::encodeBlockEntityData(
        px, py, pz,
        protocol::encodeChestSpawnNbt(px, py, pz, self.isPaired(), self.pair_x, self.pair_z));
    players_->broadcastNear(static_cast<float>(px) + 0.5f, static_cast<float>(py) + 0.5f,
                            static_cast<float>(pz) + 0.5f, 64.f, bed_self, p.level, nullptr);
    if (pair_found_x != static_cast<int>(0x80000000)) {
      auto* other = p.level->getChest(pair_found_x, py, pair_found_z);
      if (other) {
        auto bed_o = protocol::encodeBlockEntityData(
            pair_found_x, py, pair_found_z,
            protocol::encodeChestSpawnNbt(pair_found_x, py, pair_found_z, true, other->pair_x,
                                          other->pair_z));
        players_->broadcastNear(static_cast<float>(pair_found_x) + 0.5f,
                                static_cast<float>(py) + 0.5f,
                                static_cast<float>(pair_found_z) + 0.5f, 64.f, bed_o, p.level,
                                nullptr);
      }
    }
  } else {
    broadcastBlockUpdate(p.level, px, py, pz, block_id, meta, nullptr);
  }

  if (p.gamemode != 1) {
    auto& h = p.heldItem();
    if (h.count > 1) --h.count;
    else h = item::ItemStack::air();
    players_->sendInventory(p);
  }

  plugin::BlockEvent bev;
  bev.username = p.username;
  bev.x = px;
  bev.y = py;
  bev.z = pz;
  bev.action = 100; // place
  bev.face = face;
  plugins_.fireBlock(bev);
  // skip place info log (dig/place latency)
}

// Try light a nether portal from obsidian frame at (ox,oy,oz) — PM FlintSteel subset (X or Z axis).
// Returns true if portal blocks were placed.
bool Server::tryLightNetherPortal(player::Player& p, int ox, int oy, int oz) {
  if (!p.level) return false;
  auto& lvl = *p.level;
  if (lvl.getBlockId(ox, oy, oz) != protocol::BLOCK_OBSIDIAN) return false;

  auto isObs = [&](int x, int y, int z) {
    return lvl.getBlockId(x, y, z) == protocol::BLOCK_OBSIDIAN;
  };

  // --- X-axis frame (portal plane = X/Y, fixed Z) ---
  {
    int x_max = ox, x_min = ox;
    while (isObs(x_max + 1, oy, oz)) ++x_max;
    while (isObs(x_min - 1, oy, oz)) --x_min;
    const int count_x = x_max - x_min + 1;
    if (count_x >= 4 && count_x <= 23) {
      int x_max_y = oy, x_min_y = oy;
      while (isObs(x_max, x_max_y + 1, oz)) ++x_max_y;
      while (isObs(x_min, x_min_y + 1, oz)) ++x_min_y;
      // top of frame is last continuous obsidian; portal fill is below that lintel
      const int y_max = std::min(x_max_y, x_min_y);
      const int count_y = y_max - oy + 1; // vertical span of side columns from base
      if (count_y >= 5 && count_y <= 23) {
        int count_up = 0;
        for (int ux = x_min; ux <= x_max && isObs(ux, y_max, oz); ++ux) ++count_up;
        if (count_up == count_x) {
          // fill interior air with portal (meta 1 = X axis in classic PE)
          for (int px = x_min + 1; px < x_max; ++px) {
            for (int py = oy + 1; py < y_max; ++py) {
              if (lvl.getBlockId(px, py, oz) != 0) continue;
              lvl.setBlock(px, py, oz, protocol::BLOCK_PORTAL, 1);
              broadcastBlockUpdate(&lvl, px, py, oz, protocol::BLOCK_PORTAL, 1, nullptr);
            }
          }
          return true;
        }
      }
    }
  }

  // --- Z-axis frame (portal plane = Z/Y, fixed X) ---
  {
    int z_max = oz, z_min = oz;
    while (isObs(ox, oy, z_max + 1)) ++z_max;
    while (isObs(ox, oy, z_min - 1)) --z_min;
    const int count_z = z_max - z_min + 1;
    if (count_z >= 4 && count_z <= 23) {
      int z_max_y = oy, z_min_y = oy;
      while (isObs(ox, z_max_y + 1, z_max)) ++z_max_y;
      while (isObs(ox, z_min_y + 1, z_min)) ++z_min_y;
      const int y_max = std::min(z_max_y, z_min_y);
      const int count_y = y_max - oy + 1;
      if (count_y >= 5 && count_y <= 23) {
        int count_up = 0;
        for (int uz = z_min; uz <= z_max && isObs(ox, y_max, uz); ++uz) ++count_up;
        if (count_up == count_z) {
          for (int pz = z_min + 1; pz < z_max; ++pz) {
            for (int py = oy + 1; py < y_max; ++py) {
              if (lvl.getBlockId(ox, py, pz) != 0) continue;
              lvl.setBlock(ox, py, pz, protocol::BLOCK_PORTAL, 2);
              broadcastBlockUpdate(&lvl, ox, py, pz, protocol::BLOCK_PORTAL, 2, nullptr);
            }
          }
          return true;
        }
      }
    }
  }
  return false;
}

void Server::changePlayerWorld(player::Player& p, level::Level* target, float x, float y, float z,
                               bool from_portal) {
  if (!players_ || !target || !p.spawned) return;
  if (p.level == target && !from_portal) {
    // same world teleport only
    p.x = x;
    p.y = y;
    p.z = z;
    p.fall_distance_ = 0.f;
    p.on_ground_ = true;
    players_->sendPacket(
        p, protocol::encodeMovePlayer(0, p.x, p.y, p.z, p.yaw, p.yaw, p.pitch, 1, true), true);
    return;
  }

  closeContainer(p, true);
  // Effective client dim may be a /dim override; clear override on real world switch.
  const auto from_dim = (p.visual_dim >= 0)
                            ? static_cast<std::uint8_t>(p.visual_dim)
                            : (p.level ? p.level->protocolDimension() : protocol::DIMENSION_OVERWORLD);
  const auto to_dim = target->protocolDimension(); // PE 0.14: only 0/1 (never 2)
  p.visual_dim = -1;

  // Despawn this player from others in old world before level switch
  broadcastPlayerDespawn(p);
  p.level = target;
  p.sent_chunks.clear();
  p.known_entities.clear();
  p.known_players.clear();
  p.x = x;
  p.y = y;
  p.z = z;
  if (from_portal) {
    p.portal_ticks_ = 0;
    p.portal_cooldown_ = 80; // ~4s
  }
  p.fall_distance_ = 0.f;
  p.on_ground_ = true;
  p.fire_ticks_ = 0;
  p.hurt_cooldown_ = 20;

  const auto spawn = target->spawn();
  // ChangeDimension only when wire dim actually changes (nether ↔ overworld).
  // End (ender) stays dim 0 — sending dim=2 crashed PE 0.14 clients.
  if (from_dim != to_dim) {
    players_->sendPacket(p, protocol::encodeChangeDimension(to_dim), true);
  }
  players_->sendPacket(
      p,
      protocol::encodeStartGame(target->settings().seed, to_dim, target->generatorIdForStartGame(),
                                p.gamemode & 0x01, 0, spawn.x, spawn.y, spawn.z, p.x, p.y, p.z),
      true);
  players_->sendPacket(p, protocol::encodeSetSpawnPosition(spawn.x, spawn.y, spawn.z), true);
  players_->sendPacket(p, protocol::encodeSetTime(target->time(), true), true);
  players_->sendPacket(p, protocol::encodeSetHealth(p.health), true);
  {
    std::int32_t flags = 0x40;
    if (p.gamemode == 1) flags |= 0x80;
    players_->sendPacket(p, protocol::encodeAdventureSettings(flags), true);
    players_->sendPacket(p, protocol::encodeSetPlayerGameType(p.gamemode), true);
  }
  players_->sendChunksAround(p, 4);
  players_->sendPacket(p, protocol::encodePlayStatus(protocol::PLAY_STATUS_PLAYER_SPAWN), true);
  players_->sendPacket(
      p, protocol::encodeMovePlayer(0, p.x, p.y, p.z, p.yaw, p.yaw, p.pitch, 1, true), true);
  // PM setGamemode: creative palette before inventory contents
  if (p.gamemode == 1) players_->sendCreativeContents(p);
  players_->sendInventory(p);
  syncEntitiesToPlayer(p);
  syncPlayersToPlayer(p);
  broadcastPlayerSpawn(p);
}

void Server::applyPlayerVisualDim(player::Player& p, std::uint8_t dim, bool reset_to_level) {
  if (!players_ || !p.spawned || !p.level) return;
  // Client atmosphere only: pass requested dim as-is (no whitelist / no remap).
  const auto level_dim = p.level->protocolDimension();
  const std::uint8_t to_dim = reset_to_level ? level_dim : dim;

  const auto from_dim =
      (p.visual_dim >= 0) ? static_cast<std::uint8_t>(p.visual_dim) : level_dim;

  if (reset_to_level) {
    p.visual_dim = -1;
  } else {
    p.visual_dim = static_cast<int>(to_dim);
  }

  // Soft client reload in-place: keep world/pos/entities, only sky/atmosphere changes.
  // Always re-send StartGame with new dim; ChangeDimension when wire dim flips.
  if (from_dim != to_dim) {
    players_->sendPacket(p, protocol::encodeChangeDimension(to_dim), true);
  }
  const auto spawn = p.level->spawn();
  players_->sendPacket(
      p,
      protocol::encodeStartGame(p.level->settings().seed, to_dim, p.level->generatorIdForStartGame(),
                                p.gamemode & 0x01, 0, spawn.x, spawn.y, spawn.z, p.x, p.y, p.z),
      true);
  players_->sendPacket(p, protocol::encodeSetSpawnPosition(spawn.x, spawn.y, spawn.z), true);
  players_->sendPacket(p, protocol::encodeSetTime(p.level->time(), true), true);
  players_->sendPacket(p, protocol::encodeSetHealth(p.health), true);
  {
    std::int32_t flags = 0x40;
    if (p.gamemode == 1) flags |= 0x80;
    players_->sendPacket(p, protocol::encodeAdventureSettings(flags), true);
    players_->sendPacket(p, protocol::encodeSetPlayerGameType(p.gamemode), true);
  }
  // Chunks already known; re-assert spawn + position so client finishes dim switch
  players_->sendPacket(p, protocol::encodePlayStatus(protocol::PLAY_STATUS_PLAYER_SPAWN), true);
  players_->sendPacket(
      p, protocol::encodeMovePlayer(0, p.x, p.y, p.z, p.yaw, p.yaw, p.pitch, 1, true), true);
  if (p.gamemode == 1) players_->sendCreativeContents(p);
  players_->sendInventory(p);
}

bool Server::damagePlayer(player::Player& p, float amount, const char* cause) {
  if (!players_ || !p.spawned || amount <= 0.f) return false;
  if (p.gamemode == 1) return false; // creative invulnerable
  if (p.death_ticks_ >= 0) return false;
  if (p.hurt_cooldown_ > 0) return false;

  p.health = std::max(0, p.health - static_cast<int>(std::ceil(amount)));
  p.hurt_cooldown_ = 10; // ~0.5s i-frames
  players_->sendPacket(p, protocol::encodeSetHealth(p.health), true);
  // self eid is 0 on client; other viewers need real runtime entity_id (PM Living::attack)
  players_->sendPacket(p, protocol::encodeEntityEvent(0, protocol::ENTITY_EVENT_HURT), true);
  if (p.entity_id != 0) {
    auto hurt_view = protocol::encodeEntityEvent(p.entity_id, protocol::ENTITY_EVENT_HURT);
    for (auto& [_, other] : players_->all()) {
      if (!other.spawned || &other == &p || other.level != p.level) continue;
      if (!other.known_players.count(p.entity_id)) continue;
      players_->sendPacket(other, hurt_view, false);
    }
  }

  if (p.health <= 0) {
    p.death_ticks_ = 0;
    p.health = 0;
    p.fall_distance_ = 0.f;
    p.fire_ticks_ = 0;
    players_->sendPacket(p, protocol::encodeSetHealth(0), true);
    players_->sendPacket(p, protocol::encodeEntityEvent(0, protocol::ENTITY_EVENT_DEATH), true);
    if (p.entity_id != 0) {
      auto death_view = protocol::encodeEntityEvent(p.entity_id, protocol::ENTITY_EVENT_DEATH);
      for (auto& [_, other] : players_->all()) {
        if (!other.spawned || &other == &p || other.level != p.level) continue;
        if (!other.known_players.count(p.entity_id)) continue;
        players_->sendPacket(other, death_view, false);
      }
    }
    // PE 0.14: RespawnPacket on death unlocks the client respawn button (PM Player::kill).
    // Without this the button stays grey and only a server auto-respawn would recover.
    if (p.level) {
      const auto spawn = p.level->spawn();
      const float sx = static_cast<float>(spawn.x) + 0.5f;
      const float sy = static_cast<float>(spawn.y);
      const float sz = static_cast<float>(spawn.z) + 0.5f;
      players_->sendPacket(p, protocol::encodeRespawn(sx, sy, sz), true);
    }
    players_->sendPacket(
        p,
        protocol::encodeTextSystem(std::string("\xc2\xa7") + "cYou died" +
                                   (cause && cause[0] ? std::string(" (") + cause + ")" : "")),
        false);
    util::Logger::instance().info(p.username, " died", cause && cause[0] ? " (" : "",
                                  cause && cause[0] ? cause : "",
                                  cause && cause[0] ? ")" : "");
  }
  return true;
}

void Server::knockbackPlayer(player::Player& victim, float from_x, float from_z, float base) {
  if (!players_ || !victim.spawned || victim.gamemode == 1) return;
  // PM Living::knockBack: direction away from attacker, base default 0.4
  float x = victim.x - from_x;
  float z = victim.z - from_z;
  float f = std::sqrt(x * x + z * z);
  if (f <= 0.001f) {
    x = 0.f;
    z = 1.f;
    f = 1.f;
  }
  f = 1.f / f;
  float mx = x * f * base;
  float my = base;
  float mz = z * f * base;
  if (my > base) my = base;

  // Self: PE expects eid=0 for local player motion (PM Player::setMotion)
  players_->sendPacket(victim, protocol::encodeSetEntityMotion(0, mx, my, mz), true);
  // Nearby viewers: real runtime entity_id so they see the body fly back
  if (victim.entity_id != 0) {
    auto view = protocol::encodeSetEntityMotion(victim.entity_id, mx, my, mz);
    for (auto& [_, other] : players_->all()) {
      if (!other.spawned || &other == &victim || other.level != victim.level) continue;
      if (!other.known_players.count(victim.entity_id)) continue;
      players_->sendPacket(other, view, false);
    }
  }
  // Soft server-side nudge so next MovePlayer isn't rubber-band snapped instantly
  victim.x += mx * 0.5f;
  victim.y += my * 0.35f;
  victim.z += mz * 0.5f;
  victim.on_ground_ = false;
  victim.fall_distance_ = 0.f; // don't count knockback as fall start
}

void Server::respawnPlayer(player::Player& p) {
  if (!players_ || !p.level) return;
  if (p.death_ticks_ < 0 && p.health > 0) return; // already alive
  const auto spawn = p.level->spawn();
  p.health = 20;
  p.death_ticks_ = -1;
  p.fall_distance_ = 0.f;
  p.on_ground_ = true;
  p.fire_ticks_ = 0;
  p.hurt_cooldown_ = 60; // PM noDamageTicks ~60 after respawn
  p.x = static_cast<float>(spawn.x) + 0.5f;
  p.y = static_cast<float>(spawn.y);
  p.z = static_cast<float>(spawn.z) + 0.5f;
  // PM ACTION_RESPAWN: teleport + setHealth(max). Client already got RespawnPacket on death.
  players_->sendPacket(p, protocol::encodeSetHealth(p.health), true);
  players_->sendPacket(p, protocol::encodeRespawn(p.x, p.y, p.z), true);
  players_->sendPacket(
      p, protocol::encodeMovePlayer(0, p.x, p.y, p.z, p.yaw, p.yaw, p.pitch, 1, true), true);
  players_->sendPacket(p, protocol::encodePlayStatus(protocol::PLAY_STATUS_PLAYER_SPAWN), true);
  players_->sendChunksAround(p);
  // Death sent EntityEvent(DEATH) to viewers; client keeps a dead body for that eid.
  // spawnPlayerToPlayer early-returns if eid is still in known_players, so without a
  // RemovePlayer first the player stays invisible/corpse after respawn teleport.
  broadcastPlayerDespawn(p);
  broadcastPlayerSpawn(p);
  syncPlayersToPlayer(p);
}

void Server::tickPlayerDamage() {
  if (!players_) return;
  for (auto& [_, p] : players_->all()) {
    if (!p.spawned || !p.level) continue;
    if (p.hurt_cooldown_ > 0) --p.hurt_cooldown_;

    if (p.death_ticks_ >= 0) {
      ++p.death_ticks_;
      // No forced auto-respawn (PM waits for client ACTION_RESPAWN so the button works).
      // Void-stuck fallback only: if still dead deep in void for a long time, force respawn.
      if (p.y < -64.f && p.death_ticks_ >= 100) respawnPlayer(p);
      continue;
    }
    if (p.gamemode == 1) {
      p.fall_distance_ = 0.f;
      p.fire_ticks_ = 0;
      continue;
    }

    // feet block hazards
    const int bx = static_cast<int>(std::floor(p.x));
    const int by = static_cast<int>(std::floor(p.y));
    const int bz = static_cast<int>(std::floor(p.z));
    const auto feet = p.level->getBlockId(bx, by, bz);
    const auto body = p.level->getBlockId(bx, by + 1, bz);
    const bool in_lava = feet == protocol::BLOCK_LAVA || body == protocol::BLOCK_LAVA;
    const bool in_fire = feet == protocol::BLOCK_FIRE || body == protocol::BLOCK_FIRE;
    if (in_lava) {
      p.fire_ticks_ = std::max(p.fire_ticks_, 20);
      damagePlayer(p, 4.f, "lava");
    } else if (in_fire) {
      p.fire_ticks_ = std::max(p.fire_ticks_, 20);
      damagePlayer(p, 1.f, "fire");
    } else if (p.fire_ticks_ > 0) {
      --p.fire_ticks_;
      if ((p.fire_ticks_ % 20) == 0) damagePlayer(p, 1.f, "burn");
    }

    // void
    if (p.y < -16.f) damagePlayer(p, 4.f, "void");
  }
}

void Server::tickPlayerPortals() {
  if (!players_) return;
  for (auto& [_, p] : players_->all()) {
    if (!p.spawned || !p.level) continue;
    if (p.portal_cooldown_ > 0) {
      --p.portal_cooldown_;
      p.portal_ticks_ = 0;
      continue;
    }

    const int bx = static_cast<int>(std::floor(p.x));
    const int by = static_cast<int>(std::floor(p.y));
    const int bz = static_cast<int>(std::floor(p.z));
    // feet or body in portal
    const bool in_portal =
        p.level->getBlockId(bx, by, bz) == protocol::BLOCK_PORTAL ||
        p.level->getBlockId(bx, by + 1, bz) == protocol::BLOCK_PORTAL;

    if (!in_portal) {
      p.portal_ticks_ = 0;
      continue;
    }

    ++p.portal_ticks_;
    // ~3.5s stand (70 ticks) before travel — creative still needs stand time
    if (p.portal_ticks_ < 70) continue;

    level::Level* overworld = levels_.defaultLevel();
    level::Level* nether = levels_.get("nether");
    if (!overworld || !nether) {
      p.portal_ticks_ = 0;
      continue;
    }

    // End (ender) has no nether portal travel
    if (p.level->generator() == level::GeneratorType::End) {
      p.portal_ticks_ = 0;
      continue;
    }

    level::Level* dest = nullptr;
    float dx = p.x, dy = p.y, dz = p.z;
    if (p.level->protocolDimension() == protocol::DIMENSION_NETHER) {
      // nether -> overworld: *8 coords
      dest = overworld;
      dx = p.x * 8.f;
      dz = p.z * 8.f;
    } else {
      // overworld (and flat/normal) -> nether
      dest = nether;
      dx = p.x / 8.f;
      dz = p.z / 8.f;
    }

    // safe Y: open-air surface near target column (never nether ceiling underside)
    int tx = static_cast<int>(std::floor(dx));
    int tz = static_cast<int>(std::floor(dz));
    int feet = dest->safeStandFeetY(tx, tz);
    if (feet < 1) {
      // spiral search a few blocks around, then fall back to world spawn
      for (int r = 1; r <= 8 && feet < 1; ++r) {
        for (int oz = -r; oz <= r && feet < 1; ++oz) {
          for (int ox = -r; ox <= r && feet < 1; ++ox) {
            if (std::abs(ox) != r && std::abs(oz) != r) continue;
            const int fy = dest->safeStandFeetY(tx + ox, tz + oz);
            if (fy >= 1) {
              feet = fy;
              tx = tx + ox;
              tz = tz + oz;
            }
          }
        }
      }
    }
    if (feet < 1) {
      const auto sp = dest->spawn();
      feet = sp.y;
      tx = sp.x;
      tz = sp.z;
    }
    dx = static_cast<float>(tx) + 0.5f;
    dy = static_cast<float>(feet);
    dz = static_cast<float>(tz) + 0.5f;

    changePlayerWorld(p, dest, dx, dy, dz, true);
    players_->sendPacket(
        p,
        protocol::encodeTextSystem(std::string("\xc2\xa7") + "d[Portal] " +
                                   std::string(dest->name())),
        false);
  }
}

void Server::spawnEntityToPlayer(player::Player& p, const entity::Entity& e) {
  if (p.known_entities.count(e.eid)) return;
  if (e.kind == entity::EntityKind::ItemDrop) {
    // reliable: drops must reach client (unreliable can vanish on laggy links)
    auto pk = protocol::encodeAddItemEntity(e.eid, e.item_stack, e.x, e.y, e.z, e.motion_x,
                                            e.motion_y, e.motion_z);
    players_->sendPacket(p, std::move(pk), true);
  } else {
    // pitch forced 0 so models stay upright; yaw matches walk direction vector
    std::string meta;
    if (e.kind == entity::EntityKind::Sheep) {
      meta = protocol::encodeSheepMetadata(e.sheep_color, e.sheared);
    }
    auto pk = protocol::encodeAddEntity(e.eid, entity::networkType(e.kind), e.x, e.y, e.z, e.yaw,
                                        0.f /*pitch*/, 0.f, 0.f, 0.f, meta);
    players_->sendPacket(p, std::move(pk), false);
  }
  p.known_entities.insert(e.eid);
}

void Server::syncEntitiesToPlayer(player::Player& p) {
  if (!p.level || !p.spawned) return;
  for (auto& [id, e] : entities_.all()) {
    if (e.closed || e.level != p.level) continue;
    const float dx = e.x - p.x, dy = e.y - p.y, dz = e.z - p.z;
    if (dx * dx + dy * dy + dz * dz > 48.f * 48.f) continue;
    spawnEntityToPlayer(p, e);
  }
}

void Server::spawnPlayerToPlayer(player::Player& viewer, const player::Player& target) {
  if (!players_ || !viewer.spawned || !target.spawned) return;
  if (&viewer == &target) return;
  if (!viewer.level || viewer.level != target.level) return;
  if (target.entity_id == 0 || target.username.empty()) return;
  // Don't (re)spawn a corpse as a living player while still dead
  if (target.death_ticks_ >= 0 || target.health <= 0) return;
  if (viewer.known_players.count(target.entity_id)) return;

  auto pk = protocol::encodeAddPlayer(target.uuid, target.username, target.entity_id, target.x,
                                      target.y, target.z, target.yaw, target.pitch, target.heldItem());
  players_->sendPacket(viewer, std::move(pk), true);
  viewer.known_players.insert(target.entity_id);
}

void Server::despawnPlayerFrom(player::Player& viewer, const player::Player& target) {
  if (!players_) return;
  if (!viewer.known_players.erase(target.entity_id)) return;
  players_->sendPacket(viewer, protocol::encodeRemovePlayer(target.entity_id, target.uuid), true);
}

void Server::syncPlayersToPlayer(player::Player& p) {
  if (!players_ || !p.level || !p.spawned) return;
  constexpr float kRange = 64.f;
  constexpr float kRange2 = kRange * kRange;

  // Despawn players that left range / world / unspawned
  std::vector<std::int64_t> drop;
  for (auto eid : p.known_players) {
    bool still = false;
    for (auto& [_, other] : players_->all()) {
      if (!other.spawned || other.entity_id != eid) continue;
      if (other.level != p.level) break;
      const float dx = other.x - p.x, dy = other.y - p.y, dz = other.z - p.z;
      if (dx * dx + dy * dy + dz * dz <= kRange2) still = true;
      break;
    }
    if (!still) drop.push_back(eid);
  }
  for (auto eid : drop) {
    // find uuid for RemovePlayer if still in map; else RemoveEntity fallback
    player::Player* found = nullptr;
    for (auto& [_, other] : players_->all()) {
      if (other.entity_id == eid) {
        found = &other;
        break;
      }
    }
    p.known_players.erase(eid);
    if (found) {
      players_->sendPacket(p, protocol::encodeRemovePlayer(eid, found->uuid), true);
    } else {
      players_->sendPacket(p, protocol::encodeRemoveEntity(eid), true);
    }
  }

  // Spawn nearby others
  for (auto& [_, other] : players_->all()) {
    if (!other.spawned || &other == &p || other.level != p.level) continue;
    const float dx = other.x - p.x, dy = other.y - p.y, dz = other.z - p.z;
    if (dx * dx + dy * dy + dz * dz > kRange2) continue;
    spawnPlayerToPlayer(p, other);
  }
}

void Server::broadcastPlayerSpawn(const player::Player& joined) {
  if (!players_ || !joined.spawned) return;
  for (auto& [_, pl] : players_->all()) {
    if (!pl.spawned || &pl == &joined || pl.level != joined.level) continue;
    const float dx = joined.x - pl.x, dy = joined.y - pl.y, dz = joined.z - pl.z;
    if (dx * dx + dy * dy + dz * dz > 64.f * 64.f) continue;
    spawnPlayerToPlayer(pl, joined);
  }
}

void Server::broadcastPlayerDespawn(const player::Player& left) {
  if (!players_) return;
  for (auto& [_, pl] : players_->all()) {
    if (&pl == &left) continue;
    if (pl.known_players.erase(left.entity_id)) {
      players_->sendPacket(pl, protocol::encodeRemovePlayer(left.entity_id, left.uuid), true);
    }
  }
}

entity::Entity* Server::dropItemInWorld(level::Level* level, float x, float y, float z,
                                        item::ItemStack stack, float mx, float my, float mz,
                                        int pickup_delay) {
  if (!level || stack.empty() || !players_) return nullptr;
  auto& e = entities_.spawnItem(level, x, y, z, std::move(stack), mx, my, mz, pickup_delay);
  // show to nearby players immediately + motion (PM Item entity)
  auto motion = protocol::encodeSetEntityMotion(e.eid, e.motion_x, e.motion_y, e.motion_z);
  for (auto& [_, pl] : players_->all()) {
    if (!pl.spawned || pl.level != level) continue;
    const float dx = e.x - pl.x, dy = e.y - pl.y, dz = e.z - pl.z;
    if (dx * dx + dy * dy + dz * dz > 48.f * 48.f) continue;
    spawnEntityToPlayer(pl, e);
    players_->sendPacket(pl, motion, true);
  }
  return &e;
}

bool Server::playerThrowHeld(player::Player& p, item::ItemStack stack, bool full_stack) {
  if (!p.level || !p.spawned || stack.empty()) return false;

  // Prefer matching held item; fall back to first matching stack in inventory
  item::ItemStack* src = nullptr;
  auto& held = p.heldItem();
  if (!held.empty() && held.sameType(stack)) {
    src = &held;
  } else {
    for (auto& slot : p.inventory) {
      if (!slot.empty() && slot.sameType(stack)) {
        src = &slot;
        break;
      }
    }
  }
  // creative: allow throw even if not in inv (clone from stack)
  item::ItemStack to_drop;
  if (src) {
    if (full_stack || p.gamemode == 1) {
      to_drop = *src;
      if (p.gamemode != 1) *src = item::ItemStack::air();
      else if (!full_stack) {
        to_drop.count = 1;
        // creative keeps original
      }
    } else {
      to_drop = item::ItemStack::of(src->id, 1, src->damage);
      if (src->count > 1) --src->count;
      else *src = item::ItemStack::air();
    }
  } else if (p.gamemode == 1) {
    to_drop = full_stack ? stack : item::ItemStack::of(stack.id, 1, stack.damage);
  } else {
    return false;
  }
  if (to_drop.empty()) return false;

  float dx = 0, dy = 0, dz = 0;
  entity::directionVector(p.yaw, p.pitch, dx, dy, dz);
  // PHP: multiply(0.4) look vector; drop from eye height ~1.3
  const float mx = dx * 0.4f;
  const float my = dy * 0.4f + 0.1f;
  const float mz = dz * 0.4f;
  dropItemInWorld(p.level, p.x, p.y + 1.3f, p.z, to_drop, mx, my, mz, 40);
  if (p.gamemode != 1) players_->sendInventory(p);
  return true;
}

void Server::tickItemPickups() {
  if (!players_) return;
  for (auto& [_, pl] : players_->all()) {
    if (!pl.spawned || !pl.level) continue;
    // allow multiple pickups per tick so walking over a pile feels snappy
    int picks = 0;
    for (auto& [id, e] : entities_.all()) {
      if (picks >= 8) break;
      if (e.closed || e.kind != entity::EntityKind::ItemDrop || e.level != pl.level) continue;
      if (e.pickup_delay > 0 || e.item_stack.empty()) continue;
      // PE MovePlayer.y is often eye/body (~+1.62), item sits near feet → need wide vertical.
      // Horizontal ~2x2; vertical covers feet-to-head whether y is feet or eyes.
      const float dx = e.x - pl.x;
      const float dy = e.y - pl.y;
      const float dz = e.z - pl.z;
      if (std::fabs(dx) > 1.5f || std::fabs(dz) > 1.5f) continue;
      if (dy < -2.5f || dy > 3.5f) continue;

      // try place into inventory (creative: always absorb like PM — discard overflow)
      bool placed = false;
      auto remaining = e.item_stack;
      for (auto& slot : pl.inventory) {
        if (slot.id == remaining.id && slot.damage == remaining.damage && slot.count < 64) {
          const int space = 64 - slot.count;
          const int take = std::min(space, static_cast<int>(remaining.count));
          slot.count = static_cast<std::uint8_t>(slot.count + take);
          remaining.count = static_cast<std::uint8_t>(remaining.count - take);
          placed = true;
          if (remaining.count == 0) break;
        }
      }
      if (remaining.count > 0) {
        for (auto& slot : pl.inventory) {
          if (slot.empty()) {
            slot = remaining;
            remaining = item::ItemStack::air();
            placed = true;
            break;
          }
        }
      }
      if (pl.gamemode == 1) {
        // creative: vanish item even if inv full (PM does not require free slot)
        remaining = item::ItemStack::air();
        placed = true;
      }
      if (!placed && remaining.count == e.item_stack.count) continue; // survival inv full

      // pickup animation: broadcast real collector eid; also send eid=0 to self (PM 0.14)
      if (remaining.empty()) {
        auto take_pk = protocol::encodeTakeItemEntity(e.eid, pl.entity_id);
        players_->broadcastNear(e.x, e.y, e.z, 32.f, take_pk, e.level, &pl);
        players_->sendPacket(pl, protocol::encodeTakeItemEntity(e.eid, 0), true);
        auto rm = protocol::encodeRemoveEntity(e.eid);
        for (auto& [__, other] : players_->all()) {
          if (other.known_entities.erase(e.eid)) players_->sendPacket(other, rm, true);
        }
        e.closed = true;
      } else {
        e.item_stack = remaining;
        auto rm = protocol::encodeRemoveEntity(e.eid);
        for (auto& [__, other] : players_->all()) {
          if (other.known_entities.erase(e.eid)) players_->sendPacket(other, rm);
        }
        for (auto& [__, other] : players_->all()) {
          if (other.spawned && other.level == e.level) spawnEntityToPlayer(other, e);
        }
      }
      players_->sendInventory(pl);
      ++picks;
    }
  }
}

namespace {
// Shared dynamic window ids for chest + furnace (PM Player::$windowCnt single counter).
std::uint8_t nextDynamicWindowId() {
  static thread_local std::uint8_t next_win = protocol::WINDOW_FIRST_DYNAMIC;
  std::uint8_t wid = next_win++;
  if (next_win > 99) next_win = protocol::WINDOW_FIRST_DYNAMIC;
  if (wid == 0 || wid == protocol::WINDOW_INVENTORY || wid == protocol::WINDOW_ARMOR ||
      wid == protocol::WINDOW_CREATIVE) {
    wid = protocol::WINDOW_FIRST_DYNAMIC;
  }
  return wid;
}

std::string hexPreview(std::string_view bytes, std::size_t max_n = 64) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  const std::size_t n = std::min(bytes.size(), max_n);
  for (std::size_t i = 0; i < n; ++i) {
    oss << std::setw(2) << (static_cast<unsigned>(static_cast<std::uint8_t>(bytes[i])));
  }
  if (bytes.size() > max_n) oss << "...";
  return oss.str();
}

} // namespace

void Server::tickFurnaces() {
  // v0.4.14: furnace feature fully removed — no smelt tick / no UI push
}

void Server::tickEntities() {
  auto moved = entities_.tick(1.f);
  if (!players_) return;

  // despawn closed from clients
  for (auto& [id, e] : entities_.all()) {
    if (!e.closed) continue;
    auto pk = protocol::encodeRemoveEntity(e.eid);
    for (auto& [_, pl] : players_->all()) {
      if (pl.known_entities.erase(e.eid)) {
        players_->sendPacket(pl, pk);
      }
    }
  }

  // broadcast moves (mobs + item entities)
  for (auto* e : moved) {
    if (e->kind == entity::EntityKind::ItemDrop) {
      auto pk = protocol::encodeMoveEntity(e->eid, e->x, e->y, e->z, 0.f, 0.f, 0.f);
      players_->broadcastNear(e->x, e->y, e->z, 64.f, pk, e->level, nullptr);
    } else {
      // pitch 0 keeps models upright; yaw = walk facing (PHP direction vector)
      auto pk =
          protocol::encodeMoveEntity(e->eid, e->x, e->y, e->z, e->yaw, e->yaw, 0.f /*pitch*/);
      players_->broadcastNear(e->x, e->y, e->z, 64.f, pk, e->level, nullptr);
    }
  }

  tickItemPickups();

  // ensure nearby players know entities
  if ((tick_counter_ % 20) == 0) {
    for (auto& [_, pl] : players_->all()) {
      if (pl.spawned) syncEntitiesToPlayer(pl);
    }
  }
}

player::Player* Server::findPlayerByName(std::string_view name) {
  if (!players_ || name.empty()) return nullptr;
  const auto key = toLowerName(name);
  for (auto& [_, pl] : players_->all()) {
    if (toLowerName(pl.username) == key) return &pl;
  }
  return nullptr;
}

std::optional<std::string> Server::checkBanned(const player::Player& p) const {
  if (auto r = bans_.reasonForName(p.username)) {
    return r->empty() ? std::string("Banned") : ("Banned: " + *r);
  }
  if (auto r = bans_.reasonForIp(p.endpoint.address)) {
    return r->empty() ? std::string("IP banned") : ("IP banned: " + *r);
  }
  if (auto r = bans_.reasonForCid(p.client_id)) {
    return r->empty() ? std::string("Client banned") : ("Client banned: " + *r);
  }
  return std::nullopt;
}

void Server::kickPlayer(player::Player& p, std::string_view reason) {
  if (!players_) return;
  std::string msg = reason.empty() ? "Kicked from server" : std::string(reason);
  // Clean disconnect: PE Disconnect packet (message shown) then close RakNet session.
  players_->sendPacket(p, protocol::encodeDisconnect(msg), true);
  if (sessions_) {
    if (auto* s = sessions_->getSession(p.endpoint)) {
      s->disconnect(msg);
    } else {
      sessions_->removeSession(p.endpoint, msg);
    }
  }
  // PlayerManager cleanup happens on session close callback.
}

void Server::handleLogin(player::Player& p, std::string_view buffer) {
  auto login = protocol::decodeLogin(buffer);
  if (!login.ok || login.username.empty()) {
    util::Logger::instance().warning("Bad LoginPacket from ", p.endpoint.address);
    players_->sendPacket(p, protocol::encodeDisconnect("Invalid login"), true);
    kickPlayer(p, "Invalid login");
    return;
  }

  if (login.protocol1 != 70 && login.protocol1 != 60 && login.protocol1 != 46 &&
      login.protocol1 != 45) {
    players_->sendPacket(p, protocol::encodePlayStatus(protocol::PLAY_STATUS_LOGIN_FAILED_CLIENT),
                         true);
    kickPlayer(p, "Outdated client");
    return;
  }

  p.username = login.username;
  p.protocol = login.protocol1;
  p.client_id = login.client_id;
  p.uuid = login.uuid;
  p.skin_name = login.skin_name.empty() ? "Standard_Custom" : login.skin_name;
  p.skin_data = login.skin_data;
  p.gamemode = cfg_.gamemode; // default; may be overwritten by loadPlayer
  p.level = levels_.defaultLevel();

  // Ban check before world load / spawn
  if (auto ban = checkBanned(p)) {
    util::Logger::instance().notice("Rejected banned login ", p.username, " from ",
                                    p.endpoint.address, " cid=", p.client_id, " — ", *ban);
    kickPlayer(p, *ban);
    return;
  }

  // Load pos/inv/world BEFORE StartGame so spawn uses saved data
  players_->loadPlayer(p);
  if (p.has_saved_data && !p.saved_world_name.empty()) {
    if (auto* lvl = levels_.get(p.saved_world_name)) {
      p.level = lvl;
    } else {
      // Saved world gone (e.g. zc -> world rename): keep inv/mode, reset to default spawn.
      p.level = levels_.defaultLevel();
      if (p.level) {
        const auto sp = p.level->spawn();
        p.x = static_cast<float>(sp.x) + 0.5f;
        p.y = static_cast<float>(sp.y);
        p.z = static_cast<float>(sp.z) + 0.5f;
      }
      util::Logger::instance().notice(
          p.username, " saved world '", p.saved_world_name,
          "' missing; moved to default ", p.level ? p.level->name() : "?");
    }
  }

  plugin::PlayerLoginEvent lev;
  lev.address = p.endpoint.address;
  lev.port = p.endpoint.port;
  lev.username = p.username;
  lev.protocol = p.protocol;
  lev.client_id = p.client_id;
  lev.world = p.level ? p.level->name() : "";
  plugins_.firePlayerLogin(lev);

  players_->doLoginSequence(p);
  syncEntitiesToPlayer(p);
  syncPlayersToPlayer(p);
  broadcastPlayerSpawn(p);

  plugin::PlayerJoinEvent jev;
  jev.username = p.username;
  jev.world = p.level ? p.level->name() : "";
  jev.x = p.x;
  jev.y = p.y;
  jev.z = p.z;
  plugins_.firePlayerJoin(jev);

  players_->broadcastText(std::string("\xc2\xa7") + "a[+] " + p.username);
  if (sessions_) sessions_->setName(buildRaklibName());
}

void Server::handleText(player::Player& p, std::string_view buffer) {
  auto t = protocol::decodeText(buffer);
  if (!t.ok) return;
  if (t.type != protocol::TEXT_CHAT && t.type != protocol::TEXT_RAW) return;

  std::string msg = t.message;
  while (!msg.empty() && msg[0] == ' ') msg.erase(msg.begin());
  if (msg.empty()) return;

  if (msg[0] == '/') {
    std::string rest = msg.substr(1);
    std::string cmd;
    std::string args;
    auto sp = rest.find(' ');
    if (sp == std::string::npos) {
      cmd = rest;
    } else {
      cmd = rest.substr(0, sp);
      args = rest.substr(sp + 1);
    }
    for (auto& c : cmd)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

    plugin::CommandEvent cev;
    cev.username = p.username;
    cev.command = cmd;
    cev.args = args;
    plugins_.fireCommand(cev);
    if (!cev.handled) {
      handleBuiltinCommand(p, cmd, args);
    }
    return;
  }

  plugin::ChatEvent cev;
  cev.username = p.username;
  cev.message = msg;
  plugins_.fireChat(cev);
  if (cev.cancelled) return;

  util::Logger::instance().info("<", p.username, "> ", msg);
  players_->broadcastChat(p.username, msg);
}

void Server::handleBuiltinCommand(player::Player& p, std::string_view cmd, std::string_view args) {
  const PermLevel level = ops_.isOp(p.username) ? PermLevel::Op : PermLevel::Player;
  auto reply = [this, &p](std::string_view msg) {
    if (players_) players_->sendPacket(p, protocol::encodeTextSystem(msg), false);
  };
  dispatchCommand(p.username, level, cmd, args, &p, reply);
}

void Server::handleConsoleLine(std::string line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
    line.pop_back();
  std::size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i > 0) line = line.substr(i);
  if (line.empty()) return;
  if (line[0] == '/') line = line.substr(1);
  std::lock_guard lock(console_mu_);
  console_queue_.push(std::move(line));
}

void Server::processConsoleQueue() {
  std::vector<std::string> batch;
  {
    std::lock_guard lock(console_mu_);
    while (!console_queue_.empty()) {
      batch.push_back(std::move(console_queue_.front()));
      console_queue_.pop();
    }
  }
  for (auto& line : batch) {
    std::string cmd;
    std::string args;
    auto sp = line.find(' ');
    if (sp == std::string::npos) {
      cmd = line;
    } else {
      cmd = line.substr(0, sp);
      args = line.substr(sp + 1);
    }
    for (auto& c : cmd)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    util::Logger::instance().info("[CONSOLE] /", cmd, args.empty() ? "" : " ", args);
    auto reply = [](std::string_view msg) {
      util::Logger::instance().notice("[CMD] ", msg);
    };
    dispatchCommand("CONSOLE", PermLevel::Console, cmd, args, nullptr, reply);
  }
}

void Server::consoleThreadMain() {
  std::string line;
  while (running_) {
    if (!std::getline(std::cin, line)) {
      // EOF / pipe closed — stop reading but keep server up
      break;
    }
    if (!running_) break;
    handleConsoleLine(std::move(line));
  }
}

void Server::dispatchCommand(std::string_view source_name, PermLevel level, std::string_view cmd,
                             std::string_view args, player::Player* player,
                             const CommandReply& reply) {
  const PermLevel need = commandRequiredLevel(cmd);
  if (static_cast<int>(level) < static_cast<int>(need)) {
    reply(std::string("\xc2\xa7") + "cYou do not have permission to use /" + std::string(cmd));
    return;
  }

  auto trim = [](std::string s) {
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
  };

  if (cmd == "help" || cmd == "?") {
    if (static_cast<int>(level) >= static_cast<int>(PermLevel::Op)) {
      reply("Commands: /help /list /worlds /spawn /me /ver | OP: /goto /fuck /gm /give /spawnmob "
            "/clear /op /deop /kick /ban /ban-ip /ban-cid /unban /banlist /stop");
    } else {
      reply("Commands: /help /list /worlds /spawn /me /ver /version");
    }
    return;
  }

  if (cmd == "ver" || cmd == "version") {
    reply(std::string("MPMPESCoreCpp 0.4.24 (plugins-c-py-perf) | MCPE ") + cfg_.version_name +
          " protocol " + std::to_string(cfg_.protocol));
    return;
  }

  if (cmd == "list") {
    if (!players_) {
      reply("Players (0):");
      return;
    }
    std::ostringstream os;
    os << "Players (" << players_->count() << "): ";
    bool first = true;
    for (auto& [_, pl] : players_->all()) {
      if (pl.state != player::PlayerState::Playing) continue;
      if (!first) os << ", ";
      os << pl.username;
      if (ops_.isOp(pl.username)) os << "[op]";
      first = false;
    }
    reply(os.str());
    return;
  }

  if (cmd == "worlds") {
    std::ostringstream os;
    os << "Worlds: ";
    bool first = true;
    for (auto& [name, _] : levels_.all()) {
      if (!first) os << ", ";
      os << name;
      first = false;
    }
    reply(os.str());
    return;
  }

  if (cmd == "me") {
    if (!players_) return;
    players_->broadcastText("* " + std::string(source_name) + " " + std::string(args));
    return;
  }

  if (cmd == "spawn") {
    if (!player || !player->level) {
      reply("spawn requires an in-game player");
      return;
    }
    const auto spawn = player->level->spawn();
    player->x = static_cast<float>(spawn.x) + 0.5f;
    player->y = static_cast<float>(spawn.y);
    player->z = static_cast<float>(spawn.z) + 0.5f;
    players_->sendPacket(
        *player,
        protocol::encodeMovePlayer(0, player->x, player->y, player->z, player->yaw, player->yaw,
                                   player->pitch, 1, true),
        true);
    reply("Returned to spawn");
    return;
  }

  if (cmd == "goto") {
    if (!player) {
      reply("goto requires an in-game player");
      return;
    }
    std::string world = trim(std::string(args));
    auto sp = world.find(' ');
    if (sp != std::string::npos) world = world.substr(0, sp);
    // PM-style alias: end -> ender
    if (world == "end") world = "ender";
    auto* lvl = levels_.get(world);
    if (!lvl) {
      reply("Unknown world: " + world + " (try: world nether ender)");
      return;
    }
    const auto spawn = lvl->spawn();
    changePlayerWorld(*player, lvl, static_cast<float>(spawn.x) + 0.5f,
                      static_cast<float>(spawn.y), static_cast<float>(spawn.z) + 0.5f, false);
    reply("Teleported to world " + lvl->name() + " (dim " + std::to_string(lvl->dimension()) +
          ")");
    return;
  }

  if (cmd == "fuck") {
    // /fuck [player] — OP/console: client atmosphere only, send wire dim=2.
    std::string target_tok = trim(std::string(args));
    if (!target_tok.empty()) {
      auto sp = target_tok.find(' ');
      if (sp != std::string::npos) target_tok = trim(target_tok.substr(0, sp));
    }

    player::Player* target = player;
    if (!target_tok.empty()) {
      target = findPlayerByName(target_tok);
      if (!target) {
        reply("Player not online: " + target_tok);
        return;
      }
    } else if (!player) {
      reply("Usage: /fuck [player]   (console needs a player name)");
      return;
    }
    if (!target->spawned || !target->level) {
      reply("Target not fully spawned");
      return;
    }

    applyPlayerVisualDim(*target, protocol::DIMENSION_END_INTERNAL, false);
    reply(std::string("已法克(") + target->username + ")");
    return;
  }

  if (cmd == "gm" || cmd == "gamemode") {
    // /gm [mode] [player]
    //  - no args: toggle self (in-game only)
    //  - /gm 1 or /gm creative: set self (in-game) / requires target on console
    //  - /gm 1 Steve: set target (in-game or console)
    std::string a = trim(std::string(args));
    std::string mode_tok;
    std::string target_tok;
    if (!a.empty()) {
      auto sp = a.find(' ');
      if (sp == std::string::npos) {
        mode_tok = a;
      } else {
        mode_tok = trim(a.substr(0, sp));
        target_tok = trim(a.substr(sp + 1));
      }
    }

    player::Player* target = player;
    if (!target_tok.empty()) {
      target = findPlayerByName(target_tok);
      if (!target) {
        reply("Player not online: " + target_tok);
        return;
      }
    } else if (!player) {
      reply("Usage: gm <0|1|survival|creative> <player>   (console needs a player name)");
      return;
    }

    int gm = target->gamemode;
    if (!mode_tok.empty()) {
      std::string m = mode_tok;
      for (auto& c : m)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      try {
        gm = std::stoi(m);
      } catch (...) {
        if (m == "c" || m == "creative" || m == "1") gm = 1;
        else if (m == "s" || m == "survival" || m == "0") gm = 0;
        else {
          // bare name as only arg: treat as target + toggle (in-game)
          if (target_tok.empty() && player) {
            auto* named = findPlayerByName(mode_tok);
            if (named) {
              target = named;
              gm = target->gamemode == 1 ? 0 : 1;
            } else {
              reply("Unknown gamemode: " + mode_tok + " (use 0/1/s/c or survival/creative)");
              return;
            }
          } else {
            reply("Unknown gamemode: " + mode_tok + " (use 0/1/s/c or survival/creative)");
            return;
          }
        }
      }
    } else {
      gm = target->gamemode == 1 ? 0 : 1;
    }
    gm = (gm == 1) ? 1 : 0;
    target->gamemode = gm;
    players_->sendPacket(*target, protocol::encodeSetPlayerGameType(target->gamemode), true);
    std::int32_t flags = 0x40;
    if (target->gamemode == 1) flags |= 0x80;
    players_->sendPacket(*target, protocol::encodeAdventureSettings(flags), true);
    target->inventory = item::starterInventory(target->gamemode == 1);
    if (target->gamemode == 1) players_->sendCreativeContents(*target);
    players_->sendInventory(*target);
    const char* mode_s = target->gamemode == 1 ? "CREATIVE" : "SURVIVAL";
    if (player && target == player) {
      reply(std::string("Gamemode: ") + mode_s);
    } else {
      reply(std::string("Set ") + target->username + " gamemode: " + mode_s);
      if (player != target) {
        players_->sendPacket(*target,
                             protocol::encodeTextSystem(std::string("\xc2\xa7") + "eYour gamemode: " +
                                                        mode_s),
                             false);
      }
    }
    return;
  }

  if (cmd == "give") {
    if (!player) {
      reply("give requires an in-game player");
      return;
    }
    std::string a = trim(std::string(args));
    std::istringstream ss(a);
    int id = 1, count = 1, dmg = 0;
    ss >> id >> count >> dmg;
    if (id <= 0) {
      reply("Usage: /give <id> [count] [damage]");
      return;
    }
    count = std::max(1, std::min(count, 64));
    auto stack = item::ItemStack::of(static_cast<std::int16_t>(id),
                                     static_cast<std::uint8_t>(count),
                                     static_cast<std::int16_t>(dmg));
    bool ok = false;
    for (auto& slot : player->inventory) {
      if (slot.empty()) {
        slot = stack;
        ok = true;
        break;
      }
    }
    if (!ok) {
      reply("Inventory full");
      return;
    }
    players_->sendInventory(*player);
    reply("Gave " + std::to_string(count) + "x id=" + std::to_string(id));
    return;
  }

  if (cmd == "spawnmob" || cmd == "summon") {
    if (!player || !player->level) {
      reply("spawnmob requires an in-game player");
      return;
    }
    std::string a = trim(std::string(args));
    for (auto& c : a)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    entity::EntityKind kind = entity::EntityKind::Pig;
    if (a.find("chicken") != std::string::npos) kind = entity::EntityKind::Chicken;
    else if (a.find("cow") != std::string::npos) kind = entity::EntityKind::Cow;
    else if (a.find("sheep") != std::string::npos) kind = entity::EntityKind::Sheep;
    else if (a.find("zombie") != std::string::npos) kind = entity::EntityKind::Zombie;
    else if (a.find("pig") != std::string::npos || a.empty()) kind = entity::EntityKind::Pig;
    auto& e = entities_.spawn(kind, player->level, player->x + 1.5f, player->y, player->z + 1.5f);
    for (auto& [_, pl] : players_->all()) {
      if (pl.level == player->level && pl.spawned) spawnEntityToPlayer(pl, e);
    }
    reply(std::string("Spawned ") + entity::kindName(kind));
    return;
  }

  if (cmd == "clear") {
    if (!player) {
      reply("clear requires an in-game player");
      return;
    }
    player->inventory.assign(36, item::ItemStack::air());
    players_->sendInventory(*player);
    reply("Inventory cleared");
    return;
  }

  if (cmd == "op") {
    std::string target = trim(std::string(args));
    if (target.empty()) {
      reply("Usage: /op <player>");
      return;
    }
    // keep original casing from online player if present
    std::string display = target;
    if (players_) {
      for (auto& [_, pl] : players_->all()) {
        if (toLowerName(pl.username) == toLowerName(target)) {
          display = pl.username;
          break;
        }
      }
    }
    if (ops_.addOp(display)) {
      ops_.save();
      reply("Opped " + display);
      util::Logger::instance().notice(std::string(source_name), " opped ", display);
      if (players_) {
        for (auto& [_, pl] : players_->all()) {
          if (toLowerName(pl.username) == toLowerName(display)) {
            players_->sendPacket(pl, protocol::encodeTextSystem(std::string("\xc2\xa7") + "aYou are now an operator"),
                                 false);
          }
        }
      }
    } else {
      reply(display + " is already an operator");
    }
    return;
  }

  if (cmd == "deop") {
    std::string target = trim(std::string(args));
    if (target.empty()) {
      reply("Usage: /deop <player>");
      return;
    }
    if (ops_.removeOp(target)) {
      ops_.save();
      reply("De-opped " + target);
      util::Logger::instance().notice(std::string(source_name), " de-opped ", target);
    } else {
      reply(target + " is not an operator");
    }
    return;
  }

  if (cmd == "kick") {
    std::string a = trim(std::string(args));
    if (a.empty()) {
      reply("Usage: /kick <player> [reason]");
      return;
    }
    std::string target;
    std::string reason;
    const auto sp = a.find(' ');
    if (sp == std::string::npos) {
      target = a;
      reason = "Kicked by " + std::string(source_name);
    } else {
      target = a.substr(0, sp);
      reason = trim(a.substr(sp + 1));
      if (reason.empty()) reason = "Kicked by " + std::string(source_name);
    }
    auto* pl = findPlayerByName(target);
    if (!pl) {
      reply("Player not online: " + target);
      return;
    }
    const std::string name = pl->username;
    kickPlayer(*pl, reason);
    reply("Kicked " + name + ": " + reason);
    util::Logger::instance().notice(std::string(source_name), " kicked ", name, ": ", reason);
    if (players_) {
      players_->broadcastText(std::string("\xc2\xa7") + "e" + name + " was kicked: " + reason);
    }
    return;
  }

  if (cmd == "ban") {
    std::string a = trim(std::string(args));
    if (a.empty()) {
      reply("Usage: /ban <player> [reason]");
      return;
    }
    std::string target;
    std::string reason;
    const auto sp = a.find(' ');
    if (sp == std::string::npos) {
      target = a;
    } else {
      target = a.substr(0, sp);
      reason = trim(a.substr(sp + 1));
    }
    std::string display = target;
    std::string ip;
    std::int64_t cid = 0;
    if (auto* pl = findPlayerByName(target)) {
      display = pl->username;
      ip = pl->endpoint.address;
      cid = pl->client_id;
    }
    bans_.banName(display, reason);
    bans_.save();
    if (auto* pl = findPlayerByName(display)) {
      const std::string msg = reason.empty() ? "Banned" : ("Banned: " + reason);
      kickPlayer(*pl, msg);
    }
    reply("Banned " + display +
          (reason.empty() ? "" : (" (" + reason + ")")) +
          (ip.empty() ? "" : (" ip=" + ip)) +
          (cid ? (" cid=" + std::to_string(cid)) : ""));
    util::Logger::instance().notice(std::string(source_name), " banned ", display,
                                    reason.empty() ? "" : (" reason=" + reason));
    if (players_) {
      players_->broadcastText(std::string("\xc2\xa7") + "c" + display + " was banned" +
                              (reason.empty() ? "" : (": " + reason)));
    }
    return;
  }

  if (cmd == "unban" || cmd == "pardon") {
    std::string target = trim(std::string(args));
    if (target.empty()) {
      reply("Usage: /unban <player>");
      return;
    }
    if (bans_.unbanName(target)) {
      bans_.save();
      reply("Unbanned " + target);
      util::Logger::instance().notice(std::string(source_name), " unbanned ", target);
    } else {
      reply(target + " is not name-banned");
    }
    return;
  }

  if (cmd == "ban-ip" || cmd == "banip") {
    std::string a = trim(std::string(args));
    if (a.empty()) {
      reply("Usage: /ban-ip <ip|player> [reason]");
      return;
    }
    std::string first;
    std::string reason;
    const auto sp = a.find(' ');
    if (sp == std::string::npos) {
      first = a;
    } else {
      first = a.substr(0, sp);
      reason = trim(a.substr(sp + 1));
    }
    std::string ip = first;
    // if arg is online player name, use their IP
    if (auto* pl = findPlayerByName(first)) {
      ip = pl->endpoint.address;
    }
    bans_.banIp(ip, reason);
    bans_.save();
    // kick all online with this IP
    if (players_) {
      const std::string msg = reason.empty() ? "IP banned" : ("IP banned: " + reason);
      std::vector<player::Player*> to_kick;
      for (auto& [_, pl] : players_->all()) {
        if (toLowerName(pl.endpoint.address) == toLowerName(ip)) to_kick.push_back(&pl);
      }
      for (auto* pl : to_kick) kickPlayer(*pl, msg);
    }
    reply("Banned IP " + ip + (reason.empty() ? "" : (" (" + reason + ")")));
    util::Logger::instance().notice(std::string(source_name), " ban-ip ", ip);
    return;
  }

  if (cmd == "unban-ip" || cmd == "pardon-ip") {
    std::string ip = trim(std::string(args));
    if (ip.empty()) {
      reply("Usage: /unban-ip <ip>");
      return;
    }
    if (bans_.unbanIp(ip)) {
      bans_.save();
      reply("Unbanned IP " + ip);
      util::Logger::instance().notice(std::string(source_name), " unban-ip ", ip);
    } else {
      reply(ip + " is not IP-banned");
    }
    return;
  }

  if (cmd == "ban-cid" || cmd == "bancid") {
    std::string a = trim(std::string(args));
    if (a.empty()) {
      reply("Usage: /ban-cid <clientId|player> [reason]");
      return;
    }
    std::string first;
    std::string reason;
    const auto sp = a.find(' ');
    if (sp == std::string::npos) {
      first = a;
    } else {
      first = a.substr(0, sp);
      reason = trim(a.substr(sp + 1));
    }
    std::int64_t cid = 0;
    bool have = false;
    // online player name → their client_id
    if (auto* pl = findPlayerByName(first)) {
      cid = pl->client_id;
      have = true;
    } else {
      try {
        cid = static_cast<std::int64_t>(std::stoll(first));
        have = true;
      } catch (...) {
        have = false;
      }
    }
    if (!have) {
      reply("Unknown player or invalid clientId: " + first);
      return;
    }
    bans_.banCid(cid, reason);
    bans_.save();
    if (players_) {
      const std::string msg = reason.empty() ? "Client banned" : ("Client banned: " + reason);
      std::vector<player::Player*> to_kick;
      for (auto& [_, pl] : players_->all()) {
        if (pl.client_id == cid) to_kick.push_back(&pl);
      }
      for (auto* pl : to_kick) kickPlayer(*pl, msg);
    }
    reply("Banned clientId " + std::to_string(cid) +
          (reason.empty() ? "" : (" (" + reason + ")")));
    util::Logger::instance().notice(std::string(source_name), " ban-cid ", std::to_string(cid));
    return;
  }

  if (cmd == "unban-cid" || cmd == "pardon-cid") {
    std::string a = trim(std::string(args));
    if (a.empty()) {
      reply("Usage: /unban-cid <clientId>");
      return;
    }
    std::int64_t cid = 0;
    try {
      cid = static_cast<std::int64_t>(std::stoll(a));
    } catch (...) {
      reply("Invalid clientId: " + a);
      return;
    }
    if (bans_.unbanCid(cid)) {
      bans_.save();
      reply("Unbanned clientId " + std::to_string(cid));
      util::Logger::instance().notice(std::string(source_name), " unban-cid ", std::to_string(cid));
    } else {
      reply(std::to_string(cid) + " is not client-banned");
    }
    return;
  }

  if (cmd == "banlist") {
    std::ostringstream os;
    os << "Bans: names=" << bans_.nameCount() << " ips=" << bans_.ipCount()
       << " cids=" << bans_.cidCount();
    reply(os.str());
    auto names = bans_.listNames();
    if (!names.empty()) {
      std::ostringstream n;
      n << "Names: ";
      bool first = true;
      for (const auto& [k, r] : names) {
        if (!first) n << ", ";
        n << k;
        if (!r.empty()) n << "(" << r << ")";
        first = false;
      }
      reply(n.str());
    }
    auto ips = bans_.listIps();
    if (!ips.empty()) {
      std::ostringstream n;
      n << "IPs: ";
      bool first = true;
      for (const auto& [k, r] : ips) {
        if (!first) n << ", ";
        n << k;
        first = false;
      }
      reply(n.str());
    }
    auto cids = bans_.listCids();
    if (!cids.empty()) {
      std::ostringstream n;
      n << "CIDs: ";
      bool first = true;
      for (const auto& [k, r] : cids) {
        if (!first) n << ", ";
        n << k;
        first = false;
      }
      reply(n.str());
    }
    return;
  }

  if (cmd == "stop") {
    reply("Stopping the server...");
    util::Logger::instance().notice("stop issued by ", source_name);
    if (players_) {
      players_->broadcastText(std::string("\xc2\xa7") + "cServer stopping...");
    }
    // Avoid stop() under game_mutex_ (console queue); just request shutdown.
    running_ = false;
    return;
  }

  reply("Unknown command. Try /help");
  (void)args;
}

void Server::handleMove(player::Player& p, std::string_view buffer) {
  auto m = protocol::decodeMovePlayer(buffer);
  if (!m.ok || !p.spawned) return;
  if (p.death_ticks_ >= 0) return; // ignore movement while dead

  const float prev_y = p.y;
  const bool prev_ground = p.on_ground_;
  p.x = m.x;
  p.y = m.y;
  p.z = m.z;
  p.yaw = m.yaw;
  p.pitch = m.pitch;
  p.on_ground_ = m.on_ground;

  // Survival fall damage (simple): accumulate fall while airborne, apply on land
  if (p.gamemode != 1 && p.level) {
    if (!m.on_ground) {
      if (m.y < prev_y) p.fall_distance_ += (prev_y - m.y);
    } else {
      if (!prev_ground || p.fall_distance_ > 3.f) {
        // vanilla-ish: damage = floor(fall - 3)
        if (p.fall_distance_ > 3.f) {
          const float dmg = std::floor(p.fall_distance_ - 3.f);
          if (dmg > 0.f) damagePlayer(p, dmg, "fall");
        }
      }
      p.fall_distance_ = 0.f;
    }
  } else {
    p.fall_distance_ = 0.f;
  }

  plugin::MoveEvent mev;
  mev.username = p.username;
  mev.x = p.x;
  mev.y = p.y;
  mev.z = p.z;
  mev.yaw = p.yaw;
  mev.pitch = p.pitch;
  plugins_.fireMove(mev);

  // Relay movement to other clients that can see this player (PM broadcastMovement)
  if (players_ && p.entity_id != 0) {
    auto mp = protocol::encodeMovePlayer(p.entity_id, p.x, p.y, p.z, p.yaw, p.yaw, p.pitch, 0,
                                         p.on_ground_);
    for (auto& [_, other] : players_->all()) {
      if (!other.spawned || &other == &p || other.level != p.level) continue;
      if (!other.known_players.count(p.entity_id)) continue;
      players_->sendPacket(other, mp, false);
    }
  }

  if ((tick_counter_ % 10) == 0) {
    players_->sendChunksAround(p);
    syncEntitiesToPlayer(p);
    syncPlayersToPlayer(p);
  }
}

void Server::handleChunkRadius(player::Player& p, std::string_view buffer) {
  auto r = protocol::decodeRequestChunkRadius(buffer);
  if (!r.ok) return;
  p.chunk_radius = std::max(2, std::min(r.radius, 12));
  players_->sendPacket(p, protocol::encodeChunkRadiusUpdate(p.chunk_radius), true);
  players_->sendChunksAround(p);
}

void Server::handlePlayerAction(player::Player& p, std::string_view buffer) {
  auto a = protocol::decodePlayerAction(buffer);
  if (!a.ok) return;

  // Block plugins fire once from breakBlock/placeBlock — do NOT also fire here on STOP
  // (was double-firing Hello* logs / console spam on every dig).
  // DROP_ITEM is not a world block place/break; skip plugin block event here.

  if (a.action == protocol::ACTION_START_BREAK) {
    p.breaking_x = a.x;
    p.breaking_y = a.y;
    p.breaking_z = a.z;
    p.break_ticks = 0;
    // creative: instant break on START (no crack spam — laggy on s390x)
    if (p.gamemode == 1) {
      breakBlock(p, a.x, a.y, a.z);
      p.breaking_y = -1;
      return;
    }
    // survival: light crack only (no heavy broadcast path)
  } else if (a.action == protocol::ACTION_ABORT_BREAK) {
    p.breaking_y = -1;
  } else if (a.action == protocol::ACTION_STOP_BREAK) {
    // survival finish; creative may also send STOP after START — ignore if already broken
    if (p.gamemode != 1 || (p.level && p.level->getBlockId(a.x, a.y, a.z) != 0)) {
      breakBlock(p, a.x, a.y, a.z);
    }
    p.breaking_y = -1;
  } else if (a.action == protocol::ACTION_DROP_ITEM) {
    // Q key alternate path — throw held item (single)
    auto held = p.heldItem();
    if (!held.empty()) playerThrowHeld(p, held, false);
  } else if (a.action == protocol::ACTION_RESPAWN) {
    if (p.death_ticks_ >= 0 || p.health <= 0) respawnPlayer(p);
  }
}

void Server::handleDropItem(player::Player& p, std::string_view buffer) {
  auto d = protocol::decodeDropItem(buffer);
  if (!d.ok || !p.spawned) return;
  if (d.item.empty()) {
    // client sometimes sends air — throw held
    auto held = p.heldItem();
    if (!held.empty()) playerThrowHeld(p, held, false);
    return;
  }
  // full stack if counts match a full slot type, else single / packet count
  const bool full = d.item.count > 1;
  playerThrowHeld(p, d.item, full);
}

void Server::handleRemoveBlock(player::Player& p, std::string_view buffer) {
  auto r = protocol::decodeRemoveBlock(buffer);
  if (!r.ok) return;
  breakBlock(p, r.x, r.y, r.z);
}

void Server::broadcastChestLid(level::Level* level, int x, int y, int z, bool open) {
  if (!level || !players_) return;
  // PM ChestInventory: case1=1, case2=2 open / 0 close
  auto pk = protocol::encodeBlockEvent(x, y, z, 1, open ? 2 : 0);
  players_->broadcastNear(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                          static_cast<float>(z) + 0.5f, 64.f, pk, level, nullptr);
}

void Server::openChest(player::Player& p, int x, int y, int z) {
  if (!p.level || !p.spawned) return;
  if (p.level->getBlockId(x, y, z) != protocol::BLOCK_CHEST) return;

  // already open on this chest (or its pair) — ignore UseItem spam
  if (p.open_window_id != 0 && p.open_container_type == protocol::CONTAINER_TYPE_CHEST &&
      p.open_chest_y == y) {
    if ((p.open_chest_x == x && p.open_chest_z == z) ||
        (p.openChestPaired() && p.open_pair_x == x && p.open_pair_z == z)) {
      return;
    }
    // clicked partner of open left half
    auto* cur = p.level->getChest(x, y, z);
    if (cur && cur->isPaired() && p.open_chest_x == cur->pair_x && p.open_chest_z == cur->pair_z) {
      return;
    }
  }

  if (p.open_window_id != 0) closeContainer(p, true);

  auto& chest = p.level->getOrCreateChest(x, y, z);
  level::Level::ChestInv* pair = nullptr;
  if (chest.isPaired()) {
    pair = p.level->getChest(chest.pair_x, y, chest.pair_z);
    if (!pair || p.level->getBlockId(chest.pair_x, y, chest.pair_z) != protocol::BLOCK_CHEST) {
      p.level->unpairChest(x, y, z);
      pair = nullptr;
    }
  }

  // PM DoubleChestInventory order: larger (x + (z<<15)) is left
  int left_x = x, left_z = z, right_x = x, right_z = z;
  bool doubled = false;
  if (pair) {
    const auto key_self = static_cast<std::int64_t>(x) + (static_cast<std::int64_t>(z) << 15);
    const auto key_pair =
        static_cast<std::int64_t>(chest.pair_x) + (static_cast<std::int64_t>(chest.pair_z) << 15);
    if (key_pair > key_self) {
      left_x = chest.pair_x;
      left_z = chest.pair_z;
      right_x = x;
      right_z = z;
    } else {
      left_x = x;
      left_z = z;
      right_x = chest.pair_x;
      right_z = chest.pair_z;
    }
    doubled = true;
  }

  auto* left = p.level->getChest(left_x, y, left_z);
  auto* right = doubled ? p.level->getChest(right_x, y, right_z) : nullptr;
  if (!left) left = &p.level->getOrCreateChest(left_x, y, left_z);
  if (doubled && !right) right = &p.level->getOrCreateChest(right_x, y, right_z);

  const std::uint8_t wid = nextDynamicWindowId();
  const std::int16_t nslots = doubled ? static_cast<std::int16_t>(54) : static_cast<std::int16_t>(27);

  p.open_window_id = wid;
  p.open_container_type = protocol::CONTAINER_TYPE_CHEST;
  p.open_chest_x = left_x;
  p.open_chest_y = y;
  p.open_chest_z = left_z;
  if (doubled) {
    p.open_pair_x = right_x;
    p.open_pair_z = right_z;
  } else {
    p.open_pair_x = static_cast<int>(0x80000000);
    p.open_pair_z = static_cast<int>(0x80000000);
  }

  // Resync tile(s) then open — all immediate (avoid batch reorder after FullChunkData)
  auto send_bed = [&](int bx, int by, int bz) {
    auto* c = p.level->getChest(bx, by, bz);
    const bool paired = c && c->isPaired() &&
                        p.level->getBlockId(c->pair_x, by, c->pair_z) == protocol::BLOCK_CHEST;
    players_->sendPacket(
        p,
        protocol::encodeBlockEntityData(
            bx, by, bz,
            protocol::encodeChestSpawnNbt(bx, by, bz, paired, paired ? c->pair_x : 0,
                                          paired ? c->pair_z : 0)),
        true);
  };
  send_bed(x, y, z);
  if (doubled) send_bed(chest.pair_x, y, chest.pair_z);

  // ContainerOpen at clicked block coords (PM uses holder of inventory left side actually —
  // DoubleChestInventory holder is left; use left_x for open packet when double)
  const int ox = doubled ? left_x : x;
  const int oz = doubled ? left_z : z;
  players_->sendPacket(
      p,
      protocol::encodeContainerOpen(wid, protocol::CONTAINER_TYPE_CHEST, nslots, ox, y, oz, -1),
      true);

  std::vector<item::ItemStack> contents;
  contents.reserve(static_cast<std::size_t>(nslots));
  for (const auto& s : left->slots) contents.push_back(s);
  if (doubled && right) {
    for (const auto& s : right->slots) contents.push_back(s);
  }
  players_->sendPacket(p, protocol::encodeContainerSetContent(wid, contents), true);

  broadcastChestLid(p.level, x, y, z, true);
  if (doubled) broadcastChestLid(p.level, chest.pair_x, y, chest.pair_z, true);

  util::Logger::instance().info("openChest ", p.username, " wid=", static_cast<int>(wid), " @", x,
                                ",", y, ",", z, " slots=", static_cast<int>(nslots),
                                doubled ? " double" : " single");
}

void Server::openFurnace(player::Player& p, int x, int y, int z) {
  // v0.4.14: furnace removed — do not open PE furnace UI (client crash path)
  (void)x;
  (void)y;
  (void)z;
  if (players_) {
    players_->sendPacket(p, protocol::encodeTextSystem("Furnace is disabled on this server"), false);
  }
}

void Server::closeContainer(player::Player& p, bool send_close_pk) {
  if (p.open_window_id == 0) return;
  const int cx = p.open_chest_x, cy = p.open_chest_y, cz = p.open_chest_z;
  const int px = p.open_pair_x, pz = p.open_pair_z;
  const auto ctype = p.open_container_type;
  if (send_close_pk) {
    players_->sendPacket(p, protocol::encodeContainerClose(p.open_window_id), false);
  }
  p.open_window_id = 0;
  p.open_container_type = 0;
  p.open_chest_y = -1;
  p.open_pair_x = static_cast<int>(0x80000000);
  p.open_pair_z = static_cast<int>(0x80000000);
  // chest lid close (left + right if double)
  if (ctype == protocol::CONTAINER_TYPE_CHEST && p.level && cy >= 0) {
    broadcastChestLid(p.level, cx, cy, cz, false);
    if (px != static_cast<int>(0x80000000)) {
      broadcastChestLid(p.level, px, cy, pz, false);
    }
  }
}

void Server::handleContainerClose(player::Player& p, std::string_view buffer) {
  if (!p.spawned || buffer.size() < 2) return;
  const auto wid = static_cast<std::uint8_t>(buffer[1]);
  if (p.open_window_id != 0 && (wid == p.open_window_id || wid == 0)) {
    closeContainer(p, false); // client already closed UI
  }
  if (p.gamemode == 1) players_->sendCreativeContents(p);
  players_->sendInventory(p);
}

void Server::handleUseItem(player::Player& p, std::string_view buffer) {
  auto u = protocol::decodeUseItem(buffer);
  if (!u.ok) return;

  item::ItemStack held = u.item.empty() ? p.heldItem() : u.item;
  // face 0xff = air click / eat etc
  if (u.face == 0xff) return;

  // Right-click container blocks → open UI (PM onActivate)
  if (p.level) {
    const auto bid = p.level->getBlockId(u.x, u.y, u.z);
    if (bid == protocol::BLOCK_CHEST) {
      openChest(p, u.x, u.y, u.z);
      return;
    }
    if (bid == protocol::BLOCK_FURNACE || bid == protocol::BLOCK_BURNING_FURNACE) {
      openFurnace(p, u.x, u.y, u.z);
      return;
    }

    // Flint & steel: light nether portal on obsidian frame, else place fire
    if (held.id == item::ids::FLINT_STEEL) {
      bool used = false;
      if (bid == protocol::BLOCK_OBSIDIAN) {
        if (tryLightNetherPortal(p, u.x, u.y, u.z)) {
          used = true;
          players_->sendPacket(p, protocol::encodeTextSystem("Nether portal lit"), false);
        }
      }
      if (!used) {
        // place fire on face of solid block
        std::int32_t fx = u.x, fy = u.y, fz = u.z;
        protocol::faceOffset(u.face, fx, fy, fz);
        if (fy >= 0 && fy < 128 && p.level->getBlockId(fx, fy, fz) == 0) {
          p.level->setBlock(fx, fy, fz, protocol::BLOCK_FIRE, 0);
          broadcastBlockUpdate(p.level, fx, fy, fz, protocol::BLOCK_FIRE, 0, nullptr);
          used = true;
        }
      }
      if (used && p.gamemode != 1) {
        auto& h = p.heldItem();
        if (item::applyDurability(h, 1)) players_->sendInventory(p);
      }
      return;
    }
  }

  placeBlock(p, u.x, u.y, u.z, u.face, held);
}

void Server::handleMobEquipment(player::Player& p, std::string_view buffer) {
  auto e = protocol::decodeMobEquipment(buffer);
  if (!e.ok || !p.spawned) return;

  // PM: slot 0x28 / 0 / 255 => air (-1); else slot -= 9 for real inv index
  int inv_slot = static_cast<int>(e.slot);
  if (inv_slot == 0x28 || inv_slot == 0 || inv_slot == 255) {
    inv_slot = -1;
  } else {
    inv_slot -= 9;
  }

  const int selected =
      (e.selected_slot < 9) ? static_cast<int>(e.selected_slot) : p.selected_hotbar;

  if (p.gamemode == 1) {
    // Creative pick (PHP MOB_EQUIPMENT): put item into hotbar storage slot selectedSlot
    if (inv_slot == -1 && e.item.empty()) {
      // holding air: try find empty hotbar link, else select given slot
      bool found = false;
      for (int i = 0; i < 9; ++i) {
        int link = p.hotbar_link[static_cast<std::size_t>(i)];
        if (link < 0 || link >= static_cast<int>(p.inventory.size()) ||
            p.inventory[static_cast<std::size_t>(link)].empty()) {
          p.selected_hotbar = i;
          if (link < 0 || link >= static_cast<int>(p.inventory.size())) {
            p.hotbar_link[static_cast<std::size_t>(i)] = i;
          }
          found = true;
          break;
        }
      }
      if (!found && selected >= 0 && selected < 9) p.selected_hotbar = selected;
      return;
    }
    // Accept creative item into selected hotbar inv slot (0-8 storage = hotbar rows)
    if (selected < 0 || selected > 8) {
      players_->sendInventory(p);
      return;
    }
    // Allow empty (clear) or any creative-palette / free-form creative item
    p.selected_hotbar = selected;
    p.hotbar_link[static_cast<std::size_t>(selected)] = selected;
    p.inventory[static_cast<std::size_t>(selected)] =
        e.item.empty() ? item::ItemStack::air() : e.item;
    // Do NOT full-resync here: client already shows the pick; resync breaks empty-cell take
    return;
  }

  // Survival: switch held hotbar / link
  if (selected >= 0 && selected < 9) p.selected_hotbar = selected;
  if (inv_slot >= -1 && inv_slot < static_cast<int>(p.inventory.size()) && selected >= 0 &&
      selected < 9) {
    p.hotbar_link[static_cast<std::size_t>(selected)] = inv_slot;
  }
}

namespace {
// wall-clock seconds (for transaction timeout)
double nowSec() {
  using clock = std::chrono::system_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// Forward: chest slots need Server+Level; use free helpers that only touch Player for inv/armor.
// Chest slot r/w is done inside handleContainerSetSlot with level pointer.

item::ItemStack getPlayerSlot(const player::Player& p, std::uint8_t window_id, std::int16_t slot) {
  if (window_id == protocol::WINDOW_INVENTORY) {
    if (slot < 0 || slot >= static_cast<std::int16_t>(p.inventory.size())) return item::ItemStack::air();
    return p.inventory[static_cast<std::size_t>(slot)];
  }
  if (window_id == protocol::WINDOW_ARMOR) {
    if (slot < 0 || slot >= 4 || p.armor.size() < 4) return item::ItemStack::air();
    return p.armor[static_cast<std::size_t>(slot)];
  }
  return item::ItemStack::air();
}

void setPlayerSlot(player::Player& p, std::uint8_t window_id, std::int16_t slot,
                   const item::ItemStack& item) {
  if (window_id == protocol::WINDOW_INVENTORY) {
    if (slot < 0 || slot >= static_cast<std::int16_t>(p.inventory.size())) return;
    p.inventory[static_cast<std::size_t>(slot)] = item.empty() ? item::ItemStack::air() : item;
    return;
  }
  if (window_id == protocol::WINDOW_ARMOR) {
    if (p.armor.size() < 4) p.armor.assign(4, item::ItemStack::air());
    if (slot < 0 || slot >= 4) return;
    p.armor[static_cast<std::size_t>(slot)] = item.empty() ? item::ItemStack::air() : item;
  }
}

// PM SimpleTransactionGroup::matchItems: have from sources must balance need from targets
bool tryExecutePendingTxs(player::Player& p) {
  if (p.pending_txs.empty()) return false;

  // Verify each source still matches inventory (or creative: allow mismatch for free items)
  struct Need {
    item::ItemStack item;
  };
  std::vector<item::ItemStack> need;
  std::vector<item::ItemStack> have;

  for (const auto& tx : p.pending_txs) {
    auto check = getPlayerSlot(p, tx.window_id, tx.slot);
    if (!check.equalsStack(tx.source)) {
      // creative: server may already have been updated by MobEquipment; still allow if target ok
      if (p.gamemode != 1) return false;
    }
    if (!tx.target.empty()) need.push_back(tx.target);
    if (!tx.source.empty()) have.push_back(tx.source);
  }

  // Balance need vs have by type+count (PM deepEquals without NBT)
  for (std::size_t i = 0; i < need.size(); ++i) {
    for (std::size_t j = 0; j < have.size(); ++j) {
      if (have[j].empty()) continue;
      if (!need[i].sameType(have[j])) continue;
      const int amount =
          std::min(static_cast<int>(need[i].count), static_cast<int>(have[j].count));
      need[i].count = static_cast<std::uint8_t>(need[i].count - amount);
      have[j].count = static_cast<std::uint8_t>(have[j].count - amount);
      if (need[i].count == 0) break;
    }
  }
  for (const auto& n : need)
    if (!n.empty() && p.gamemode != 1) return false;
  for (const auto& h : have)
    if (!h.empty() && p.gamemode != 1) return false;

  // Creative: always apply pending targets (client is authority for inventory UI)
  // Survival: only if fully balanced (above)
  if (p.gamemode != 1) {
    for (const auto& n : need)
      if (!n.empty()) return false;
    for (const auto& h : have)
      if (!h.empty()) return false;
  }

  for (const auto& tx : p.pending_txs) {
    setPlayerSlot(p, tx.window_id, tx.slot, tx.target);
  }
  p.pending_txs.clear();
  p.pending_tx_time = 0;
  return true;
}
} // namespace

void Server::handleContainerSetSlot(player::Player& p, std::string_view buffer) {
  if (!p.spawned) return;
  auto d = protocol::decodeContainerSetSlot(buffer);
  if (!d.ok || d.slot < 0) return;

  // Creative window 0x79 is server→client only content list; ignore client writes
  if (d.window_id == protocol::WINDOW_CREATIVE) {
    return;
  }

  // Dynamic container window: client UI authority while open
  if (p.open_window_id != 0 && d.window_id == p.open_window_id && p.level &&
      p.open_chest_y >= 0) {
    if (p.open_container_type == protocol::CONTAINER_TYPE_FURNACE) {
      auto* fur = p.level->getFurnace(p.open_chest_x, p.open_chest_y, p.open_chest_z);
      if (!fur) fur = &p.level->getOrCreateFurnace(p.open_chest_x, p.open_chest_y, p.open_chest_z);
      if (d.slot >= 0 && d.slot < static_cast<std::int16_t>(fur->slots.size())) {
        fur->slots[static_cast<std::size_t>(d.slot)] =
            d.item.empty() ? item::ItemStack::air() : item::sanitizeSlot(d.item);
        // client already applied slot locally — only mark dirty for autosave.
        // Never set need_push_slots here (echo SetSlot crashes some 0.14 clients).
        fur->dirty = true;
        fur->need_push_slots = false;
      }
    } else {
      // chest / double chest: open_chest_* = left half; slots 0..26 left, 27..53 right
      const int slot = static_cast<int>(d.slot);
      if (p.openChestPaired()) {
        if (slot >= 0 && slot < 27) {
          auto* left = p.level->getChest(p.open_chest_x, p.open_chest_y, p.open_chest_z);
          if (!left) left = &p.level->getOrCreateChest(p.open_chest_x, p.open_chest_y, p.open_chest_z);
          left->slots[static_cast<std::size_t>(slot)] =
              d.item.empty() ? item::ItemStack::air() : d.item;
          left->dirty = true;
        } else if (slot >= 27 && slot < 54) {
          auto* right = p.level->getChest(p.open_pair_x, p.open_chest_y, p.open_pair_z);
          if (!right)
            right = &p.level->getOrCreateChest(p.open_pair_x, p.open_chest_y, p.open_pair_z);
          right->slots[static_cast<std::size_t>(slot - 27)] =
              d.item.empty() ? item::ItemStack::air() : d.item;
          right->dirty = true;
        }
      } else {
        auto* chest = p.level->getChest(p.open_chest_x, p.open_chest_y, p.open_chest_z);
        if (!chest)
          chest = &p.level->getOrCreateChest(p.open_chest_x, p.open_chest_y, p.open_chest_z);
        if (slot >= 0 && slot < static_cast<int>(chest->slots.size())) {
          chest->slots[static_cast<std::size_t>(slot)] =
              d.item.empty() ? item::ItemStack::air() : d.item;
          chest->dirty = true;
        }
      }
    }
    return;
  }

  if (d.window_id != protocol::WINDOW_INVENTORY && d.window_id != protocol::WINDOW_ARMOR) {
    return;
  }

  if (d.window_id == protocol::WINDOW_INVENTORY &&
      d.slot >= static_cast<std::int16_t>(p.inventory.size())) {
    players_->sendInventory(p);
    return;
  }
  if (d.window_id == protocol::WINDOW_ARMOR && d.slot >= 4) return;

  const auto source = getPlayerSlot(p, d.window_id, d.slot);
  const auto target = d.item.empty() ? item::ItemStack::air() : d.item;

  // No-op local echo
  if (source.equalsStack(target)) return;

  // Expire stale transaction group (~8s like PM)
  const double t = nowSec();
  if (!p.pending_txs.empty() && p.pending_tx_time > 0 && (t - p.pending_tx_time) > 8.0) {
    p.pending_txs.clear();
    players_->sendInventory(p);
  }
  if (p.pending_txs.empty()) p.pending_tx_time = t;

  // Replace same-slot pending entry (PM addTransaction)
  bool replaced = false;
  for (auto& tx : p.pending_txs) {
    if (tx.window_id == d.window_id && tx.slot == d.slot) {
      tx.source = source;
      tx.target = target;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    player::Player::SlotTx tx;
    tx.window_id = d.window_id;
    tx.slot = d.slot;
    tx.source = source;
    tx.target = target;
    p.pending_txs.push_back(std::move(tx));
  }

  // Hotbar link hint from packet (optional)
  if (d.window_id == protocol::WINDOW_INVENTORY && d.hotbar_slot >= 0 && d.hotbar_slot < 9) {
    p.hotbar_link[static_cast<std::size_t>(d.hotbar_slot)] = d.slot;
    p.selected_hotbar = d.hotbar_slot;
  }

  if (tryExecutePendingTxs(p)) {
    // Applied move/swap — do NOT full-resync (that re-fills source = "copy" bug)
    return;
  }

  // Creative single-slot write: allow free set without balanced pair
  // (empty cell take from creative often arrives as one SetSlot after MobEquipment)
  if (p.gamemode == 1 && p.pending_txs.size() == 1) {
    setPlayerSlot(p, d.window_id, d.slot, target);
    p.pending_txs.clear();
    p.pending_tx_time = 0;
    return;
  }

  // Incomplete group: wait for more SetSlots (move needs clear-source + set-dest)
  // If too many without execute, resync to unstick
  if (p.pending_txs.size() >= 8) {
    p.pending_txs.clear();
    p.pending_tx_time = 0;
    players_->sendInventory(p);
  }
}

void Server::handleCraftingEvent(player::Player& p, std::string_view buffer) {
  auto c = protocol::decodeCraftingEvent(buffer);
  if (!c.ok) return;
  if (c.output.empty()) return;

  // Accept client craft result into inventory (trust client for stub; creative unlimited)
  auto result = c.output[0];
  if (result.empty()) return;

  if (p.gamemode != 1) {
    // consume inputs best-effort
    for (const auto& ing : c.input) {
      if (ing.empty()) continue;
      int need = ing.count;
      for (auto& slot : p.inventory) {
        if (slot.id == ing.id && (ing.damage < 0 || slot.damage == ing.damage)) {
          int take = std::min(need, static_cast<int>(slot.count));
          slot.count = static_cast<std::uint8_t>(slot.count - take);
          if (slot.count == 0) slot = item::ItemStack::air();
          need -= take;
          if (need <= 0) break;
        }
      }
    }
  }

  bool placed = false;
  for (auto& slot : p.inventory) {
    if (slot.id == result.id && slot.damage == result.damage && slot.count < 64) {
      int space = 64 - slot.count;
      int add = std::min(space, static_cast<int>(result.count));
      slot.count = static_cast<std::uint8_t>(slot.count + add);
      result.count = static_cast<std::uint8_t>(result.count - add);
      if (result.count == 0) {
        placed = true;
        break;
      }
    }
  }
  if (!placed || result.count > 0) {
    for (auto& slot : p.inventory) {
      if (slot.empty()) {
        slot = result;
        placed = true;
        break;
      }
    }
  }
  players_->sendInventory(p);
  util::Logger::instance().info(p.username, " crafted id=", result.id, " x",
                                static_cast<int>(c.output[0].count));
}

void Server::handleAnimate(player::Player& p, std::string_view buffer) {
  auto a = protocol::decodeAnimate(buffer);
  if (!a.ok || !p.spawned || p.death_ticks_ >= 0) return;
  if (p.entity_id == 0 || !players_) return;
  // PM: rebroadcast Animate to viewers with this player's runtime eid (not client 0)
  auto pk = protocol::encodeAnimate(a.action, p.entity_id);
  for (auto& [_, other] : players_->all()) {
    if (!other.spawned || &other == &p || other.level != p.level) continue;
    if (!other.known_players.count(p.entity_id)) continue;
    players_->sendPacket(other, pk, false);
  }
}

void Server::handleInteract(player::Player& p, std::string_view buffer) {
  auto i = protocol::decodeInteract(buffer);
  if (!i.ok || !p.spawned || p.death_ticks_ >= 0) return;
  // PM InteractPacket: 1=RIGHT_CLICK (mount/use/shear), 2=LEFT_CLICK (attack)

  auto* e = entities_.get(i.target);
  player::Player* victim = nullptr;
  if (!e || e->closed || e->level != p.level) {
    // Target may be another player's runtime entity_id (not in entities_ map).
    if (i.action != 2 || !players_) return;
    for (auto& [_, pl] : players_->all()) {
      if (pl.spawned && pl.entity_id == i.target && pl.level == p.level && &pl != &p &&
          pl.death_ticks_ < 0) {
        victim = &pl;
        break;
      }
    }
    if (!victim) return;
    e = nullptr;
  } else if (e->kind == entity::EntityKind::ItemDrop) {
    return;
  }

  // reach check (~6 blocks survival, ~8 creative)
  const float tx = victim ? victim->x : e->x;
  const float ty = victim ? victim->y : e->y;
  const float tz = victim ? victim->z : e->z;
  const float dx = tx - p.x;
  const float dy = ty - p.y;
  const float dz = tz - p.z;
  const float dist2 = dx * dx + dy * dy + dz * dz;
  const float reach = (p.gamemode == 1) ? 8.f : 6.f;
  if (dist2 > reach * reach) return;

  // Right-click: sheep shear with shears (PM Sheep::shear) — mobs only
  if (i.action == 1) {
    if (!e || e->kind != entity::EntityKind::Sheep || e->sheared) return;
    auto& held = p.heldItem();
    if (held.id != item::ids::SHEARS) return;

    e->sheared = true;
    auto meta = protocol::encodeSheepMetadata(e->sheep_color, true);
    // SetEntityData with only COLOR_INFO update is fine; full sheep meta works too
    auto sed = protocol::encodeSetEntityData(e->eid, meta);
    players_->broadcastNear(e->x, e->y, e->z, 64.f, sed, e->level, nullptr);

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> cnt(1, 3);
    const int n = cnt(rng);
    dropItemInWorld(e->level, e->x, e->y + 1.0f, e->z,
                    item::ItemStack::of(item::ids::WOOL, static_cast<std::uint8_t>(n),
                                        e->sheep_color),
                    0.f, 0.2f, 0.f, 10);

    if (p.gamemode != 1) {
      if (item::applyDurability(held, 1)) players_->sendInventory(p);
    }
    return;
  }

  // Only left-click damages — right-click must NOT despawn mobs.
  if (i.action != 2) return;

  // basic weapon damage table (PM subset)
  float dmg = 1.f;
  auto& held = p.heldItem();
  if (p.gamemode == 1) {
    dmg = 20.f;
  } else {
    switch (held.id) {
      case 268: case 272: case 267: case 283: case 276: // swords wood/stone/iron/gold/diamond
        dmg = (held.id == 276) ? 7.f : (held.id == 267 || held.id == 283) ? 6.f
              : (held.id == 272)                                       ? 5.f
                                                                       : 4.f;
        break;
      case 271: case 275: case 258: case 286: case 279: // axes
        dmg = 3.f;
        break;
      default:
        dmg = 1.f;
        break;
    }
    // PM: sword/hoe +1 durability on entity; pick/axe/shovel +2
    if (item::isToolItem(held.id)) {
      int wear = 1;
      auto k = item::toolKind(held.id);
      if (k == item::ToolKind::Pickaxe || k == item::ToolKind::Axe || k == item::ToolKind::Shovel)
        wear = 2;
      if (item::applyDurability(held, wear)) players_->sendInventory(p);
    }
  }

  // Player-vs-player (pvp=off blocks; creative victims still invulnerable in damagePlayer)
  if (victim) {
    if (!cfg_.pvp) return;
    // Ensure viewers see attacker swing even if client omitted Animate this tick
    if (p.entity_id != 0) {
      auto swing = protocol::encodeAnimate(protocol::ANIMATE_SWING_ARM, p.entity_id);
      for (auto& [_, other] : players_->all()) {
        if (!other.spawned || &other == &p || other.level != p.level) continue;
        if (!other.known_players.count(p.entity_id)) continue;
        players_->sendPacket(other, swing, false);
      }
    }
    if (damagePlayer(*victim, dmg, "player")) {
      // PM Living::knockBack after successful attack damage
      knockbackPlayer(*victim, p.x, p.z, 0.4f);
    }
    return;
  }
  if (!e) return;

  e->health -= dmg;
  const auto hurt_pk = protocol::encodeEntityEvent(e->eid, protocol::ENTITY_EVENT_HURT);
  players_->broadcastNear(e->x, e->y, e->z, 64.f, hurt_pk, e->level, nullptr);

  // light knockback away from attacker
  {
    float kx = dx, kz = dz;
    const float len = std::sqrt(kx * kx + kz * kz);
    if (len > 0.001f) {
      kx /= len;
      kz /= len;
    } else {
      kx = 0.f;
      kz = 1.f;
    }
    e->motion_x = kx * 0.4f;
    e->motion_y = 0.35f;
    e->motion_z = kz * 0.4f;
    e->on_ground = false;
    auto mp = protocol::encodeMoveEntity(e->eid, e->x, e->y, e->z, e->yaw, e->yaw, 0.f);
    players_->broadcastNear(e->x, e->y, e->z, 64.f, mp, e->level, nullptr);
  }

  if (e->health <= 0.f) {
    const auto death_pk = protocol::encodeEntityEvent(e->eid, protocol::ENTITY_EVENT_DEATH);
    players_->broadcastNear(e->x, e->y, e->z, 64.f, death_pk, e->level, nullptr);
    // drop a little loot then remove
    if (e->kind == entity::EntityKind::Pig)
      dropItemInWorld(e->level, e->x, e->y + 0.5f, e->z, item::ItemStack::of(319, 1), 0, 0.2f, 0,
                      10); // raw porkchop-ish; 319 = raw porkchop PE
    else if (e->kind == entity::EntityKind::Cow)
      dropItemInWorld(e->level, e->x, e->y + 0.5f, e->z, item::ItemStack::of(363, 1), 0, 0.2f, 0,
                      10); // raw beef
    else if (e->kind == entity::EntityKind::Chicken)
      dropItemInWorld(e->level, e->x, e->y + 0.5f, e->z, item::ItemStack::of(365, 1), 0, 0.2f, 0,
                      10); // raw chicken
    else if (e->kind == entity::EntityKind::Sheep && !e->sheared)
      dropItemInWorld(e->level, e->x, e->y + 0.5f, e->z,
                      item::ItemStack::of(item::ids::WOOL, 1, e->sheep_color), 0, 0.2f, 0, 10);
    const auto eid = e->eid;
    const float ex = e->x, ey = e->y, ez = e->z;
    auto* elvl = e->level;
    entities_.remove(eid);
    auto rm = protocol::encodeRemoveEntity(eid);
    players_->broadcastNear(ex, ey, ez, 64.f, rm, elvl, nullptr);
    for (auto& [_, pl] : players_->all()) pl.known_entities.erase(eid);
  }
}

namespace {
// PM Network::processBatch: zlib_decode payload, then int32 len + packet bytes.
// Each inner packet is the full DataPacket buffer: [pid][fields...] (NO 0x8e inside batch).
// Note: PM getPacket(ord($buf[1])) is because setBuffer skips channel byte on some forks;
// Genisys 0.14 DataPacket::reset uses chr(NETWORK_ID) only — so ord($buf[0]) is pid.
// We try pid at [0]; if unknown and size>1, try [1] (PM RakLibInterface style).
std::string inflateZlib(std::string_view compressed) {
  if (compressed.empty()) return {};
  z_stream strm{};
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
  strm.avail_in = static_cast<uInt>(std::min(compressed.size(), static_cast<std::size_t>(0xFFFFFFFFu)));
  // 15+32 = zlib/gzip auto header
  if (inflateInit2(&strm, 15 + 32) != Z_OK) {
    // try raw deflate
    strm = {};
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    strm.avail_in = static_cast<uInt>(std::min(compressed.size(), static_cast<std::size_t>(0xFFFFFFFFu)));
    if (inflateInit2(&strm, -15) != Z_OK) return {};
  }
  std::string out;
  out.resize(std::min<std::size_t>(compressed.size() * 8 + 4096, 64 * 1024 * 1024));
  int ret = Z_OK;
  std::size_t total = 0;
  while (ret == Z_OK) {
    if (total >= out.size()) {
      if (out.size() >= 64 * 1024 * 1024) {
        inflateEnd(&strm);
        return {};
      }
      out.resize(std::min(out.size() * 2, static_cast<std::size_t>(64 * 1024 * 1024)));
    }
    strm.next_out = reinterpret_cast<Bytef*>(&out[total]);
    strm.avail_out = static_cast<uInt>(out.size() - total);
    ret = inflate(&strm, Z_NO_FLUSH);
    total = out.size() - strm.avail_out;
    if (ret == Z_STREAM_END) break;
    if (ret == Z_BUF_ERROR && strm.avail_in == 0) break;
    if (ret != Z_OK && ret != Z_BUF_ERROR) {
      inflateEnd(&strm);
      return {};
    }
  }
  inflateEnd(&strm);
  out.resize(total);
  return out;
}
} // namespace

void Server::dispatchMcpePacket(player::Player& p, std::string_view buffer) {
  if (buffer.empty()) return;
  const auto pid = static_cast<std::uint8_t>(buffer[0]);
  switch (pid) {
    case protocol::LOGIN_PACKET:
      handleLogin(p, buffer);
      break;
    case protocol::TEXT_PACKET:
      handleText(p, buffer);
      break;
    case protocol::MOVE_PLAYER_PACKET:
      handleMove(p, buffer);
      break;
    case protocol::REQUEST_CHUNK_RADIUS_PACKET:
      handleChunkRadius(p, buffer);
      break;
    case protocol::PLAYER_ACTION_PACKET:
      handlePlayerAction(p, buffer);
      break;
    case protocol::REMOVE_BLOCK_PACKET:
      handleRemoveBlock(p, buffer);
      break;
    case protocol::USE_ITEM_PACKET:
      handleUseItem(p, buffer);
      break;
    case protocol::MOB_EQUIPMENT_PACKET:
      handleMobEquipment(p, buffer);
      break;
    case protocol::CONTAINER_SET_SLOT_PACKET:
      handleContainerSetSlot(p, buffer);
      break;
    case protocol::CONTAINER_CLOSE_PACKET:
      handleContainerClose(p, buffer);
      break;
    case protocol::CRAFTING_EVENT_PACKET:
      handleCraftingEvent(p, buffer);
      break;
    case protocol::INTERACT_PACKET:
      handleInteract(p, buffer);
      break;
    case protocol::DROP_ITEM_PACKET:
      handleDropItem(p, buffer);
      break;
    case protocol::ANIMATE_PACKET:
      handleAnimate(p, buffer);
      break;
    case protocol::BATCH_PACKET:
      handleBatch(p, buffer);
      break;
    default: {
      if (pid >= 0x8f) {
        std::ostringstream os;
        os << "MCPE 0x" << std::hex << static_cast<unsigned>(pid) << std::dec << " from "
           << (p.username.empty() ? p.endpoint.address : p.username) << " len=" << buffer.size();
        util::Logger::instance().info(os.str());
      }
      break;
    }
  }
}

void Server::handleBatch(player::Player& p, std::string_view buffer) {
  // BatchPacket: [0x92][int32 payload_len][zlib_payload]
  try {
    binary::BinaryStream in{std::string(buffer)};
    if (in.getByte() != protocol::BATCH_PACKET) return;
    const auto size = in.getInt();
    if (size <= 0 || size > 64 * 1024 * 1024) {
      util::Logger::instance().warning("Batch bad size=", size, " from ", p.endpoint.address);
      return;
    }
    auto compressed = in.get(static_cast<std::size_t>(size));
    if (static_cast<int>(compressed.size()) != size) {
      // remaining buffer may be the whole payload without length (defensive)
      compressed = std::string(buffer.substr(1));
    }
    auto inflated = inflateZlib(compressed);
    if (inflated.empty()) {
      // try whole remainder after pid as compressed (some clients)
      inflated = inflateZlib(buffer.substr(1));
    }
    if (inflated.empty()) {
      util::Logger::instance().warning("Batch zlib fail from ", p.endpoint.address,
                                       " compressed=", compressed.size());
      return;
    }
    util::Logger::instance().info("Batch inflate ", inflated.size(), "B from ",
                                  p.username.empty() ? p.endpoint.address : p.username);
    // stream of: int32 len + packet
    binary::BinaryStream stream{std::move(inflated)};
    int count = 0;
    while (!stream.feof() && count < 512) {
      const auto rem = stream.buffer().size() - stream.offset();
      if (rem < 4) break;
      const auto pk_len = stream.getInt();
      if (pk_len <= 0 || pk_len > 8 * 1024 * 1024) break;
      if (stream.buffer().size() - stream.offset() < static_cast<std::size_t>(pk_len)) break;
      auto pk_buf = stream.get(static_cast<std::size_t>(pk_len));
      if (pk_buf.empty()) break;
      // PM: pid at buf[1] when wire has 0x8e inside batch; bare buffer uses [0]
      if (static_cast<std::uint8_t>(pk_buf[0]) == protocol::MCPE_RAKNET_CUSTOM_PACKET_ID &&
          pk_buf.size() > 1) {
        dispatchMcpePacket(p, std::string_view(pk_buf).substr(1));
      } else {
        dispatchMcpePacket(p, pk_buf);
      }
      ++count;
    }
  } catch (const std::exception& e) {
    util::Logger::instance().warning("Batch parse fail: ", e.what());
  } catch (...) {
    util::Logger::instance().warning("Batch parse fail");
  }
}

void Server::handleMcpePayload(const raklib::Endpoint& ep, const raklib::EncapsulatedPacket& pk) {
  if (pk.buffer.empty()) return;
  auto* p = players_ ? players_->get(ep) : nullptr;
  if (!p) {
    p = &players_->getOrCreate(ep, 0);
  }

  // 0.14 wire: [0x8e][pid][...] — strip custom packet id (RakLibInterface getPacket)
  std::string_view buf = pk.buffer;
  if (static_cast<std::uint8_t>(buf[0]) == protocol::MCPE_RAKNET_CUSTOM_PACKET_ID) {
    if (buf.size() < 2) return;
    buf = buf.substr(1);
  }

  const auto pid = static_cast<std::uint8_t>(buf[0]);
  if (pid == protocol::LOGIN_PACKET || pid == protocol::BATCH_PACKET) {
    util::Logger::instance().info("MCPE pid=0x",
                                  [&] {
                                    std::ostringstream os;
                                    os << std::hex << static_cast<unsigned>(pid);
                                    return os.str();
                                  }(),
                                  " len=", buf.size(), " from ", ep.address, ":", ep.port);
  }
  dispatchMcpePacket(*p, buf);
}

void Server::saveEverything(bool force_chunks) {
  if (players_) players_->saveAllPlayers();
  levels_.saveAll(force_chunks);
}

void Server::tickAutosave() {
  if (cfg_.autosave_seconds <= 0) return;
  if (autosave_ticks_left_ <= 0) {
    autosave_ticks_left_ = cfg_.autosave_seconds * 20; // 20 tps
  }
  if (--autosave_ticks_left_ > 0) return;
  autosave_ticks_left_ = cfg_.autosave_seconds * 20;
  saveEverything(false);
}

void Server::start() {
  auto& log = util::Logger::instance();
  log.notice("MPMPESCoreCpp ", "0.4.24", " (plugins-c-py-perf)");
  log.info("Target protocol ", cfg_.protocol, " (MCPE ", cfg_.version_name, ")");
  log.info("Binding UDP ", cfg_.bind, ":", cfg_.port);
  log.info("Batch: compression-level=", cfg_.network_compression_level,
           " threshold-kb=", cfg_.network_batch_threshold_kb,
           " always-drop-on-break=", cfg_.always_drop_on_break ? "on" : "off",
           " autosave=", cfg_.autosave_seconds, "s");
  log.info("Default gamemode (new players): ", cfg_.gamemode == 1 ? "creative(1)" : "survival(0)",
           "  [server.properties gamemode=]");
  log.info("PvP: ", cfg_.pvp ? "on" : "off", "  level=", cfg_.level_name, " type=", cfg_.level_type);

  level::LevelSettings defs;
  defs.name = cfg_.level_name.empty() ? "world" : cfg_.level_name;
  defs.generator = level::parseGenerator(cfg_.level_type);
  defs.seed = static_cast<std::int32_t>(server_id_ & 0x7fffffff);
  defs.gamemode = cfg_.gamemode;
  levels_.loadWorldsFile("worlds.txt", defs);

  // seed mobs in non-void worlds
  for (auto& [name, lvl] : levels_.all()) {
    entities_.seedWorld(*lvl, 6);
    log.info("Seeded mobs in ", name, " total entities=", entities_.all().size());
  }

  if (cfg_.enable_plugins) {
    plugins_.loadAll(cfg_.plugins_dir);
    plugins_.enableAll();
  } else {
    log.info("Plugins disabled");
  }

  for (auto& [name, lvl] : levels_.all()) {
    plugin::WorldLoadEvent wev;
    wev.name = name;
    wev.generator = static_cast<std::int32_t>(lvl->generator());
    wev.seed = lvl->settings().seed;
    plugins_.fireWorldLoad(wev);
  }

  socket_.bind(cfg_.bind, cfg_.port);
  sessions_holder_ = std::make_unique<raklib::SessionManager>(socket_, server_id_);
  sessions_ = sessions_holder_.get();
  sessions_->setPort(cfg_.port);
  sessions_->setName(buildRaklibName());

  players_ = std::make_unique<player::PlayerManager>(*sessions_);
  players_->setDefaultLevel(levels_.defaultLevel());
  players_->setNetworkBatch(cfg_.network_compression_level, cfg_.network_batch_threshold_kb);
  players_->setPlayersDataPath("players");
  autosave_ticks_left_ = cfg_.autosave_seconds > 0 ? cfg_.autosave_seconds * 20 : 0;

  ops_.setPath("ops.txt");
  ops_.load();
  log.info("Operators loaded: ", ops_.size(), " from ops.txt");
  bans_.setPath("bans.txt");
  bans_.load();
  log.info("Bans loaded: names=", bans_.nameCount(), " ips=", bans_.ipCount(),
           " cids=", bans_.cidCount(), " from bans.txt");

  sessions_->setHandlers(
      [this](const raklib::Endpoint& ep, const raklib::EncapsulatedPacket& pk) {
        handleMcpePayload(ep, pk);
      },
      [this](const raklib::Endpoint& ep, std::int64_t client_id) {
        util::Logger::instance().notice("Session open ", ep.address, ":", ep.port,
                                        " id=", client_id);
        auto& pl = players_->getOrCreate(ep, client_id);
        pl.state = player::PlayerState::Connected;
        plugin::SessionOpenEvent ev;
        ev.address = ep.address;
        ev.port = ep.port;
        ev.client_id = client_id;
        plugins_.fireSessionOpen(ev);
      },
      [this](const raklib::Endpoint& ep, std::string_view reason) {
        util::Logger::instance().notice("Session close ", ep.address, ":", ep.port, " (", reason,
                                        ")");
        if (auto* pl = players_->get(ep)) {
          if (!pl->username.empty()) {
            plugin::PlayerQuitEvent qev;
            qev.username = pl->username;
            qev.reason = std::string(reason);
            plugins_.firePlayerQuit(qev);
            players_->broadcastText(std::string("\xc2\xa7") + "c[-] " + pl->username);
          }
          // Remove world body for others before PlayerList remove + map erase
          broadcastPlayerDespawn(*pl);
          players_->remove(ep);
        }
        plugin::SessionCloseEvent ev;
        ev.address = ep.address;
        ev.port = ep.port;
        ev.reason = std::string(reason);
        plugins_.fireSessionClose(ev);
        if (sessions_) sessions_->setName(buildRaklibName());
      });

  running_ = true;
  g_server = this;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // Dedicated network thread: drain UDP + RakLib session update under game_mutex_
  network_thread_ = std::thread([this] { networkThreadMain(); });
  network_thread_started_ = true;

  console_thread_ = std::thread([this] { consoleThreadMain(); });
  console_thread_started_ = true;

  log.notice("Server started on ", cfg_.bind, ":", cfg_.port);
  log.info("Console ready — type commands (e.g. help, stop, op <name>)");
  log.info("MOTD: ", cfg_.motd);
  log.info("Default world: ", levels_.defaultLevel() ? levels_.defaultLevel()->name() : "?");
  log.info("Worlds: ", levels_.all().size(), " Entities: ", entities_.all().size());
  log.info("Plugins: ", plugins_.size(), " from ", cfg_.plugins_dir);
  log.info("Features: dig/place, terrain gens, nether/end, portal, chest UI, save, batch");

  plugin::ServerStartEvent sev;
  sev.motd = cfg_.motd;
  sev.port = cfg_.port;
  plugins_.fireServerStart(sev);
}

void Server::networkThreadMain() {
  using namespace std::chrono_literals;
  // Low-latency network loop:
  // - poll UDP with short timeout instead of fixed 2ms sleep
  // - if packets arrived, tick again immediately (no sleep)
  int idle_spins = 0;
  while (running_) {
    // Wait up to 1ms for inbound UDP when idle (reduces wake latency vs sleep_for).
    if (socket_.isOpen()) {
      socket_.waitReadable(idle_spins > 0 ? 1 : 0);
    }
    int handled = 0;
    {
      std::lock_guard<std::mutex> lock(game_mutex_);
      if (sessions_) handled = sessions_->tick();
    }
    if (handled > 0) {
      idle_spins = 0;
      continue; // hot path: drain again ASAP
    }
    // Idle: tiny yield so we don't peg a full core when nobody is online.
    ++idle_spins;
    if (idle_spins > 8) {
      std::this_thread::sleep_for(1ms);
      idle_spins = 0;
    }
  }
}

void Server::stop() {
  if (!running_.exchange(false)) return;
  util::Logger::instance().notice("Stopping...");
  if (console_thread_.joinable()) {
    // wake console reader by closing stdin is hard; thread exits on running_ false after next line
    // or EOF — join may block if stdin blocked; detach if needed after short wait
  }
  if (network_thread_.joinable()) network_thread_.join();
  network_thread_started_ = false;
  if (console_thread_.joinable()) {
    // non-blocking: if console blocked on getline, detach so stop completes
    console_thread_.detach();
    console_thread_started_ = false;
  }
  ops_.save();
  bans_.save();
  {
    std::lock_guard<std::mutex> lock(game_mutex_);
    saveEverything(true);
  }
  plugins_.disableAll();
}

void Server::runLoop() {
  using namespace std::chrono_literals;
  auto next = std::chrono::steady_clock::now();
  while (running_) {
    {
      // Game tick: worlds + entities + pickups (network thread holds mutex only briefly)
      std::lock_guard<std::mutex> lock(game_mutex_);
      processConsoleQueue();
      levels_.tickAll();
      tickFurnaces();
      tickEntities();
      tickPlayerPortals();
      tickPlayerDamage();
      tickAutosave();
      ++tick_counter_;
      if ((tick_counter_ % 20) == 0 && players_) {
        for (auto& [_, pl] : players_->all()) {
          if (pl.spawned) players_->sendChunksAround(pl);
        }
        if (sessions_) sessions_->setName(buildRaklibName());
      }
    }
    next += 50ms;
    std::this_thread::sleep_until(next);
    if (std::chrono::steady_clock::now() > next + 200ms) {
      next = std::chrono::steady_clock::now();
    }
  }
  if (network_thread_.joinable()) network_thread_.join();
  network_thread_started_ = false;
  {
    std::lock_guard<std::mutex> lock(game_mutex_);
    saveEverything(true);
  }
  socket_.close();
  plugins_.disableAll();
  util::Logger::instance().notice("Server stopped.");
}

} // namespace mpmpes::server
