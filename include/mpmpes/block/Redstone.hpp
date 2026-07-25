#pragma once

// Simplified PE 0.14 redstone (Genisys subset): sources, wire strength 0-15,
// lamps, powered rails, levers, torches, redstone block, repeater diode,
// daylight sensor as weak comparator-like signal (true comparator not in 0.14 client).

#include "mpmpes/block/Rails.hpp"
#include "mpmpes/level/Level.hpp"
#include "mpmpes/protocol/Info.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mpmpes::block {

inline bool isRedstoneWire(std::uint8_t id) { return id == protocol::BLOCK_REDSTONE_WIRE; }
inline bool isRedstoneTorchLit(std::uint8_t id) { return id == protocol::BLOCK_REDSTONE_TORCH; }
inline bool isRedstoneTorchAny(std::uint8_t id) {
  return id == protocol::BLOCK_REDSTONE_TORCH || id == protocol::BLOCK_UNLIT_REDSTONE_TORCH;
}
inline bool isLever(std::uint8_t id) { return id == protocol::BLOCK_LEVER; }
inline bool isRedstoneBlock(std::uint8_t id) { return id == protocol::BLOCK_REDSTONE_BLOCK; }
inline bool isRepeater(std::uint8_t id) {
  return id == protocol::BLOCK_UNPOWERED_REPEATER || id == protocol::BLOCK_POWERED_REPEATER;
}
inline bool isLamp(std::uint8_t id) {
  return id == protocol::BLOCK_INACTIVE_REDSTONE_LAMP || id == protocol::BLOCK_ACTIVE_REDSTONE_LAMP;
}
inline bool isDaylightSensor(std::uint8_t id) { return id == protocol::BLOCK_DAYLIGHT_SENSOR; }
// PC-style comparator ids — may render as unknown on pure 0.14; kept for API completeness
inline bool isComparator(std::uint8_t id) {
  return id == protocol::BLOCK_UNPOWERED_COMPARATOR || id == protocol::BLOCK_POWERED_COMPARATOR;
}

inline std::int64_t rsKey(int x, int y, int z) {
  return (static_cast<std::int64_t>(static_cast<std::uint32_t>(x)) << 32) |
         (static_cast<std::int64_t>(static_cast<std::uint16_t>(y)) << 16) |
         static_cast<std::uint16_t>(static_cast<std::uint32_t>(z) & 0xffff);
}

// Lever meta: low 3 bits attachment; bit 0x8 = powered
inline bool leverPowered(std::uint8_t meta) { return (meta & 0x08) != 0; }

inline int oppositeSide(int side) {
  static constexpr int opp[6] = {1, 0, 3, 2, 5, 4};
  return (side >= 0 && side < 6) ? opp[side] : 0;
}
inline void sideOffset(int side, int& dx, int& dy, int& dz) {
  dx = dy = dz = 0;
  switch (side) {
    case 0: dy = -1; break;
    case 1: dy = 1; break;
    case 2: dz = -1; break;
    case 3: dz = 1; break;
    case 4: dx = -1; break;
    case 5: dx = 1; break;
    default: break;
  }
}

// Repeater facing (Genisys PoweredRepeater::getDirection):
// meta%4 maps to block faces: 0→3(S), 1→4(W), 2→2(N), 3→5(E).
// In Genisys this face is the INPUT side (checkPower / activate from that side).
// Output is always the opposite face (activateBlock(getOppositeDirection)).
// Previous C++ treated this table as OUTPUT and took the opposite as input —
// that made signal only enter from the visual/output end.
inline int repeaterInSide(std::uint8_t meta) {
  switch (meta % 4) {
    case 0: return 3; // south
    case 1: return 4; // west
    case 2: return 2; // north
    case 3: return 5; // east
    default: return 3;
  }
}
// Output face (arrow / signal out)
inline int repeaterOutSide(std::uint8_t meta) { return oppositeSide(repeaterInSide(meta)); }
// Delay level 1..4 from high meta bits (Genisys getDelayLevel)
inline int repeaterDelayLevel(std::uint8_t meta) {
  return static_cast<int>((meta - (meta % 4)) / 4) + 1;
}
// Scheduled ticks until state change (Genisys scheduleUpdate: delayLevel * 2)
inline int repeaterDelayTicks(std::uint8_t meta) { return repeaterDelayLevel(meta) * 2; }

