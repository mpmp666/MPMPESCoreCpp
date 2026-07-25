#pragma once

#include "mpmpes/item/Item.hpp"
#include "mpmpes/level/Level.hpp"
#include "mpmpes/protocol/Info.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace mpmpes::entity {

enum class EntityKind {
  Pig,
  Chicken,
  Cow,
  Sheep,
  Zombie,
  ItemDrop,
  Minecart,
  MinecartHopper,
  MinecartTNT,
  MinecartChest,
};

inline bool isMinecartKind(EntityKind k) {
  return k == EntityKind::Minecart || k == EntityKind::MinecartHopper ||
         k == EntityKind::MinecartTNT || k == EntityKind::MinecartChest;
}

inline std::int32_t networkType(EntityKind k) {
  switch (k) {
    case EntityKind::Chicken: return protocol::ENTITY_CHICKEN;
    case EntityKind::Cow: return protocol::ENTITY_COW;
    case EntityKind::Pig: return protocol::ENTITY_PIG;
    case EntityKind::Sheep: return protocol::ENTITY_SHEEP;
    case EntityKind::Zombie: return protocol::ENTITY_ZOMBIE;
    case EntityKind::ItemDrop: return protocol::ENTITY_ITEM;
    case EntityKind::Minecart: return protocol::ENTITY_MINECART;
    case EntityKind::MinecartHopper: return protocol::ENTITY_MINECART_HOPPER;
    case EntityKind::MinecartTNT: return protocol::ENTITY_MINECART_TNT;
    case EntityKind::MinecartChest: return protocol::ENTITY_MINECART_CHEST;
  }
  return protocol::ENTITY_PIG;
}

inline const char* kindName(EntityKind k) {
  switch (k) {
    case EntityKind::Chicken: return "Chicken";
    case EntityKind::Cow: return "Cow";
    case EntityKind::Pig: return "Pig";
    case EntityKind::Sheep: return "Sheep";
    case EntityKind::Zombie: return "Zombie";
    case EntityKind::ItemDrop: return "Item";
    case EntityKind::Minecart: return "Minecart";
    case EntityKind::MinecartHopper: return "MinecartHopper";
    case EntityKind::MinecartTNT: return "MinecartTNT";
    case EntityKind::MinecartChest: return "MinecartChest";
  }
  return "Mob";
}

inline bool isMob(EntityKind k) {
  return k != EntityKind::ItemDrop && !isMinecartKind(k);
}

inline EntityKind kindFromSpawnMeta(std::int16_t meta) {
  switch (meta) {
    case 10: return EntityKind::Chicken;
    case 11: return EntityKind::Cow;
    case 12: return EntityKind::Pig;
    case 13: return EntityKind::Sheep;
    case 32: return EntityKind::Zombie;
    default: return EntityKind::Pig;
  }
}

// MCPE direction vector (PHP Entity::getDirectionVector)
// x = -sin(yaw)*cos(pitch), y = -sin(pitch), z = cos(yaw)*cos(pitch)
inline void directionVector(float yaw_deg, float pitch_deg, float& ox, float& oy, float& oz) {
  const float yaw = yaw_deg * 3.14159265f / 180.f;
  const float pitch = pitch_deg * 3.14159265f / 180.f;
  const float xz = std::cos(pitch);
  ox = -xz * std::sin(yaw);
  oy = -std::sin(pitch);
  oz = xz * std::cos(yaw);
}

struct Entity {
  std::int64_t eid = 0;
  EntityKind kind = EntityKind::Pig;
  level::Level* level = nullptr;
  float x = 0, y = 0, z = 0;
  float yaw = 0, pitch = 0;
  float motion_x = 0, motion_y = 0, motion_z = 0;
  float health = 10;
  bool on_ground = true;
  bool closed = false;
  int age = 0;
  int wander_ticks = 0;
  float target_yaw = 0;
  float speed = 0.12f;
  bool hostile = false;
  // Item drop fields
  item::ItemStack item_stack;
  int pickup_delay = 0; // ticks before players can pick up
  int lifetime = 6000;  // ~5 min at 20 tps

