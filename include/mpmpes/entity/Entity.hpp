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
};

inline std::int32_t networkType(EntityKind k) {
  switch (k) {
    case EntityKind::Chicken: return protocol::ENTITY_CHICKEN;
    case EntityKind::Cow: return protocol::ENTITY_COW;
    case EntityKind::Pig: return protocol::ENTITY_PIG;
    case EntityKind::Sheep: return protocol::ENTITY_SHEEP;
    case EntityKind::Zombie: return protocol::ENTITY_ZOMBIE;
    case EntityKind::ItemDrop: return protocol::ENTITY_ITEM;
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
  }
  return "Mob";
}

inline bool isMob(EntityKind k) {
  return k != EntityKind::ItemDrop;
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