// Player horizontal dir (PHP Entity::getDirection): 0=S 1=W 2=N 3=E
inline int playerHorizontalDir(float yaw) {
  float rotation = std::fmod(yaw - 90.f, 360.f);
  if (rotation < 0.f) rotation += 360.f;
  if ((0.f <= rotation && rotation < 45.f) || (315.f <= rotation && rotation < 360.f))
    return 2; // North
  if (45.f <= rotation && rotation < 135.f) return 3; // East
  if (135.f <= rotation && rotation < 225.f) return 0; // South
  return 1; // West
}

// Repeater/comparator place (PE 0.14 / Genisys PoweredRepeater::place):
//   meta = (player.getDirection() + 5) % 4
// meta%4 is Genisys getDirection() = INPUT face (see repeaterInSide).
// Plain meta=dir looks ~90° left on PE clients; the +5 offset is required.
inline std::uint8_t repeaterPlaceMeta(float yaw) {
  return static_cast<std::uint8_t>((playerHorizontalDir(yaw) + 5) % 4);
}

// Comparator: read chest fill ratio 0-15 on input face (declared before sourcePowerAt)
inline int comparatorInputStrength(level::Level& lvl, int x, int y, int z, std::uint8_t meta) {
  const int in_side = repeaterInSide(meta);
  int dx, dy, dz;
  sideOffset(in_side, dx, dy, dz);
  const int ix = x + dx, iy = y + dy, iz = z + dz;
  const auto id = lvl.getBlockId(ix, iy, iz);
  if (id == protocol::BLOCK_CHEST) {
    if (auto* chest = lvl.getChest(ix, iy, iz)) {
      int slots = 0;
      int items = 0;
      for (auto& s : chest->slots) {
        ++slots;
        if (!s.empty()) items += static_cast<int>(s.count);
      }
      if (slots <= 0 || items <= 0) return 0;
      const int max_items = slots * 64;
      int str = 1 + (items * 14) / max_items;
      if (str > 15) str = 15;
      return str;
    }
    return 0;
  }
  if (isRedstoneWire(id)) return lvl.getBlockMeta(ix, iy, iz) & 0x0f;
  // non-recursive sources only (avoid calling sourcePowerAt)
  if (isRedstoneBlock(id)) return 15;
  if (isRedstoneTorchLit(id)) return 15;
  if (isLever(id) && leverPowered(lvl.getBlockMeta(ix, iy, iz))) return 15;
  if (id == protocol::BLOCK_POWERED_REPEATER) return 15;
  return 0;
}

// Direct power contribution at a cell from non-wire sources (0-15)
inline int sourcePowerAt(level::Level& lvl, int x, int y, int z) {
  const auto id = lvl.getBlockId(x, y, z);
  if (isRedstoneBlock(id)) return 15;
  if (isRedstoneTorchLit(id)) return 15;
  if (isLever(id) && leverPowered(lvl.getBlockMeta(x, y, z))) return 15;
  if (id == protocol::BLOCK_POWERED_REPEATER) return 15;
  // Detector rail powered while cart present (meta bit 0x8)
  if (id == protocol::BLOCK_DETECTOR_RAIL && (lvl.getBlockMeta(x, y, z) & 0x8)) return 15;
  if (isDaylightSensor(id)) {
    // rough sky proxy: higher Y → more power; nether dim weaker
    int p = 5 + (y / 16);
    if (p > 15) p = 15;
    if (lvl.dimension() == protocol::DIMENSION_NETHER) p = 0;
    return p;
  }
  if (id == protocol::BLOCK_POWERED_COMPARATOR) {
    int s = comparatorInputStrength(lvl, x, y, z, lvl.getBlockMeta(x, y, z));
    if (s <= 0) s = 1;
    return s;
  }
  // pressure plates: meta>0 → on
  if ((id == protocol::BLOCK_STONE_PRESSURE_PLATE || id == protocol::BLOCK_WOODEN_PRESSURE_PLATE ||
       id == protocol::BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE ||
       id == protocol::BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE) &&
      (lvl.getBlockMeta(x, y, z) & 0x01)) {
    return 15;
  }
  return 0;
}