  // Sheep: wool color 0-15; sheared → no wool drop / client DATA_COLOR_INFO bit4
  std::uint8_t sheep_color = 0;
  bool sheared = false;

  // Minecart / vehicle link (passenger is player runtime eid, or 0)
  std::int64_t linked_eid = 0;
  int linked_type = 0; // 0 none, 1 vehicle-has-passenger, 2 passenger-on-vehicle
  float cart_speed = 0.4f;
  // Rider drive input (copied from PlayerInput each tick while linked)
  float drive_forward = 0.f; // -1..1 along look / track
  float drive_strafe = 0.f;  // unused on rails; reserved
  bool drive_has_input = false;
  // Special minecart extras
  std::vector<item::ItemStack> cart_slots; // hopper=5, chest=27
  int cart_cooldown = 0;                   // hopper minecart transfer cooldown
  int tnt_fuse = -1;                       // -1 idle; >=0 primed ticks remaining
};

class EntityManager {
public:
  std::int64_t nextEid() { return next_eid_++; }

  Entity& spawn(EntityKind kind, level::Level* level, float x, float y, float z) {
    Entity e;
    e.eid = nextEid();
    e.kind = kind;
    e.level = level;
    e.x = x;
    e.y = y;
    e.z = z;
    e.pitch = 0; // keep upright; non-zero pitch can look "upside down" on some models
    e.hostile = (kind == EntityKind::Zombie);
    e.health = e.hostile ? 20.f : 10.f;
    e.speed = e.hostile ? 0.15f : 0.10f;
    e.wander_ticks = 20 + (static_cast<int>(e.eid) % 40);
    // Face a sane default; motion uses PHP direction vector signs
    e.yaw = 0;
    e.target_yaw = 0;
    if (kind == EntityKind::MinecartHopper) {
      e.cart_slots.assign(5, item::ItemStack::air());
      e.cart_cooldown = 0;
    } else if (kind == EntityKind::MinecartChest) {
      e.cart_slots.assign(27, item::ItemStack::air());
    } else if (kind == EntityKind::MinecartTNT) {
      e.tnt_fuse = -1;
    }
    if (kind == EntityKind::Sheep) {
      // mostly white (PM-ish weights simplified)
      static const std::uint8_t palette[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            7, 8, 15, 12, 14, 1};
      e.sheep_color = palette[static_cast<std::size_t>(e.eid) % (sizeof(palette) / sizeof(palette[0]))];
      e.sheared = false;
    }
    entities_[e.eid] = e;
    return entities_[e.eid];
  }

  // Drop item entity (AddItemEntity network type 64)
  Entity& spawnItem(level::Level* level, float x, float y, float z, item::ItemStack stack,
                    float mx, float my, float mz, int pickup_delay = 10) {
    Entity e;
    e.eid = nextEid();
    e.kind = EntityKind::ItemDrop;
    e.level = level;
    e.x = x;
    e.y = y;
    e.z = z;
    e.motion_x = mx;
    e.motion_y = my;
    e.motion_z = mz;
    e.pitch = 0;
    e.yaw = 0;
    e.health = 5.f;
    e.speed = 0;
    e.item_stack = std::move(stack);
    e.pickup_delay = pickup_delay;
    e.lifetime = 6000;
    e.on_ground = false;
    entities_[e.eid] = e;
    return entities_[e.eid];
  }

  Entity* get(std::int64_t eid) {
    auto it = entities_.find(eid);
    if (it == entities_.end() || it->second.closed) return nullptr;
    return &it->second;
  }

  void remove(std::int64_t eid) {
    auto it = entities_.find(eid);
    if (it != entities_.end()) it->second.closed = true;
  }