// Soft power into a wire from adjacent blocks
inline int adjacentSourcePower(level::Level& lvl, int x, int y, int z) {
  int best = 0;
  static constexpr int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (auto& o : d) {
    const int nx = x + o[0], ny = y + o[1], nz = z + o[2];
    if (ny < 0 || ny >= 128) continue;
    int p = sourcePowerAt(lvl, nx, ny, nz);
    // repeater only powers from its output face into this cell
    const auto id = lvl.getBlockId(nx, ny, nz);
    if (id == protocol::BLOCK_POWERED_REPEATER) {
      int out = repeaterOutSide(lvl.getBlockMeta(nx, ny, nz));
      int dx, dy, dz;
      sideOffset(out, dx, dy, dz);
      if (nx + dx != x || ny + dy != y || nz + dz != z) p = 0;
    }
    if (id == protocol::BLOCK_POWERED_COMPARATOR) {
      int out = repeaterOutSide(lvl.getBlockMeta(nx, ny, nz));
      int dx, dy, dz;
      sideOffset(out, dx, dy, dz);
      if (nx + dx != x || ny + dy != y || nz + dz != z) p = 0;
    }
    if (p > best) best = p;
  }
  return best;
}

struct RedstoneUpdate {
  int x, y, z;
  std::uint8_t id;
  std::uint8_t meta;
};

// Delayed diode state flip (Genisys scheduleUpdate: delayLevel * 2 ticks).
struct RedstoneSchedule {
  int x = 0, y = 0, z = 0;
  std::uint8_t id = 0;
  std::uint8_t meta = 0;
  int delay_ticks = 0;
};

// Full local recompute in a radius (BFS wire). Collect block changes for broadcast.
// Repeaters are NOT flipped instantly — desired state is pushed into `scheduled` with delay.
inline void recomputeRedstone(level::Level& lvl, int ox, int oy, int oz, int radius,
                              std::vector<RedstoneUpdate>& out,
                              std::vector<RedstoneSchedule>* scheduled = nullptr) {
  if (radius < 1) radius = 1;
  if (radius > 32) radius = 32;

  // 1) Collect wires in box
  std::vector<std::array<int, 3>> wires;
  for (int y = std::max(0, oy - radius); y <= std::min(127, oy + radius); ++y) {
    for (int x = ox - radius; x <= ox + radius; ++x) {
      for (int z = oz - radius; z <= oz + radius; ++z) {
        if (isRedstoneWire(lvl.getBlockId(x, y, z))) wires.push_back({x, y, z});
      }
    }
  }

  // 2) Multi-source BFS: strength on wires
  std::unordered_map<std::int64_t, int> strength;
  std::queue<std::array<int, 4>> q; // x,y,z,str
  for (auto& w : wires) {
    int s = adjacentSourcePower(lvl, w[0], w[1], w[2]);
    if (s > 0) {
      // wire receives source-1 when placed next to source (PM calcSignal PLACE)
      int ws = s - 1;
      if (ws < 0) ws = 0;
      if (s == 15) ws = 15; // strong source: torch/block often feed 15 into adjacent wire meta
      // Genisys uses getHighestStrengthAround()-1; approximate with max(s-1,0) but keep 15 for block
      if (isRedstoneBlock(lvl.getBlockId(w[0] + 1, w[1], w[2])) ||
          isRedstoneBlock(lvl.getBlockId(w[0] - 1, w[1], w[2])) ||
          isRedstoneBlock(lvl.getBlockId(w[0], w[1], w[2] + 1)) ||
          isRedstoneBlock(lvl.getBlockId(w[0], w[1], w[2] - 1)) ||
          isRedstoneBlock(lvl.getBlockId(w[0], w[1] + 1, w[2])) ||
          isRedstoneBlock(lvl.getBlockId(w[0], w[1] - 1, w[2])) ||
          isRedstoneTorchLit(lvl.getBlockId(w[0] + 1, w[1], w[2])) ||
          isRedstoneTorchLit(lvl.getBlockId(w[0] - 1, w[1], w[2])) ||
          isRedstoneTorchLit(lvl.getBlockId(w[0], w[1], w[2] + 1)) ||
          isRedstoneTorchLit(lvl.getBlockId(w[0], w[1], w[2] - 1)) ||
          isRedstoneTorchLit(lvl.getBlockId(w[0], w[1] + 1, w[2])) ||
          isRedstoneTorchLit(lvl.getBlockId(w[0], w[1] - 1, w[2]))) {
        ws = 15;
      } else if (s > 0) {
        ws = s;
      }
      auto k = rsKey(w[0], w[1], w[2]);
      if (ws > strength[k]) {
        strength[k] = ws;
        q.push({w[0], w[1], w[2], ws});
      }
    } else {
      strength[rsKey(w[0], w[1], w[2])] = 0;
    }
  }

  static constexpr int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (!q.empty()) {
    auto cur = q.front();
    q.pop();
    const int x = cur[0], y = cur[1], z = cur[2], s = cur[3];
    if (s <= 1) continue;
    const int ns = s - 1;
    auto tryWire = [&](int nx, int ny, int nz) {
      if (ny < 0 || ny >= 128) return;
      if (!isRedstoneWire(lvl.getBlockId(nx, ny, nz))) return;
      auto k = rsKey(nx, ny, nz);
      if (ns > strength[k]) {
        strength[k] = ns;
        q.push({nx, ny, nz, ns});
      }
    };
    for (auto& d : d4) {
      tryWire(x + d[0], y, z + d[1]);
      tryWire(x + d[0], y + 1, z + d[1]); // step up
      tryWire(x + d[0], y - 1, z + d[1]); // step down
    }
  }

  // 3) Apply wire meta
  for (auto& w : wires) {
    auto k = rsKey(w[0], w[1], w[2]);
    int s = 0;
    auto it = strength.find(k);
    if (it != strength.end()) s = it->second;
    if (s < 0) s = 0;
    if (s > 15) s = 15;
    const auto old = lvl.getBlockMeta(w[0], w[1], w[2]);
    if (old != static_cast<std::uint8_t>(s)) {
      lvl.setBlock(w[0], w[1], w[2], protocol::BLOCK_REDSTONE_WIRE, static_cast<std::uint8_t>(s));
      out.push_back({w[0], w[1], w[2], protocol::BLOCK_REDSTONE_WIRE, static_cast<std::uint8_t>(s)});
    }
  }

  // Helper: is position strongly powered?
  auto poweredAt = [&](int x, int y, int z) -> int {
    int best = sourcePowerAt(lvl, x, y, z);
    // wire here
    if (isRedstoneWire(lvl.getBlockId(x, y, z))) {
      best = std::max(best, static_cast<int>(lvl.getBlockMeta(x, y, z) & 0x0f));
    }
    // adjacent wire
    for (auto& d : d4) {
      if (isRedstoneWire(lvl.getBlockId(x + d[0], y, z + d[1]))) {
        best = std::max(best, static_cast<int>(lvl.getBlockMeta(x + d[0], y, z + d[1]) & 0x0f));
      }
      if (isRedstoneWire(lvl.getBlockId(x + d[0], y + 1, z + d[1]))) {
        best = std::max(best, static_cast<int>(lvl.getBlockMeta(x + d[0], y + 1, z + d[1]) & 0x0f));
      }
      if (isRedstoneWire(lvl.getBlockId(x + d[0], y - 1, z + d[1]))) {
        best = std::max(best, static_cast<int>(lvl.getBlockMeta(x + d[0], y - 1, z + d[1]) & 0x0f));
      }
    }
    if (isRedstoneWire(lvl.getBlockId(x, y + 1, z)))
      best = std::max(best, static_cast<int>(lvl.getBlockMeta(x, y + 1, z) & 0x0f));
    if (isRedstoneWire(lvl.getBlockId(x, y - 1, z)))
      best = std::max(best, static_cast<int>(lvl.getBlockMeta(x, y - 1, z) & 0x0f));
    return best;
  };

  // 4) Lamps, powered rails, repeaters, comparators in box
  for (int y = std::max(0, oy - radius); y <= std::min(127, oy + radius); ++y) {
    for (int x = ox - radius; x <= ox + radius; ++x) {
      for (int z = oz - radius; z <= oz + radius; ++z) {
        const auto id = lvl.getBlockId(x, y, z);
        const auto meta = lvl.getBlockMeta(x, y, z);

        if (isLamp(id)) {
          const bool on = poweredAt(x, y, z) > 0 || poweredAt(x, y + 1, z) > 0 ||
                          poweredAt(x, y - 1, z) > 0 || poweredAt(x + 1, y, z) > 0 ||
                          poweredAt(x - 1, y, z) > 0 || poweredAt(x, y, z + 1) > 0 ||
                          poweredAt(x, y, z - 1) > 0;
          const auto nid =
              on ? protocol::BLOCK_ACTIVE_REDSTONE_LAMP : protocol::BLOCK_INACTIVE_REDSTONE_LAMP;
          if (nid != id) {
            lvl.setBlock(x, y, z, nid, 0);
            out.push_back({x, y, z, nid, 0});
          }
        } else if (id == protocol::BLOCK_POWERED_RAIL || id == protocol::BLOCK_ACTIVATOR_RAIL) {
          // Powered/activator rails take external redstone power (bit 0x8)
          const bool on = poweredAt(x, y, z) > 0 || poweredAt(x, y - 1, z) > 0 ||
                          poweredAt(x + 1, y, z) > 0 || poweredAt(x - 1, y, z) > 0 ||
                          poweredAt(x, y, z + 1) > 0 || poweredAt(x, y, z - 1) > 0;
          const int base = meta & 0x7;
          const auto nmeta = static_cast<std::uint8_t>(base | (on ? 0x8 : 0));
          if (nmeta != meta) {
            lvl.setBlock(x, y, z, id, nmeta);
            out.push_back({x, y, z, id, nmeta});
          }
        } else if (id == protocol::BLOCK_DETECTOR_RAIL) {
          // Detector rail power is driven by cart presence (Server::tickEntities), not wire.
          // Leave meta alone here so cart occupancy bit is not clobbered.
        } else if (id == protocol::BLOCK_HOPPER) {
          // PE HopperBlock: bit 0x8 = disabled when powered by redstone
          const bool powered = poweredAt(x, y, z) > 0 || poweredAt(x, y + 1, z) > 0 ||
                               poweredAt(x + 1, y, z) > 0 || poweredAt(x - 1, y, z) > 0 ||
                               poweredAt(x, y, z + 1) > 0 || poweredAt(x, y, z - 1) > 0;
          const int facing = meta & 0x7;
          const auto nmeta = static_cast<std::uint8_t>(facing | (powered ? 0x8 : 0));
          if (nmeta != meta) {
            lvl.setBlock(x, y, z, id, nmeta);
            out.push_back({x, y, z, id, nmeta});
          }
        } else if (isRepeater(id)) {
          // Genisys: checkPower on getDirection() = input face only
          int in_side = repeaterInSide(meta);
          int dx, dy, dz;
          sideOffset(in_side, dx, dy, dz);
          const int ip = poweredAt(x + dx, y + dy, z + dz);
          const bool should = ip > 0;
          const auto nid =
              should ? protocol::BLOCK_POWERED_REPEATER : protocol::BLOCK_UNPOWERED_REPEATER;
          if (nid != id) {
            // PE/Genisys: delay before powered/unpowered flip (1–4 * 2 ticks)
            if (scheduled) {
              scheduled->push_back({x, y, z, nid, meta, repeaterDelayTicks(meta)});
            } else {
              // fallback for callers without schedule queue
              lvl.setBlock(x, y, z, nid, meta);
              out.push_back({x, y, z, nid, meta});
            }
          }
        } else if (isComparator(id)) {
          int in_str = comparatorInputStrength(lvl, x, y, z, meta);
          // side subtract mode: meta bit 0x4 = subtract (optional); ignore for now
          const bool should = in_str > 0;
          // store strength in meta low bits is wrong (facing uses low 2). Keep facing; power via id.
          const auto nid =
              should ? protocol::BLOCK_POWERED_COMPARATOR : protocol::BLOCK_UNPOWERED_COMPARATOR;
          // encode strength in upper bits if needed — for sourcePowerAt use powered id + meta&0x0f
          // preserve facing (low 2 bits) and flags
          std::uint8_t nmeta = static_cast<std::uint8_t>((meta & 0x0c) | (meta % 4));
          if (should && in_str > 0) {
            // stash strength not possible cleanly; sourcePowerAt returns 1-15 from meta for powered
            nmeta = static_cast<std::uint8_t>((meta & 0x0c) | (meta % 4));
            // use meta high: actually use full meta as facing only; strength from recompute via
            // comparatorInputStrength when reading as source — update sourcePowerAt path instead
          }
          if (nid != id) {
            lvl.setBlock(x, y, z, nid, nmeta);
            out.push_back({x, y, z, nid, nmeta});
          }
        }
      }
    }
  }
}