  // Simple wander / gravity AI; returns entities that moved (need broadcast)
  // Mob facing uses PHP getDirectionVector signs so walk direction matches yaw.
  std::vector<Entity*> tick(float dt_ticks = 1.f) {
    std::vector<Entity*> moved;
    std::vector<std::int64_t> to_erase;
    for (auto& [id, e] : entities_) {
      if (e.closed) {
        to_erase.push_back(id);
        continue;
      }
      if (!e.level) continue;
      ++e.age;

      if (e.kind == EntityKind::ItemDrop) {
        if (e.pickup_delay > 0) --e.pickup_delay;
        if (e.age >= e.lifetime) {
          e.closed = true;
          to_erase.push_back(id);
          continue;
        }
        // gravity + drag (PHP Item entity)
        e.motion_y -= 0.04f * dt_ticks;
        e.motion_x *= 0.98f;
        e.motion_z *= 0.98f;
        e.motion_y *= 0.98f;

        float nx = e.x + e.motion_x * dt_ticks;
        float ny = e.y + e.motion_y * dt_ticks;
        float nz = e.z + e.motion_z * dt_ticks;

        const int bx = static_cast<int>(std::floor(nx));
        const int bz = static_cast<int>(std::floor(nz));
        int ground_y = e.level->highestBlockY(bx, bz);
        float floor_y = static_cast<float>(ground_y) + 1.f;
        // item sits slightly above block surface
        if (ny <= floor_y) {
          ny = floor_y;
          e.on_ground = true;
          e.motion_y = 0;
          e.motion_x *= 0.7f;
          e.motion_z *= 0.7f;
        } else {
          e.on_ground = false;
        }
        if (ny < 0) {
          e.closed = true;
          to_erase.push_back(id);
          continue;
        }

        const bool changed =
            (std::fabs(nx - e.x) > 0.001f || std::fabs(ny - e.y) > 0.001f ||
             std::fabs(nz - e.z) > 0.001f);
        e.x = nx;
        e.y = ny;
        e.z = nz;
        if (changed) moved.push_back(&e);
        continue;
      }

      // Minecart (all variants): snap to nearest rail and slide
      if (isMinecartKind(e.kind)) {
        const int bx = static_cast<int>(std::floor(e.x));
        const int by = static_cast<int>(std::floor(e.y + 0.01f));
        const int bz = static_cast<int>(std::floor(e.z));
        auto isRail = [](std::uint8_t id) {
          return id == protocol::BLOCK_RAIL || id == protocol::BLOCK_POWERED_RAIL ||
                 id == protocol::BLOCK_DETECTOR_RAIL || id == protocol::BLOCK_ACTIVATOR_RAIL;
        };
        // Prefer rail at / just under cart (include +1 for slope crest)
        int rx = bx, ry = by, rz = bz;
        bool on_rail = false;
        auto tryRailAt = [&](int tx, int ty, int tz) {
          if (ty < 0 || ty >= 128) return false;
          if (!isRail(e.level->getBlockId(tx, ty, tz))) return false;
          rx = tx;
          ry = ty;
          rz = tz;
          return true;
        };
        if (tryRailAt(bx, by, bz) || tryRailAt(bx, by - 1, bz) || tryRailAt(bx, by + 1, bz)) {
          on_rail = true;
        } else {
          for (int dy = 1; dy >= -1 && !on_rail; --dy) {
            for (int dx = -1; dx <= 1 && !on_rail; ++dx) {
              for (int dz = -1; dz <= 1 && !on_rail; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                if (tryRailAt(bx + dx, by + dy, bz + dz)) on_rail = true;
              }
            }
          }
        }
        if (!on_rail) {
          // freefall if off track
          e.motion_y -= 0.04f * dt_ticks;
          e.x += e.motion_x * dt_ticks;
          e.y += e.motion_y * dt_ticks;
          e.z += e.motion_z * dt_ticks;
          if (e.y < 0) {
            e.closed = true;
            to_erase.push_back(id);
            continue;
          }
          moved.push_back(&e);
          continue;
        }
        // center on rail block (slightly higher on slopes for client feel)
        e.x = static_cast<float>(rx) + 0.5f;
        e.y = static_cast<float>(ry) + 0.125f;
        e.z = static_cast<float>(rz) + 0.5f;

        const auto rid = e.level->getBlockId(rx, ry, rz);
        const auto rmeta = e.level->getBlockMeta(rx, ry, rz);
        int base = (rid == protocol::BLOCK_RAIL) ? (rmeta & 0x0f) : (rmeta & 0x7);
        if (base < 0 || base > 9) base = 0;

        // connection table (same as Rails.hpp) — xz only; y resolved by search
        static const int ends[10][2][2] = {
            {{0, 1}, {0, -1}}, {{1, 0}, {-1, 0}}, {{1, 0}, {-1, 0}}, {{1, 0}, {-1, 0}},
            {{0, 1}, {0, -1}}, {{0, 1}, {0, -1}}, {{1, 0}, {0, 1}},  {{0, 1}, {-1, 0}},
            {{-1, 0}, {0, -1}}, {{0, -1}, {1, 0}},
        };
        // Prefer existing motion; if passenger is driving, prefer look/input direction.
        // Do NOT invent motion from cart yaw alone — that made idle carts auto-forward.
        float prefer_dx = e.motion_x;
        float prefer_dz = e.motion_z;
        if (e.drive_has_input && std::fabs(e.drive_forward) > 0.05f) {
          prefer_dx = -std::sin(e.yaw * 3.14159265f / 180.f) * e.drive_forward;
          prefer_dz = std::cos(e.yaw * 3.14159265f / 180.f) * e.drive_forward;
        } else if (std::fabs(prefer_dx) < 0.01f && std::fabs(prefer_dz) < 0.01f) {
          // coast / empty cart: keep previous rail axis via residual motion only
          prefer_dx = e.motion_x;
          prefer_dz = e.motion_z;
        }
        int best_i = 0;
        float best = -1e9f;
        for (int i = 0; i < 2; ++i) {
          const float dx = static_cast<float>(ends[base][i][0]);
          const float dz = static_cast<float>(ends[base][i][1]);
          const float dot = dx * prefer_dx + dz * prefer_dz;
          if (dot > best) {
            best = dot;
            best_i = i;
          }
        }
        // If no preferred direction (idle), keep existing motion axis or stay put.
        const bool has_prefer =
            std::fabs(prefer_dx) > 0.01f || std::fabs(prefer_dz) > 0.01f;
        if (!has_prefer && best < 0.01f) {
          // pick end 0 as default axis for powered rails only; else no move
          best_i = 0;
        }
        const int mdx = ends[base][best_i][0];
        const int mdz = ends[base][best_i][1];

        // Slope: meta 2 ascend E, 3 ascend W, 4 ascend N, 5 ascend S
        // Uphill travel → try y+1 first; downhill → y-1 first (this was broken before)
        int slope_up_dx = 0, slope_up_dz = 0; // unit dir that goes UP this slope
        if (base == 2) {
          slope_up_dx = 1;
        } else if (base == 3) {
          slope_up_dx = -1;
        } else if (base == 4) {
          slope_up_dz = -1;
        } else if (base == 5) {
          slope_up_dz = 1;
        }
        const bool on_slope = (base >= 2 && base <= 5);
        const bool going_up =
            on_slope && ((slope_up_dx != 0 && mdx == slope_up_dx) ||
                         (slope_up_dz != 0 && mdz == slope_up_dz));
        const bool going_down =
            on_slope && ((slope_up_dx != 0 && mdx == -slope_up_dx) ||
                         (slope_up_dz != 0 && mdz == -slope_up_dz));

        float speed = e.cart_speed;
        // powered rail boost / brake
        if (rid == protocol::BLOCK_POWERED_RAIL) {
          if (rmeta & 0x8) speed = 0.6f;
          else speed = 0.05f; // unpowered brake
        }
        // slight gravity assist downhill / slow uphill
        if (going_down) speed = std::min(0.7f, speed + 0.12f);
        else if (going_up) speed = std::max(0.2f, speed - 0.08f);

        const bool has_passenger = e.linked_eid != 0;
        const bool powered = (rid == protocol::BLOCK_POWERED_RAIL) && (rmeta & 0x8);
        const bool is_normal_rail = (rid == protocol::BLOCK_RAIL);
        // Normal rails: only advance with residual motion, slope gravity, or rider input.
        // Never auto-drive just because someone is sitting still looking forward.
        const bool rider_driving =
            has_passenger && e.drive_has_input && std::fabs(e.drive_forward) > 0.05f;
        const bool coasting =
            std::fabs(e.motion_x) > 0.01f || std::fabs(e.motion_z) > 0.01f;
        const bool should_move =
            powered || going_down || coasting || rider_driving ||
            (!is_normal_rail && has_passenger && coasting);
        if (should_move) {
          const int nx = rx + mdx;
          const int nz = rz + mdz;
          // y search order depends on slope travel
          int y_try[3];
          if (going_up) {
            y_try[0] = ry + 1;
            y_try[1] = ry;
            y_try[2] = ry - 1;
          } else if (going_down) {
            y_try[0] = ry - 1;
            y_try[1] = ry;
            y_try[2] = ry + 1;
          } else {
            y_try[0] = ry;
            y_try[1] = ry + 1;
            y_try[2] = ry - 1;
          }
          bool advanced = false;
          for (int yi = 0; yi < 3; ++yi) {
            const int ny = y_try[yi];
            if (ny < 0 || ny >= 128) continue;
            if (!isRail(e.level->getBlockId(nx, ny, nz))) continue;
            e.x = static_cast<float>(nx) + 0.5f;
            e.y = static_cast<float>(ny) + 0.125f;
            e.z = static_cast<float>(nz) + 0.5f;
            e.motion_x = static_cast<float>(mdx) * speed;
            e.motion_z = static_cast<float>(mdz) * speed;
            e.motion_y = static_cast<float>(ny - ry) * speed;
            e.yaw = -std::atan2(static_cast<float>(mdx), static_cast<float>(mdz)) * 180.f /
                    3.14159265f;
            e.on_ground = true;
            moved.push_back(&e);
            advanced = true;
            break;
          }
          if (!advanced) {
            // still on this slope tile: nudge along slope height for visuals
            if (on_slope && (powered || rider_driving || coasting || going_down)) {
              e.motion_x = static_cast<float>(mdx) * speed * 0.35f;
              e.motion_z = static_cast<float>(mdz) * speed * 0.35f;
              // keep motion so next tick retries downhill/uphill neighbor
              moved.push_back(&e);
            } else if (powered || rider_driving) {
              // stuck at end of track / missing neighbor: keep mild residual only if driving
              e.motion_x = static_cast<float>(mdx) * speed * 0.5f;
              e.motion_z = static_cast<float>(mdz) * speed * 0.5f;
              e.x += e.motion_x * dt_ticks;
              e.z += e.motion_z * dt_ticks;
              moved.push_back(&e);
            } else {
              e.motion_x *= 0.8f;
              e.motion_z *= 0.8f;
            }
          }
        } else {
          // Idle on rail (no input / not powered / not coasting): stay put
          e.motion_x = 0.f;
          e.motion_z = 0.f;
          e.motion_y = 0.f;
          e.on_ground = true;
        }
        continue;
      }

      --e.wander_ticks;

      // gravity
      if (!e.on_ground) {
        e.motion_y -= 0.04f * dt_ticks;
      } else if (e.motion_y < 0) {
        e.motion_y = 0;
      }

      // wander AI — face target_yaw then walk along PHP direction vector
      if (e.wander_ticks <= 0) {
        e.wander_ticks = 40 + (static_cast<int>(id * 17) % 80);
        std::uniform_real_distribution<float> dist(0.f, 360.f);
        e.target_yaw = dist(rng_);
        // sometimes stand still
        if ((id + e.age) % 5 == 0) {
          e.motion_x = 0;
          e.motion_z = 0;
        } else {
          float dx = 0, dy = 0, dz = 0;
          // pitch=0 for upright walking (avoids "upside-down" look)
          directionVector(e.target_yaw, 0.f, dx, dy, dz);
          e.motion_x = dx * e.speed;
          e.motion_z = dz * e.speed;
          e.yaw = e.target_yaw;
          e.pitch = 0.f;
        }
      } else if (e.motion_x != 0.f || e.motion_z != 0.f) {
        // keep yaw aligned with actual horizontal motion (atan2)
        // MC yaw: 0 looks +Z, increases clockwise looking down? PHP uses -sin/cos
        // so yaw matching directionVector means facing = walk direction.
        e.yaw = e.target_yaw;
        e.pitch = 0.f;
      }

      float nx = e.x + e.motion_x * dt_ticks;
      float ny = e.y + e.motion_y * dt_ticks;
      float nz = e.z + e.motion_z * dt_ticks;

      // ground collision: snap to surface height
      const int bx = static_cast<int>(std::floor(nx));
      const int bz = static_cast<int>(std::floor(nz));
      int ground_y = e.level->highestBlockY(bx, bz);
      float floor_y = static_cast<float>(ground_y + 1);
      if (ny <= floor_y) {
        ny = floor_y;
        e.on_ground = true;
        e.motion_y = 0;
      } else {
        e.on_ground = false;
      }

      // keep above void / bounds
      if (ny < 0) {
        ny = 64;
        e.motion_y = 0;
      }
      if (ny > 120) ny = 120;

      const bool changed =
          (std::fabs(nx - e.x) > 0.001f || std::fabs(ny - e.y) > 0.001f ||
           std::fabs(nz - e.z) > 0.001f || std::fabs(e.yaw - e.target_yaw) > 0.5f);
      e.x = nx;
      e.y = ny;
      e.z = nz;
      if (changed) moved.push_back(&e);
    }
    for (auto id : to_erase) entities_.erase(id);
    return moved;
  }