// Toggle lever meta bit 0x8
inline std::uint8_t leverToggleMeta(std::uint8_t meta) {
  return static_cast<std::uint8_t>(meta ^ 0x08);
}

// Lever place meta from face + yaw (Lever.php)
inline std::uint8_t leverPlaceMeta(std::uint8_t face, float yaw) {
  // face 0 down, 1 up, 2-5 sides
  if (face == 0) {
    // ceiling
    int to = 0;
    float rotation = std::fmod(yaw - 90.f, 360.f);
    if (rotation < 0) rotation += 360.f;
    // dir 0S 1W 2N 3E
    if (45.f <= rotation && rotation < 135.f) to = 1;
    else if (135.f <= rotation && rotation < 225.f) to = 0;
    else if (225.f <= rotation && rotation < 315.f) to = 1;
    else to = 0;
    return static_cast<std::uint8_t>((to % 2 != 1) ? 0 : 7);
  }
  if (face == 1) {
    int to = 0;
    float rotation = std::fmod(yaw - 90.f, 360.f);
    if (rotation < 0) rotation += 360.f;
    if (45.f <= rotation && rotation < 135.f) to = 1;
    else if (135.f <= rotation && rotation < 225.f) to = 0;
    else if (225.f <= rotation && rotation < 315.f) to = 1;
    else to = 0;
    return static_cast<std::uint8_t>((to % 2 != 1) ? 6 : 5);
  }
  // sides: 3→3, 2→4, 4→2, 5→1
  switch (face) {
    case 2: return 4;
    case 3: return 3;
    case 4: return 2;
    case 5: return 1;
    default: return 5;
  }
}

// Torch place meta from face (1E 2W 3S 4N 5 floor)
inline std::uint8_t torchPlaceMeta(std::uint8_t face) {
  switch (face) {
    case 1: return 5; // top of block → standing
    case 2: return 4; // north face → torch points N meta 4
    case 3: return 3;
    case 4: return 2;
    case 5: return 1;
    default: return 5;
  }
}

} // namespace mpmpes::block