  std::unordered_map<std::int64_t, Entity>& all() { return entities_; }
  const std::unordered_map<std::int64_t, Entity>& all() const { return entities_; }

  // Spawn a few passive mobs near spawn of each non-void level
  void seedWorld(level::Level& level, int count = 6) {
    if (level.generator() == level::GeneratorType::Void) return;
    const auto sp = level.spawn();
    for (int i = 0; i < count; ++i) {
      EntityKind kinds[] = {EntityKind::Pig, EntityKind::Chicken, EntityKind::Cow,
                            EntityKind::Sheep};
      auto kind = kinds[i % 4];
      float ox = static_cast<float>((i * 3) % 17 - 8);
      float oz = static_cast<float>((i * 5) % 17 - 8);
      float x = static_cast<float>(sp.x) + ox + 0.5f;
      float z = static_cast<float>(sp.z) + oz + 0.5f;
      int gy = level.highestBlockY(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(z)));
      float y = static_cast<float>(gy + 1);
      spawn(kind, &level, x, y, z);
    }
    // one zombie at night-ish far
    spawn(EntityKind::Zombie, &level, static_cast<float>(sp.x) + 12.5f,
          static_cast<float>(sp.y), static_cast<float>(sp.z) + 8.5f);
  }

private:
  std::int64_t next_eid_ = 1000; // leave room below for players
  std::unordered_map<std::int64_t, Entity> entities_;
  std::mt19937 rng_{std::random_device{}()};
};

} // namespace mpmpes::entity
